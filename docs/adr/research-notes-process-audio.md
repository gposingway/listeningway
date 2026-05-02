# Research notes: per-process audio capture for ProcessAudioSource

Consolidated findings from three parallel research agents (May 2026). Feeds the upcoming ADR-0009 (ProcessAudioSource design).

## Summary

The industry has converged on `ActivateAudioInterfaceAsync` with `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS` (Microsoft's "Application Audio Capture" API). OBS 28+, Streamlabs, and Discord all use it. NVIDIA Broadcast and Krisp ship virtual driver pairs (different UX, driver install required); Voicemeeter and VB-Cable are pure user-routed virtual cables. There is no second blessed Microsoft API for per-app audio buffers; only metering data is available via the Audio Session APIs.

Listeningway's situation is unusually convenient: we are a ReShade addon injected into the host game process, so the target PID is `GetCurrentProcessId()`. The whole "find the game's PID, watch for launchers reparenting, child renderer processes, and so on" complexity does not apply to us.

## How major tools solve the problem

| Tool | Technique | Driver install? | Routing? |
|---|---|---|---|
| **OBS Studio** (28+) | `ActivateAudioInterfaceAsync` + `PROCESS_LOOPBACK` | No | Pick PID from dropdown |
| **Streamlabs Desktop** | OBS fork; same API path | No | Same as OBS |
| **Discord** stream-with-audio | Historically API hooking; now believed migrated to `PROCESS_LOOPBACK` on supported builds | No | Automatic |
| **NVIDIA Broadcast / RTX Voice** | Virtual mic and speaker driver pair | Yes (signed) | Manual per app |
| **Krisp** | Virtual mic and speaker driver pair | Yes (signed) | Manual per app |
| **Voicemeeter** (VB-Audio) | Virtual cable plus user-mode mixer | Yes (signed) | Manual per app |
| **VB-Cable** | Virtual cable, no mixer | Yes (signed) | Manual per app |
| **XSplit Broadcaster** | Virtual cable plus `PROCESS_LOOPBACK` on Win11 | Yes (driver) | Mix |
| **Equalizer APO** | sAPO endpoint-level processing | Yes (registry) | Endpoint-only |
| **VRChat AudioLink** | In-process Unity `GetOutputData` | No | Same process |

The pattern: anything serious that doesn't ship a driver uses `PROCESS_LOOPBACK`. Anything that wanted broad reach and didn't have the OS API shipped a driver pair instead.

## The official API in detail

`ActivateAudioInterfaceAsync` with `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`.

### Availability

- Officially Win10 build 20348 / Win11 21H2 per Microsoft's docs.
- In practice OBS reports it works on recent Win10 22H2 builds ([OBS PR #5218](https://github.com/obsproject/obs-studio/pull/5218)).
- No elevation, no UAC prompt, no manifest tweaks. The activation runs through `audiodg.exe`; the calling app never opens a process handle.

### Format negotiation

`GetMixFormat` and `IsFormatSupported` return `E_NOTIMPL` on the virtual device returned by process loopback (it is `AudioSes!CMixerClient`, which doesn't implement the COM interface methods).

You must hardcode the format before calling `Initialize`. OBS uses:

- `WAVEFORMATEXTENSIBLE`
- `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`
- 32-bit, 48 kHz, 2 channels
- channel mask = `KSAUDIO_SPEAKER_STEREO`

The audio engine resamples, requantizes, and downmixes internally to fit. This is the OBS-tested format and the safe default in 2026.

### Flags

```cpp
constexpr DWORD kStreamFlags =
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
    AUDCLNT_STREAMFLAGS_LOOPBACK;
```

Event mode works for process loopback (unlike system loopback, where `EVENTCALLBACK | LOOPBACK` is documented broken because the event never fires). Process loopback uses a different code path through `ActivateAudioInterfaceAsync`, and event mode is the OBS-tested path.

`AUTOCONVERTPCM` is unnecessary because we control the format. The MS sample misplaces this flag in `hnsPeriodicity`, so don't copy-paste blindly ([windows-classic-samples #196](https://github.com/microsoft/Windows-classic-samples/issues/196)).

### Process tree mode

`PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE` includes the target PID and its child processes. Recommended for:

- Games that spawn separate audio child processes (Chromium-based EVE, some launchers).
- Games with media playback components in helper processes.

`EXCLUDE_TARGET_PROCESS_TREE` is the inverse: capture everything *except* the target. Useful for "system audio minus voice chat".

For Listeningway targeting `GetCurrentProcessId()`, the INCLUDE mode is correct: the addon runs in the game process and we want the game's audio plus any child renderers.

### Known gotchas (still current in 2026)

| Bug | Status | Mitigation |
|---|---|---|
| `GetMixFormat` returns `E_NOTIMPL` | Confirmed unfixed | Hardcode format (above) |
| Crash on alt-tab from exclusive-fullscreen | [Q&A 1036316](https://learn.microsoft.com/en-us/answers/questions/1036316/) unfixed | SEH-wrap `GetBuffer`; tear down and re-activate on AV |
| Rapid Start/Stop race crash | Same Q&A | State machine: only `Stop` while `Capturing` |
| 60-minute audio drift / crackle | OBS issues #8064, #8086 still open | Watchdog timer: re-activate if `u64DevicePosition` stalls |
| ARM64: `GetNextPacketSize` returns 0 | [Q&A 5694431](https://learn.microsoft.com/en-us/answers/questions/5694431/) | x64-only addon; not relevant |
| MSIX/UWP packaged apps | Works | Not applicable for FFXIV |
| Exclusive-mode targets | Not captured | Detect and warn (rare on FFXIV and modern games) |

### Anti-cheat exposure

None reported. OBS and `bozbez/win-capture-audio` have a massive deployed base since 2022 with no anti-cheat reports. Architecturally, the API is invisible to AC:

- No `OpenProcess` against the target.
- No memory reads.
- No DLL injection.
- The PID is passed to `MMDevAPI`, which talks to `audiodg.exe`. AC monitors handle access, memory, and module loads, none of which happen here.

Microsoft documents the API for arbitrary PIDs without a consent prompt (unlike screen capture's `GraphicsCaptureItem`, which requires the system picker).

For Listeningway, we target our own PID, which makes the AC question even more academic. FFXIV has no anti-cheat.

## Alternative isolation approaches surveyed

### Audio Session APIs (`IAudioMeterInformation`)

Confirmed: per-session metering only. `GetPeakValue` returns a 0..1 peak per channel per device period. There is no raw audio buffer.

What it IS useful for in Listeningway:

- Detect that a target PID has an active audio session.
- A "game has audio" UI signal.
- A cheap pre-gate for the system-loopback fallback (only run analysis when the target session has non-zero peak; doesn't isolate Discord output, but suppresses noise when the game is silent).

Source: [matthewvaneerde](https://matthewvaneerde.wordpress.com/2013/09/26/getting-peak-meters-and-volume-settings-for-all-apps-and-audio-devices-on-the-system/), [MS Learn](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudiometerinformation).

**Decision**: ship as a complement, not a fallback. Cheap, no permissions, valuable as a "game has audio" signal.

### API hooking (Detours / MinHook on `IAudioRenderClient`)

What pre-2022 Discord and the original `bozbez/win-capture-audio` did: inject a DLL into the target, hook `GetBuffer` and `ReleaseBuffer`, copy the buffer to shared memory.

**Skip** for these reasons:

- Anti-cheat risk for online games. EAC, BattlEye, Vanguard, and Ricochet flag third-party DLL injection and IAT/inline hooks on COM v-tables.
- Crash risk in the audio render path.
- Worse than `PROCESS_LOOPBACK` when the latter works.

Even though Listeningway is *already* injected via ReShade, adding hooks on top still trips heuristics.

### Virtual audio device drivers

VB-Cable, Voicemeeter, SAR, VAC. All require the user to install a signed driver and manually route the target app's output to the virtual device.

- VB-Cable: free for personal use, paid for commercial. Cannot be bundled (licensing). Document only as a power-user fallback.
- Voicemeeter: same family, more mixer features.
- SAR (Synchronous Audio Router): GPLv3, kernel driver, niche. Skip.
- VAC: paid shareware. Skip.

**Decision**: VB-Cable is the only one to mention in user docs as a "if you want isolation on Windows older than 22H2, here's the path" option. Don't ship it, don't auto-install it.

### APOs (Audio Processing Objects)

System-effect APOs see the endpoint mix, not per-process streams. Not isolation. Skip for this problem.

### Media Foundation source readers

Consume files and capture devices. There is no source for "another process's render stream". Skip.

## What Listeningway gains by being already-injected

This is the killer fact. Most "how do major tools do this" research is coloured by tools that run *outside* the target process and have to discover the PID, watch for launchers, deal with anti-cheat scanning their hook engine, and so on.

Listeningway is a ReShade addon, loaded as a DLL into the game's own process. That changes everything:

1. No PID resolution. `GetCurrentProcessId()` is the target. Zero complexity around launcher reparenting, child processes that inherit audio, finding the right PID in a process tree, anti-cheat probing.
2. No anti-cheat handle access. The addon doesn't touch the game's process handle from outside; it's already inside.
3. Format negotiation is simplified. We can use the OBS format verbatim and the engine handles conversion.
4. Process-tree INCLUDE mode is safe. The worst case is that it captures child renderer processes the game spawned, which is what we'd want anyway.

This is closer to the VRChat AudioLink situation (in-process audio read) than to OBS's situation (out-of-process discovery plus capture).

## Recommended design for Listeningway

### Primary path

`ProcessAudioSource` implementing `IAudioSource`:

- `available()` returns true if Win10 build is at least 20348 (`RtlGetVersion`) AND `ActivateAudioInterfaceAsync` resolves at runtime.
- `open()` constructs the `AUDIOCLIENT_ACTIVATION_PARAMS` with `TargetProcessId = GetCurrentProcessId()` and `ProcessLoopbackMode = INCLUDE_TARGET_PROCESS_TREE`.
- Hardcoded format: WAVEFORMATEXTENSIBLE / IEEE_FLOAT / 32-bit / 48 kHz / stereo / `KSAUDIO_SPEAKER_STEREO` mask.
- Stream flags: `LOOPBACK | EVENTCALLBACK`. Event-driven capture thread.
- State machine: `Initialized → Starting → Capturing → Stopping → Stopped`. Reject `Stop` unless `Capturing`.
- Watchdog: every N minutes, verify `u64DevicePosition` advances; if stalled, tear down and re-activate (the "60-minute drift" mitigation).
- Honour `AUDCLNT_BUFFERFLAGS_SILENT` explicitly (push zeros to the pipeline rather than possibly-stale memory).
- SEH-wrap `GetBuffer` reads to survive alt-tab edge cases; on AV, tear down and re-activate.

### Source registration

Add to `DllMain` after the existing two:

```cpp
g_system->register_source(std::make_unique<lw::source::WasapiLoopbackSource>());
g_system->register_source(std::make_unique<lw::source::ProcessAudioSource>());
g_system->register_source(std::make_unique<lw::source::OffSource>());
```

`Info` for ProcessAudioSource:

```cpp
{
    .code = "process",
    .display = "Game Audio Only (Process Loopback)",
    .is_default = false,
    .order = 1,                  // between system and off
    .activates_capture = true,
}
```

### Fallback / availability

- If `available()` is false (older OS, API missing), the dropdown shows the option grayed with a tooltip: *"Requires Windows 10 build 20348 (22H2) or Windows 11."*
- If `open()` fails despite reported availability (corner cases like the 22H2 loopback bug), `AudioSystem` falls back through its state-machine error path; the user sees the error and can re-pick System loopback.

### v2.x bonus (separate PR)

`IAudioMeterInformation` peak gating for the system-loopback path: when configured, only run DSP when the addon's PID has non-zero peak in its audio session. Complementary to ProcessAudioSource, useful for users on older OSes who can't get true isolation but still want the visualization to be quiet when the game is silent.

## Out of scope

- Bundling or auto-installing virtual audio drivers (VB-Cable and similar).
- DLL hooking the game's `IAudioRenderClient` (anti-cheat exposure).
- APO-based capture (wrong layer).
- Non-Windows targets (per ADR-0007).

## References

- [PROCESS_LOOPBACK_MODE](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-process_loopback_mode).
- [AUDIOCLIENT_ACTIVATION_PARAMS](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_activation_params).
- [ActivateAudioInterfaceAsync](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync).
- [ApplicationLoopback sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/), the canonical reference.
- [OBS PR #5218](https://github.com/obsproject/obs-studio/pull/5218), the production reference.
- [OBS win-wasapi.cpp](https://github.com/obsproject/obs-studio/blob/master/plugins/win-wasapi/win-wasapi.cpp).
- [bozbez/win-capture-audio](https://github.com/bozbez/win-capture-audio), an alternative reference implementation.
- [MS Q&A 1125409: `GetMixFormat` E_NOTIMPL workaround](https://learn.microsoft.com/en-us/answers/questions/1125409/).
- [MS Q&A 1036316: alt-tab crash](https://learn.microsoft.com/en-us/answers/questions/1036316/).
- [windows-classic-samples #196](https://github.com/microsoft/Windows-classic-samples/issues/196), [#275](https://github.com/microsoft/Windows-classic-samples/issues/275).
- [OBS issue #9669: process-tree toggle](https://github.com/obsproject/obs-studio/issues/9669).
- [IAudioMeterInformation](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudiometerinformation).
- [Matthew van Eerde: peak meters per session](https://matthewvaneerde.wordpress.com/2013/09/26/getting-peak-meters-and-volume-settings-for-all-apps-and-audio-devices-on-the-system/).
- [VB-Audio Cable](https://vb-audio.com/Cable/) (power-user fallback only).
