# ADR-0007: v1 scope and detection-technique choices

## Status

Accepted, 2026-05-01

## Context

ADR-0001 commits to a clean-room v2 engine; ADR-0002 through ADR-0006 fix the architecture, the configuration model, the uniform contract, and the testing strategy. This ADR locks the scope of the first beta release (v2.0.0-beta.1): what's in, what's deferred, and which specific algorithms or techniques the implementation commits to for each detection feature.

The rationale for a hard scope lock is the discipline rule from the project conversation: "while we're here" features are accepted, but only if they fit the same property-test-before-feature gate that everything else does. This ADR is the list of features that pass that gate; everything else moves to v1.5 or later.

The detection technique choices here are drawn directly from the MIR research ([research-notes.md](research-notes.md) §1), the WASAPI research (§2), the LUFS research (§5), and the visualizer research (§4). Citations and rationale live there; this ADR pins the specific choices.

## Decision: v1 in-scope

### Capture sources

| Source | Status | Notes |
|---|---|---|
| `WasapiLoopbackSource` | In v1 | Polling timer (event mode is broken for loopback per [research-notes.md](research-notes.md) §2). `AUTOCONVERTPCM` to float-stereo 48 kHz. MMCSS `L"Audio"`. |
| `OffSource` | In v1 | No-op; `start()` returns true, sink never called. |
| `FileSource` | In v1 | WAV reader, real-time pace; foundation of replay tests (ADR-0006). |
| `SignalGeneratorSource` | In v1 | Synthetic sine / click / white-noise. Tests only. |
| `TeeSource` | In v1 | Decorator that wraps any source, writes raw frames to a `.wav` while passing through. User-triggered "record this session for bug reports." |
| `ProcessAudioSource` | In v1, opt-in | Windows 10 build 20348+; format must be hardcoded; opt-in via setting, never default. |

### DSP stages

The pipeline (ADR-0002) is composed of these stages in order. Each ships in v1 with property tests (ADR-0006).

| Stage | Reads from `AnalysisFrame` | Writes to `AnalysisFrame` |
|---|---|---|
| `VolumeStage` | `samples`, `format` | `volume`, `volume_left`, `volume_right`, `volume_norm`, `volume_att` |
| `FftStage` | `samples` | `magnitudes` (size = `fft_size / 2`) |
| `BandsStage` (Slaney mel + linear + log options; default Slaney mel) | `magnitudes`, `format` | `raw_bands`, `bands_16`, `bands_32` (derived by summing) |
| `LogBoostStage` (optional) | `raw_bands` | `raw_bands` (in-place gain curve) |
| `EqualizerStage` (5-band Gaussian-weighted) | `raw_bands` | `bands` |
| `BandNormStage` | `bands`, history | `bass_norm`, `mid_norm`, `treb_norm`, `bass_att`, `mid_att`, `treb_att` |
| `FluxStage` (per-band log-flux) | `magnitudes`, prev state | `flux_total`, `flux_low`, `flux_mid`, `flux_high` |
| `BeatStage` (uses `IBeatDetector`) | `flux_*`, dt | `beat`, `tempo_bpm`, `tempo_confidence`, `beat_phase`, `tempo_detected` |
| `ChronotensityStage` | `volume_norm`, `bass_norm`, `treb_norm`, dt | `phase_volume`, `phase_bass`, `phase_treble` |
| `PanStage` | `samples`, `format` | `pan` (with deadzone, silence threshold, significant threshold preserved from v1) |
| `DirectionalStage` | `samples`, `format`, `channel_layout` | `direction8` |
| `SpectralCentroidStage` | `magnitudes` | `spectral_centroid` (normalized to Nyquist) |
| `LoudnessStage` (K-weighted, BS.1770) | `samples`, `format` | `loudness` (400 ms momentary RMS) |
| `HistoryStage` | `volume` | `volume_history` (ring of 64) |

### Detection techniques (specific choices)

#### Tempo tracking
- **Algorithm:** onset-envelope autocorrelation, ~8-second sliding window, log-Gaussian tempo prior centered at 120 BPM with σ ≈ 0.7 octaves.
- **Implementation:** streaming comb-filter or Davies' causal method (aubio `tempo.c` style).
- **Confidence:** peak-to-second-peak ratio of the autocorrelation, EMA-smoothed (α ≈ 0.1).
- **Citation:** [research-notes.md §1](research-notes.md). Specifically aubio `tempo.c`, BTrack, librosa `beat.py`, Ellis 2007.

#### Onset detection
- **Algorithm:** spectral flux per band on log-magnitude mel spectrogram. Half-wave rectified log-energy difference per band.
- **Bands:** kick 20–150 Hz, snare 150–2000 Hz, hi-hat 6000–16000 Hz. (These are *internal* bands feeding the tempo tracker; not exposed as named uniforms. See ADR-0005.)
- **Citation:** [research-notes.md §1](research-notes.md). Specifically Battenberg drum thesis, Bello tutorial, Essentia `OnsetDetection`.

#### Adaptive threshold
- **Formula:** `threshold = median(window) + 0.1 * mean(window)` where window = 5 past + 1 future samples (~60 ms at 100 Hz frame rate).
- **Refractory period:** 50 ms after a detected onset.
- **Pre-smoothing:** 2nd-order Butterworth low-pass (cutoff ~0.34 normalized) on the onset envelope; bidirectional if latency budget permits, else forward-only.
- **Citation:** [research-notes.md §1](research-notes.md). aubio `peakpicker.c`, Brossier 2004.

#### Beat phase prediction
- **Algorithm:** PLL-style soft phase correction on confident detected onsets.
- **Constants:** `k_p = 0.15` (phase pull on detected onset), `k_i = 0.01` (BPM drift correction). Gate corrections by onset confidence to avoid being yanked by noise.
- **Output:** `beat_phase ∈ [0, 1)` continuously advancing at the detected tempo.
- **Citation:** [research-notes.md §1](research-notes.md). OBTAIN paper, Cemgil/Large oscillator model, BTrack.

#### Spectral centroid
- **Formula:** `Σ(f[k] · |X[k]|) / Σ|X[k]|`, divided by Nyquist for [0, 1] uniform range.
- **Numerical guard:** add ε = 1e-10 before division to handle silence.
- **Hann window:** required upstream; rectangular leaks energy.

#### Mel-scale band mapping
- **Convention:** Slaney (linear below 1 kHz, log above) with `norm='slaney'` filter normalization.
- **Default band count:** 64 (preserved from v1).
- **Derived counts:** `bands_16 = sum(bands[i*4 : (i+1)*4])` and similar; cheap re-binning.
- **Citation:** [research-notes.md §1](research-notes.md). librosa.

#### Loudness (K-weighting)
- **Algorithm:** ITU-R BS.1770 K-weighting (two cascaded biquads per channel) + 400 ms momentary RMS.
- **Coefficients (48 kHz):** documented in [research-notes.md §5](research-notes.md). Derived per-rate via bilinear transform of the spec's filter (shelf at 1681.97 Hz / G=4 dB / Q=0.707, HPF at 38.14 Hz / Q=0.5).
- **Output:** `sqrt(mean_square)` (linear, [0, 1]); skip the LUFS log conversion for the uniform.
- **Citation:** [research-notes.md §5](research-notes.md). ITU-R BS.1770-5; libebur128.

#### AGC normalization
- **Algorithm:** instantaneous value ÷ exponential moving mean over a 5-second window. Clamped to a sensible range (e.g. [0, 4]) to bound shader-side effects.
- **Time constant:** start at 5 s; expose as a setting for tuning.

#### Chronotensity phase accumulators
- **Algorithm:** accumulator advances at `1.0 + k * (band_norm - 1.0)` Hz baseline, modulo 1.0. Effectively, when band is at average loudness it advances at 1 Hz; louder accelerates, quieter decelerates.
- **Constant:** `k ≈ 0.5`; expose as a setting.
- **Citation:** [research-notes.md §4](research-notes.md). AudioLink documentation.

### Configuration

`Setting<T>` declarations + auto-marshalled JSON via `nlohmann::json` macros (ADR-0004). No migration framework in v1. `schema_version` field present from day one.

### Output

The full uniform set from ADR-0005. 22 preserved + ~15 new. Backed by the centralized shader contract header. `STABILITY.md` shipped alongside.

### Lifecycle

State machine in `AudioSystem`: `Off → Starting → Running → Stopping → Off | Error`. Provider switching is an explicit transition through `Off`. Worker thread handles device-change rebuild with 200–500 ms settle delay.

## Decision: v1.5 / deferred

These items are designed-in but deferred to allow v1 to ship at the agreed timeline. None of them require architectural change.

| Deferred item | Why deferred | Trigger to add |
|---|---|---|
| Per-band history uniforms (`freqbands_history[N]`) | Cost: 64 × N floats per snapshot. Volume history alone delivers 80% of the value. | Shader-author request; first preset that genuinely needs band waterfall. |
| Compile-time pipeline composition (`std::tuple<...>` + `std::apply`) | Optimization, ~10% perf headroom. Current dynamic pipeline is plenty. | Profiler shows pipeline driver as a hot path. |
| `xsimd` or `std::simd` for portable SIMD | Cleanup of hand-written intrinsics. Current SSE3 hand-write is fine. | When MSVC ships `std::simd` or when we add ARM64 support. |
| ReShade buffer/SRV bridge for high-bandwidth analysis | Designed-in; needed only for very-high-resolution shaders. | Shader-author request for raw FFT or full waveform. |
| Spectral rolloff, spectral flatness uniforms | Real shader demand is concentrated on centroid; rolloff/flatness are MIR research territory. | Demand-driven. |
| Stage-level UI to toggle stages on/off | Useful for performance work but requires UI design. | After main panel UI lands. |
| Hot-reload of `Listeningway.json` from disk | The overlay's "Save" button is sufficient for v1 tuning. | Shader-author request; useful for live preset development. |
| Settings migration framework | v1 → v2 is a beta with no migration; v2 → v2.x changes will need it. | First v2.x schema change. |

## Decision: explicitly out of scope

Things considered and rejected for v1:

- DDD entities, aggregates, and repositories. Not a domain-rich app (ADR-0001).
- A plugin system or scripting layer. Two sources is the universe.
- Async or coroutines. Threads plus a ring buffer is the right tool.
- Cross-platform support. Windows and WASAPI only; ReShade itself is Windows-focused, so cross-platform doesn't pay.
- A DSP profiler GUI. Useful but not required for shipping. Deferred.
- Configurable pipeline via JSON list of stage names. Designed-in but adds startup complexity; the C++ wiring in `DllMain` is simple enough.

## Sequencing (build plan)

| Day | Work | Verifies |
|---|---|---|
| 0 (1 hr) | `git mv src src.harvest`. Empty `src/`. CMake builds an empty stub addon that loads as no-op. | Greenfield workspace ready. |
| 1 | `IAudioSource`, `OffSource`, `FileSource`, `SignalGeneratorSource`. moodycamel `ReaderWriterQueue` vendored. `FrameRing` wrapper. Property tests for sources. | Sources work in isolation. |
| 2 | `WasapiLoopbackSource`: port the WASAPI lambda from harvest as a "value harvest" with the corrections from [research-notes.md §2](research-notes.md): polling timer, MMCSS `Audio`, `AUTOCONVERTPCM`. End-to-end audio flowing capture → ring. | Audio flows. |
| 3 | DSP stages 1: `VolumeStage`, `FftStage`, `BandsStage` (Slaney mel), `EqualizerStage`, `LogBoostStage`, `BandNormStage`, `SpectralCentroidStage`. Property tests + replay tests for sine/silence/pink-noise. | First half of DSP, green. |
| 4 | DSP stages 2: `FluxStage` (per-band), `BeatStage` (autocorrelation tempo + PLL phase + adaptive median threshold), `ChronotensityStage`, `PanStage`, `DirectionalStage`, `LoudnessStage`. Replay test against click-train pins tempo lock. | Full DSP green. |
| 5 | `Setting<T>`, `Settings` struct + intrusive JSON, `Store` with version counter, state machine in `AudioSystem`. Overlay rebuilt against snapshot reads + setting writes. Uniform contract header + auto-generated `UniformPublisher`. Shader contract `STABILITY.md`. | End-to-end working in-game. |
| 6 | `TeeSource`. `ProcessAudioSource` (opt-in attempt; if blocked by API surprises, document and defer). DSP profiler view in overlay (optional). Polish, deploy.bat update. Tag `v2.0.0-beta.1`. Drop `src.harvest/`. | Ship. |
| (+1 buffer) | Slack day for the inevitable "this took longer than planned." | |

## Consequences

### Positive

- **Scope is locked.** Every feature in v1 has a documented rationale; every deferred item has a documented trigger to revisit.
- **Detection techniques are committed and citable.** Reviewers and contributors can verify the choice against the cited research.
- **Build plan is concrete and time-boxed.** 6 days of focused work + 1 day slack.

### Negative

- **No room for spontaneous additions.** Anyone proposing "while we're here, let's also..." should propose a v1.5 ADR instead of inline-adding to v1.

### Neutral

- **Specific algorithm choices may need tuning during beta.** That's expected. Beta is the tuning loop; the algorithms themselves don't change without superseding ADRs.

## References

- ADR-0001 (beta scope).
- ADR-0002 (pipeline architecture).
- ADR-0003 (adapter usage policy).
- ADR-0004 (configuration strategy).
- ADR-0005 (uniform contract).
- ADR-0006 (testing strategy).
- ADR-0008 (language and dependencies).
- [research-notes.md](research-notes.md) holds the full citations for every algorithm choice in this ADR.
