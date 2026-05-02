# Research Notes — Listeningway v2 Engine

This document captures the focused research conducted before drafting the ADRs. Six parallel investigations covered MIR algorithms, WASAPI loopback patterns, real-time audio pipeline architectures, audio-reactive visualizer design conventions, perceptual loudness, and lock-free concurrency. Findings here feed directly into ADR-0001 through ADR-0008.

The research surfaced five points where agent recommendations conflicted; resolutions are recorded inline. Where an ADR diverges from a research recommendation, the rationale is noted.

---

## 1. MIR algorithms

### Tempo tracking
**Decision:** Onset-envelope autocorrelation, ~8-second sliding window, log-Gaussian tempo prior centered at 120 BPM (σ ≈ 0.5–1.0 octaves). Streaming comb-filter or aubio's Davies causal method, *not* Ellis's offline DP.

**Why:** Octave errors (60↔120↔240 BPM) are the dominant failure mode in naive trackers; the log-Gaussian prior biases selection toward musically plausible tempos. Window size matches librosa default. Ellis 2007 is foundational but offline; we need streaming.

**Citations:**
- [Ellis 2007 — Beat Tracking by Dynamic Programming (PDF)](https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf)
- [aubio tempo.c (BSD)](https://github.com/aubio/aubio/blob/master/src/tempo/tempo.c)
- [BTrack — Davies real-time tracker](https://github.com/adamstark/BTrack)
- [librosa beat.py](https://github.com/bmcfee/librosa/blob/main/librosa/beat.py)

### Per-band onset detection
**Decision:** Compute spectral flux *per band* on log-magnitude mel/log spectrogram, peak-pick each band independently. Bands: kick 20–150 Hz, snare 150–2000 Hz, hi-hat 6–16 kHz.

**Why:** Standard MIR practice (Battenberg drum thesis). Half-wave rectified log-energy difference is more perceptually flat than linear flux. Snare bleeds broadly so use wider band; rely on high-band kick suppression.

**Citations:**
- [Battenberg drum thesis (PDF)](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2012/EECS-2012-250.pdf)
- [Bello et al. — Tutorial on Onset Detection](https://www.csd.uoc.gr/~hannover/MMILab-Andre_files/HolzapfelIEEEOnset.pdf)
- [Essentia OnsetDetection](https://essentia.upf.edu/reference/streaming_OnsetDetection.html)

### Adaptive threshold
**Decision:** aubio's exact formula — `threshold = median(window) + 0.1 * mean(window)` over a window of 5 past + 1 future samples (~60 ms at ~100 Hz frame rate). Refractory ~50 ms after detected onset.

**Why:** Battle-tested. Median-based is far more robust than moving-average — moving averages are pulled up by the very onsets they're meant to detect, raising the threshold and causing missed onsets.

**Citations:**
- [aubio peakpicker.c](https://github.com/aubio/aubio/blob/master/src/onset/peakpicker.c)
- [Brossier 2004 — Fast Notes (PDF)](https://aubio.org/articles/brossier04fastnotes.pdf)

### Spectral features
**Decision:** Compute on **magnitude** spectrum (not power) after Hann window. Centroid = `Σ(f[k]·|X[k]|) / Σ|X[k]|`, normalized by Nyquist. Add ε=1e-10 before any log/division for silence robustness.

**Citations:**
- [librosa spectral.py](https://librosa.org/doc/main/_modules/librosa/feature/spectral.html)
- [MATLAB spectral descriptors](https://www.mathworks.com/help/audio/ug/spectral-descriptors.html)

### Mel scale convention
**Decision:** Slaney convention (linear below 1 kHz, log above) with `norm='slaney'` filter normalization.

**Why:** Allocates more bins to bass/low-mid where kick/snare live — exactly the right tradeoff for music visualizers. HTK's pure-log form is for speech recognition (formant resolution); wrong tradeoff for music reactivity.

**Citations:**
- [librosa mel_frequencies](https://librosa.org/doc/main/generated/librosa.mel_frequencies.html)
- [Mel scale — Wikipedia](https://en.wikipedia.org/wiki/Mel_scale)

---

## 2. WASAPI loopback

### Critical correction: event mode is broken for loopback
**Finding:** `AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK` together is broken — the call succeeds, `SetEventHandle` succeeds, but the event never fires for loopback streams. Confirmed in OBS source, cubeb issues, and Microsoft's own samples.

**Decision:** Drive the capture loop with `CreateWaitableTimerEx(... HIGH_RESOLUTION ...)` at half the device period. Loop on `IAudioCaptureClient::GetNextPacketSize` until 0, then wait for timer.

**This contradicts the v1 code's use of event mode for loopback.** The v1 code's "alternating event/polling by attempt counter" was a workaround for this issue without a clean explanation. Document explicitly in v2.

### Format pinning
**Decision:** `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`, request float-stereo 48 kHz `WAVEFORMATEXTENSIBLE` with `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`, `KSAUDIO_SPEAKER_STEREO`.

**Why:** The engine handles resample/downmix for any odd device format. Trade bit-exact capture for robustness. For an audio-reactive visualizer this is unambiguous — we never round-trip the audio, only analyze it.

### Process loopback
**Finding:** Available since Windows 10 build 20348. `IAudioClient::GetMixFormat` and `IsFormatSupported` return `E_NOTIMPL` on the virtual device — must hardcode format. Rapid Start/Stop cycles can crash. Children-process inclusion misses some launchers that re-parent.

**Decision:** `ProcessAudioSource` ships in v1 as **opt-in advanced toggle**, never the default. Default remains system loopback.

### MMCSS thread priority
**Decision:** `AvSetMmThreadCharacteristicsW(L"Audio")`, **not** `L"Pro Audio"`.

**Why:** `Pro Audio` competes with the game's renderer thread on low-core systems. Measured benefit for a non-realtime consumer is single-digit microseconds. Always release with `AvRevertMmThreadCharacteristics` on thread exit.

### Device-change handling
**Finding:** `OnDefaultDeviceChanged` fires three times per change (one per role: Console / Multimedia / Communications). Microsoft's contract: callbacks must be non-blocking, must not call register/unregister from inside, must not wait on sync objects.

**Decision:** In the callback, only `SetEvent` on a "device dirty" flag and return. A separate worker thread does the actual rebuild. Filter to `flow == eRender, role == eMultimedia`. Wait 200–500 ms before re-activating (USB DACs, Bluetooth need settle time).

### Buffer period
**Finding:** Shared loopback hard floor is ~10 ms regardless of `hnsBufferDuration` requested. `IAudioClient3::InitializeSharedAudioStream` (sub-10 ms) **cannot be used with loopback**.

**Decision:** Request `hnsBufferDuration = 0` (engine chooses), poll at `defaultPeriod / 2`. Don't fight the platform; 10–20 ms is exactly where shared loopback lives natively.

### Citations
- [OBS win-wasapi.cpp](https://github.com/obsproject/obs-studio/blob/master/plugins/win-wasapi/win-wasapi.cpp)
- [Loopback Recording — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
- [Matthew van Eerde — WASAPI loopback sample](https://learn.microsoft.com/en-us/archive/blogs/matthew_van_eerde/sample-wasapi-loopback-capture-record-what-you-hear)
- [PROCESS_LOOPBACK_MODE](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-process_loopback_mode)
- [ApplicationLoopbackAudio sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [Mozilla cubeb device-change bug 1134078](https://bugzilla.mozilla.org/show_bug.cgi?id=1134078)
- [miniaudio device-change crash #582](https://github.com/mackron/miniaudio/issues/582)
- [Donya Quick — Minimizing WASAPI Latency](https://www.donyaquick.com/minimizing-audio-latency-on-windows-10-with-wasapi/)

---

## 3. Real-time pipeline architectures

The five-layer design (Source → Ring → DSP → Snapshot → Consumers) **validates against JUCE, VST3, rtaudio, and PortAudio conventions**. Three concrete refinements emerged:

### Refinement 1: Capabilities query
**Decision:** Add a `Capabilities` struct returned by `IAudioSource::open()` carrying sample rate, channels, frame size, reported latency. DSP pipeline and FFT stage size buffers from this rather than guessing.

**Source:** [PortAudio API overview](http://www.portaudio.com/docs/v19-doxydocs/api_overview.html)

### Refinement 2: AnalysisFrame optionality semantics
**Decision:** `std::optional<...>` fields in `AnalysisFrame` represent **"stage was disabled / skipped"**, not "stage was lazy." If a stage runs, it always produces its output.

**Source:** FAUST architecture; awesome-musicdsp.

### Refinement 3: VST3-style sample-accurate parameter automation NOT adopted
**Decision:** A simple `const Settings&` reference passed to each stage is fine; we read whole settings, not interpolated curves. We are an analysis tool, not a synth.

**Source:** [VST3 Parameters & Automation](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Parameters+Automation/Index.html)

### Citations
- [JUCE AudioProcessor docs](https://docs.juce.com/master/classAudioProcessor.html)
- [JUCE AudioProcessorValueTreeState](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html)
- [Ross Bencina — real-time audio 101](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing)
- [Timur Doumler — locks in realtime audio](https://timur.audio/using-locks-in-real-time-audio-processing-safely)

---

## 4. Audio-reactive visualizer design conventions

**This research surfaced the most surprising findings.** The default uniforms a v2 engine should expose differ materially from MIR research recommendations.

### Finding: AudioLink is the de facto standard
[AudioLink (VRChat ecosystem)](https://github.com/llealloo/audiolink) is the most actively coded-against audio-shader contract today. Two of its design decisions invert what we initially planned.

### Finding: Normalized-to-running-average is the missing pattern
**MilkDrop / projectM / Butterchurn all expose `bass / running_mean(bass)`** (a value where 1.0 = "normal," >1.3 = "loud") rather than raw values. Preset authors strongly prefer this AGC-relative form because it works across loud/quiet sources without per-preset tuning. Listeningway v1 exposes only raw — this is the single biggest gap.

**Decision (v2 addition):** `listeningway_volume_norm`, `_bass_norm`, `_mid_norm`, `_treb_norm` (instant ÷ 5–10 second running mean). Plus `_att` smoothed siblings.

### Finding: AudioLink rejects BPM-locked phase, uses chronotensity instead
**AudioLink does not expose explicit BPM or beat-phase [0,1).** The authors found exposing BPM is brittle (estimation lag, octave errors, genre dependence) while *chronotensity* — accumulated per-band energy modulo 1.0 — is robust and gives shaders tempo-locked motion without `_Time.y`.

**Decision (v2 addition):** Add `listeningway_phase_volume`, `_phase_bass`, `_phase_treble` chronotensity-style accumulators. **Plus** the MIR-style `listeningway_beat_phase` from autocorrelation (works when tempo is confidently locked). Authors choose per use case.

### Finding: Per-band beats as named uniforms — skip
Despite the MIR research recommending per-band onset detection (which we *do* need internally for tempo tracking), no major visualizer system exposes kick/snare/hat as named uniforms. Shader authors derive these from `freqbands`.

**Decision:** Compute per-band onsets internally for the tempo tracker. Expose them only via per-band history arrays (cheap, more flexible) — not as named uniforms.

### Finding: Multiple band counts is a cheap win
[Wallpaper Engine](https://docs.wallpaperengine.io/en/scene/shader/variables.html) exposes `g_AudioSpectrum16/32/64Left[]` variants. Many shaders want 8 or 16 bins for bar visualizers without aliasing 64 down.

**Decision (v2 addition):** `listeningway_freqbands16[16]`, `_freqbands32[32]` derived by summing the 64-bin array.

### Finding: Spectral centroid yes, rolloff/flatness no
Only spectral centroid has shader-author demand (color-temperature / brightness modulation).

**Decision:** Add `listeningway_spectral_centroid`. Skip rolloff and flatness for v1; reconsider if requested.

### Finding: History buffer is highly requested
AudioLink's most-praised feature is per-band history (128 frames). Enables waterfall/trail effects without shader-side ring buffers.

**Decision (v2 addition):** `listeningway_volume_history[64]` (one ring-buffered row). Per-band histories deferred to v1.5 (cost N×bands floats).

### Citations
- [MilkDrop preset authoring (Geisswerks)](https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html)
- [projectM](https://github.com/projectM-visualizer/projectm)
- [butterchurn](https://github.com/jberg/butterchurn)
- [AudioLink (llealloo)](https://github.com/llealloo/audiolink)
- [AudioLink chronotensity & beat detection](https://deepwiki.com/llealloo/audiolink/6.3-chronotensity-and-beat-detection)
- [Wallpaper Engine shader variables](https://docs.wallpaperengine.io/en/scene/shader/variables.html)
- [AS-StageFX (Listeningway consumer)](https://github.com/LeonAquitaine/as-stagefx)

---

## 5. K-weighting / LUFS

### Finding: Full LUFS is overkill; AGC matters more
The LUFS research recommended adding a K-weighted 400 ms momentary loudness uniform. The visualizer research independently surfaced that **AGC normalization** (running-mean-relative) is what shader authors actually want — LUFS is for broadcast compliance, not music reactivity.

### Resolution: Both, separated by purpose
- `listeningway_volume_norm` — **AGC-normalized** raw RMS volume ÷ 5–10 second running mean. **Primary** uniform for shader authors. Solves "preset works at any source volume."
- `listeningway_loudness` — **K-weighted** 400 ms momentary RMS. Optional, perceptually meaningful slow envelope. Cost: two biquads per channel + sliding-window sum.

Both are cheap (negligible vs FFT). They serve different shader needs and don't conflict.

### K-weighting biquad coefficients (48 kHz, Direct Form I)
```
Stage 1 (high-shelf pre-filter):
  b0 =  1.53512485958697   b1 = -2.69169618940638   b2 =  1.19839281085285
  a1 = -1.69065929318241   a2 =  0.73248077421585

Stage 2 (RLB high-pass):
  b0 =  1.0                b1 = -2.0                b2 =  1.0
  a1 = -1.99004745483398   a2 =  0.99007225036621
```

For other sample rates: shelf at f0=1681.9744 Hz, G=3.99984 dB, Q=0.70717; HPF at f0=38.13547 Hz, Q=0.50033; bilinear-transform with `K = tan(π·f0/fs)`. Listeningway captures whatever WASAPI returns, so coefficients must be derived per-rate, not hardcoded.

### Citations
- [ITU-R BS.1770-5 (Nov 2023, PDF)](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf)
- [libebur128 reference impl](https://github.com/jiixyj/libebur128)
- [JUCE forum — BS.1770 IIR coefficients](https://forum.juce.com/t/itu-r-bs-1770-loudness-measurement-iir-filter-coefficients/59167)

---

## 6. Lock-free concurrency

Two tensions emerged between the lock-free agent's recommendations and the pipeline-architecture agent's recommendations.

### Tension 1 — SPSC ring: hand-rolled vs moodycamel ReaderWriterQueue

| Option | Pros | Cons |
|---|---|---|
| Hand-rolled Lamport SPSC (~80 LOC) | No dependency; full control; documented memory orderings | We own the correctness proof; risk of subtle MSVC codegen issues |
| moodycamel `ReaderWriterQueue` | Battle-tested; tens of thousands of users; header-only BSD | One header dependency; one external library to vendor |

**Decision:** **moodycamel `ReaderWriterQueue`.** Risk reduction wins. We are MSVC/x64-only; moodycamel has been stress-tested across far more environments than we'll ever validate ourselves. The cost is one vendored header; the benefit is "memory ordering correctness is somebody else's problem."

The hand-rolled pattern remains documented (in this notes file) as a fallback if moodycamel ever becomes unavailable.

### Tension 2 — Snapshot publication: seqlock vs triple-buffer

| Option | Pros | Cons |
|---|---|---|
| Seqlock | Natural fit for one-writer-N-readers; zero allocation; small POD payload; simple counter logic | Reader can technically race the body; mitigate with `memcpy`; payload must be POD (no `std::vector`) |
| Triple buffer with atomic exchange | Wait-free for both sides; classic audio idiom | N concurrent readers complicate slot accounting; needs reader-side reservation |

**Decision:** **Seqlock with `memcpy` body copy.** Payload is small (~few KB) POD. Readers retry on contention with up to 4 spin attempts before falling back to last good copy. Reader semantics ("get a recent consistent value") match the seqlock contract.

This requires the snapshot to be POD: replace `std::vector<float> freq_bands` with `std::array<float, kMaxBands> freq_bands; uint32_t freq_band_count;`. `kMaxBands = 128` (covers any reasonable configuration).

### SPSC ring memory orderings (canonical Lamport pattern, even if we use moodycamel)
```
Producer (capture thread):
  read own writeIdx: relaxed
  read consumer readIdx: acquire   ← pairs with consumer's release
  write data
  publish writeIdx: release

Consumer (DSP thread):
  read own readIdx: relaxed
  read producer writeIdx: acquire   ← pairs with producer's release
  read data
  publish readIdx: release
```

`alignas(64)` on each index to prevent false sharing. Capacity must be power-of-two for cheap mask-based modulo. No `atomic_thread_fence` needed beyond the per-atomic orderings on x64 TSO.

### Seqlock writer/reader (if rolling our own)
```
Writer:
  s = seq.load(relaxed)
  seq.store(s+1, relaxed)              ← mark odd = writing
  atomic_thread_fence(release)         ← odd visible before payload
  memcpy(&data, &snap, sizeof(snap))   ← non-atomic body
  atomic_thread_fence(release)         ← payload visible before even
  seq.store(s+2, release)              ← even = stable

Reader:
  for (spin = 0..3):
    s1 = seq.load(acquire)
    if (s1 & 1) continue                 ← writer mid-update
    atomic_thread_fence(acquire)
    memcpy(&out, &data, sizeof(out))
    atomic_thread_fence(acquire)
    s2 = seq.load(acquire)
    if (s1 == s2) return success
  return last_good_copy
```

### Citations
- [moodycamel ReaderWriterQueue](https://github.com/cameron314/readerwriterqueue)
- [Charles Frasch — Lock-free SPSC for Real-Time Audio (CppCon 2023)](https://www.youtube.com/results?search_query=Charles+Frasch+CppCon+2023)
- [Lamport 1983 — original SPSC paper](https://lamport.azurewebsites.net/pubs/buridan.pdf)
- [Hans Boehm — Can Seqlocks Get Along with PL Memory Models (HPL 2012)](https://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf)
- [Linux kernel seqlock](https://en.wikipedia.org/wiki/Seqlock)
- [Ruurd Adema — Triple Buffering](https://r18a.nl/triple-buffering/)
- [1024cores — SPSC analysis](https://www.1024cores.net/home/lock-free-algorithms/queues/unbounded-spsc-queue)

---

## Summary of decisions feeding the ADRs

| Area | Decision | ADR |
|---|---|---|
| Tempo | Onset-envelope autocorrelation, 8 s window, log-Gaussian @120 BPM prior | 0007 |
| Per-band onsets | Internal: kick 20-150, snare 150-2000, hat 6-16k Hz on log-mel | 0007 |
| Threshold | aubio formula `median + 0.1·mean`, 5-past/1-future window | 0007 |
| Beat phase | Both: PLL-locked `beat_phase` (when tempo confident) + chronotensity `phase_*` accumulators (always) | 0005, 0007 |
| Spectral features | Centroid only (skip rolloff, flatness) | 0005, 0007 |
| Mel scale | Slaney with `norm='slaney'` | 0007 |
| WASAPI loopback | Polling timer (NOT event mode) at half device period; `AUTOCONVERTPCM` to float-stereo 48k | 0007 |
| Process loopback | Opt-in advanced; system loopback remains default | 0007 |
| MMCSS | `L"Audio"` not `L"Pro Audio"` | 0007 |
| Device change | Signal-only callback, worker thread rebuild, 200-500 ms settle | 0007 |
| Buffer period | Request 0, poll at half device default period | 0007 |
| Pipeline shape | Source → Ring → DSP stages → Snapshot → Consumers | 0002 |
| Capabilities query | Returned by `IAudioSource::open()` | 0002 |
| `AnalysisFrame` optional | "Stage skipped," not "lazy" | 0002 |
| Adapters | `IAudioSource`, `IDspStage`, `IBeatDetector` only | 0003 |
| Settings | `Setting<T>` declarative bounds, single `Settings` struct, intrusive JSON | 0004 |
| AGC normalization | `_norm` and `_att` siblings for volume/bass/mid/treb (PRIMARY) | 0005 |
| K-weighted loudness | `listeningway_loudness` separate uniform (slower envelope) | 0005, 0007 |
| Multiple band counts | `freqbands16[16]`, `freqbands32[32]` derived | 0005 |
| History uniforms | `volume_history[64]` in v1; per-band history v1.5 | 0005 |
| SPSC ring | moodycamel `ReaderWriterQueue` (single header dep) | 0002, 0008 |
| Snapshot publish | Seqlock with `memcpy`, POD payload, fixed-size arrays | 0002 |
| Snapshot type | `std::array<float, kMaxBands>` + count, no `std::vector` | 0002 |
| Testing | Property-based, FileSource replay, no behavior parity | 0006 |
| Language | C++20, MSVC 2022 | 0008 |
| Deps | kissfft, nlohmann-json, gtest, readerwriterqueue (header) | 0008 |

---

## Open items deferred to implementation time

These decisions don't need to be made at the ADR level but are worth flagging:

- **AGC time constant** — start at 5 s running mean for `_norm` siblings; expose as a setting.
- **Chronotensity rate constants** — start at 1 Hz baseline modulated by band energy; tune by feel.
- **History uniform update rate** — match audio publish rate (~60-200 Hz); shader authors can downsample.
- **Spectral centroid normalization** — divide by Nyquist (sr/2) for [0,1] uniform range.
- **Tempo prior σ** — start at 0.7 octaves; widen if too "pulled toward 120" on actual tracks.
