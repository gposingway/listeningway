# ADR-0009: ProcessAudioSource. Per-process loopback

## Status

Accepted, 2026-05-02

## Context

`WasapiLoopbackSource` (ADR-0007) captures the system default render endpoint. That is a great default. It just works and needs no permissions. But it has one user-visible weakness: **the visualization reacts to everything the user hears**, including Discord, browser tabs, music players, system notifications, alt-tabbed apps. For a shader effect tied to *the game's* music, that bleed is the difference between an immersive effect and a distraction.

The user asked for source isolation. Three parallel research agents surveyed the industry; full notes in [research-notes-process-audio.md](research-notes-process-audio.md). Summary:

- **OBS Studio (28+), Streamlabs, Discord (modern builds)** all use `ActivateAudioInterfaceAsync` with `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`. Microsoft's "Application Audio Capture" API, available on Windows 10 build 20348 (22H2) and Windows 11.
- **NVIDIA Broadcast, Krisp, Voicemeeter, VB-Cable** ship signed virtual audio drivers and require manual per-app routing.
- **Pre-2022 Discord and the original `bozbez/win-capture-audio`** used DLL injection and IAT/inline hooks on `IAudioRenderClient`. Since 2022 they have all migrated away from that path.
- **APOs (system-effect audio processing objects)** see the endpoint mix, not per-process streams. Wrong layer.
- **Media Foundation source readers** consume files and capture devices. Wrong domain.

There is no second blessed Microsoft API for per-process audio buffers. Only metering data is exposed via `IAudioMeterInformation`.

Listeningway has a **uniquely advantageous situation**: it is a ReShade addon, loaded as a DLL into the game's own process. The "find the game's PID, deal with launchers reparenting, watch child processes" complexity that out-of-process tools must solve does not apply. `GetCurrentProcessId()` IS the target.

## Decision

Add a third `IAudioSource` implementation, `ProcessAudioSource`, alongside `WasapiLoopbackSource` and `OffSource`. Use Microsoft's process-loopback API targeting `GetCurrentProcessId()` with the process-tree-include mode.

### API path

```cpp
AUDIOCLIENT_ACTIVATION_PARAMS params = {};
params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
params.ProcessLoopbackParams.TargetProcessId      = GetCurrentProcessId();
params.ProcessLoopbackParams.ProcessLoopbackMode  =
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

PROPVARIANT activate_params = {};
activate_params.vt          = VT_BLOB;
activate_params.blob.cbSize = sizeof(params);
activate_params.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

IActivateAudioInterfaceAsyncOperation* op = nullptr;
ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                            __uuidof(IAudioClient), &activate_params,
                            completion_handler, &op);
```

The `IActivateAudioInterfaceCompletionHandler` posts back to a Win32 event; the source's main thread waits on the event with a timeout, then proceeds. This matches the OBS-tested pattern.

### Format. Hardcoded

`GetMixFormat` and `IsFormatSupported` return `E_NOTIMPL` on the virtual device returned by process loopback. Format **must** be hardcoded:

```cpp
WAVEFORMATEXTENSIBLE want = {};
want.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
want.Format.nChannels            = 2;
want.Format.nSamplesPerSec       = 48000;
want.Format.wBitsPerSample       = 32;
want.Format.nBlockAlign          = 8;          // 2 ch × 32-bit
want.Format.nAvgBytesPerSec      = 48000 * 8;
want.Format.cbSize               = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
want.Samples.wValidBitsPerSample = 32;
want.dwChannelMask               = KSAUDIO_SPEAKER_STEREO;
want.SubFormat                   = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
```

The audio engine resamples / requantizes / downmixes internally to fit. This is the OBS-tested format and it's safe in 2026.

### Stream flags

```cpp
constexpr DWORD kStreamFlags =
    AUDCLNT_STREAMFLAGS_LOOPBACK |
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
```

Event mode **works** for process loopback (unlike system loopback, where `EVENTCALLBACK | LOOPBACK` is documented broken. The event never fires). Process loopback uses a different code path through `ActivateAudioInterfaceAsync` and event-mode is the OBS-tested path.

### State machine

```
Initialized → Starting → Capturing → Stopping → Stopped
                            │
                            └─→ Error (re-activate on next start())
```

Reject `stop()` unless `Capturing`; reject `start()` unless `Stopped` or fresh `Initialized`. Mitigates the rapid-Start/Stop race documented in [MS Q&A 1036316](https://learn.microsoft.com/en-us/answers/questions/1036316/).

### Robustness

- **SEH-wrap `GetBuffer`**: alt-tab from exclusive-fullscreen can crash inside `GetBuffer`. On AV exception, tear down + re-activate.
- **Watchdog**: every N (60+) seconds, verify `u64DevicePosition` from `GetBuffer` advances. If stalled, force re-activate. Mitigates the 60-minute drift bug (OBS issues #8064, #8086).
- **Honor `AUDCLNT_BUFFERFLAGS_SILENT`**: when set, push zero samples to the pipeline rather than possibly-stale memory.

### Source registration

```cpp
g_system->register_source(std::make_unique<lw::source::WasapiLoopbackSource>());  // order = 0
g_system->register_source(std::make_unique<lw::source::ProcessAudioSource>());    // order = 1
g_system->register_source(std::make_unique<lw::source::OffSource>());             // order = 100
```

`ProcessAudioSource::info()`:

```cpp
{
    .code = "process",
    .display = "Game Audio Only (Process Loopback)",
    .is_default = false,                  // System remains the default
    .order = 1,
    .activates_capture = true,
}
```

### Availability

`available()` returns true iff:

1. Windows 10 build ≥ 20348 (`RtlGetVersion` from `ntdll.dll`); AND
2. `ActivateAudioInterfaceAsync` resolves at runtime via `GetProcAddress` on `MMDevApi.dll`.

If unavailable, the dropdown still shows the option but disabled, with a tooltip: *"Requires Windows 10 22H2 (build 20348+) or Windows 11."*

If `available()` is true but `open()` fails (corner cases like the 22H2 process-tree bug), `AudioSystem` falls back through its existing state-machine error path; user sees the error and can re-pick `system`.

## Consequences

### Positive

- **True isolation.** The visualization tracks the game's audio *only*. Discord, browser, music app, system notifications all become invisible. This is what users have been asking for.
- **No driver install.** Unlike NVIDIA Broadcast / VB-Cable / Voicemeeter, no installer, no signed-driver rollout, no UAC.
- **No anti-cheat exposure.** `ActivateAudioInterfaceAsync` against our own PID is invisible to anti-cheat: no `OpenProcess`, no memory reads, no DLL injection beyond what ReShade already does. FFXIV ships no anti-cheat anyway, but this is portability-friendly.
- **In-process advantage.** `GetCurrentProcessId()` IS the target. None of the OBS-grade complexity around discovering the right PID, watching launchers, or handling separate audio child processes applies. The addon is loaded inside the game already, and `INCLUDE_TARGET_PROCESS_TREE` covers any child renderers.
- **Adapter pattern reused.** Source layer was designed for exactly this (ADR-0003): a new source slots in without touching the ring, DSP, snapshot, or output layers.

### Negative

- **OS floor.** Windows 10 prior to build 20348 (22H2 update) and all Windows 11 builds prior to 21H2 cannot use this source. Mitigation: graceful disable + tooltip; `system` source remains the default.
- **Hardcoded format.** Per-device sample-rate awareness is gone for this source. Engine handles conversion. In practice this is a non-issue: 48 kHz / float / stereo is a near-universal target.
- **Documented edge cases.** Alt-tab from exclusive-fullscreen, 60-minute drift, rapid Start/Stop. Each has a mitigation; none has been a user-blocking issue in OBS's deployed base since 2022.
- **One more source to test.** Replay tests don't cover the source layer (they hit `FileSource`); manual in-game verification covers `ProcessAudioSource`.

### Neutral

- **Latency.** Process-loopback latency is in the same ballpark as system loopback (~20 ms), per OBS measurements. No noticeable change for the visualization.
- **CPU.** Negligible. The audio engine does the per-process mixing / conversion in `audiodg.exe`; we just consume the result.

## Alternatives considered

### Bundle a virtual audio driver (VB-Cable / similar)

**Rejected.** Cannot redistribute VB-Cable (license forbids bundling). Building our own driver pair is an order of magnitude more complex than the entire v2 engine, requires WHQL signing, and introduces a per-machine install. Power users who want this on older OSes can install VB-Cable themselves; we'll mention it in user docs as a fallback, no more.

### DLL hooking the game's `IAudioRenderClient`

**Rejected.** Anti-cheat heuristics flag IAT/inline hooks on COM v-tables. Even though Listeningway is *already* injected via ReShade, adding hooks on top still trips heuristics for online games (EAC, BattlEye, Vanguard, Ricochet). Strictly worse than `PROCESS_LOOPBACK` when the latter works.

### APOs

**Rejected.** Endpoint-only. Wrong layer for per-process isolation.

### Media Foundation source reader

**Rejected.** Consumes files and capture devices, not other-process render streams. Wrong API for the problem.

### Replace `WasapiLoopbackSource` with `ProcessAudioSource`

**Rejected.** The OS floor would lock out Windows 10 < 22H2 users entirely. Both sources should ship; user picks. `system` remains the default because it has no OS floor.

### `IAudioMeterInformation` peak gating

**Deferred to a separate PR.** Per-session peak metering is **per-session metering only**. No raw audio buffer. It can serve as a "game has audio" pre-gate for the system-loopback path, suppressing analysis when the game is silent on older OSes. Useful, complementary, but orthogonal to ProcessAudioSource. Track separately.

## References

- ADR-0001. Clean-room v2 engine; sets toolchain freedom.
- ADR-0002. Pipeline architecture; Source is the topmost layer.
- ADR-0003. Adapter usage policy; `IAudioSource` is one of the three interfaces.
- ADR-0007. V1 scope; `WasapiLoopbackSource` is in, advanced sources were deferred.
- [research-notes-process-audio.md](research-notes-process-audio.md). Full research notes.
- [PROCESS_LOOPBACK_MODE](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-process_loopback_mode)
- [AUDIOCLIENT_ACTIVATION_PARAMS](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_activation_params)
- [ActivateAudioInterfaceAsync](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync)
- [ApplicationLoopback sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [OBS PR #5218](https://github.com/obsproject/obs-studio/pull/5218)
- [bozbez/win-capture-audio](https://github.com/bozbez/win-capture-audio)
