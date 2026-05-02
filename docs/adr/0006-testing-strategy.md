# ADR-0006: Testing strategy — property-based, FileSource replay, no parity baseline

## Status

Accepted — 2026-05-01

## Context

v1 has 19 unit tests added late in the refactor (Phase 3 of the audit work). They cover the configuration round-trip, beat profile presets, audio format utilities, and channel layout — useful but narrow. The DSP core itself (FFT, bands, beat detection, pan, directional) is entirely untested because it lives inside the 1,000-line god-procedure (`AnalyzeAudioBuffer`) and isn't structurally testable.

Two consequences fell out of that:
1. The recent SIMD-toggle bug existed because no test exercised the code path with `cfg_simd = false`. The test would have been one assertion, but the structure made it impossible to write.
2. The provider-switching bug was lifecycle-coordination across atomic flags, with no place to plug a test in.

ADR-0001 commits to a beta release with no behavior-parity guarantee against v1. This means we don't have a "golden recording" to test against. Instead, the test strategy must establish *correctness* and *invariants* on its own merits.

The pipeline architecture (ADR-0002) and the adapter policy (ADR-0003) make tests structurally feasible: each `IDspStage` is a pure transformation of `AnalysisFrame` against `Settings`; each `IAudioSource` is a small lifecycle with a sink callback. Both shapes accept synthetic input and produce checkable output.

## Decision

The test strategy has four layers, in order from cheapest to most expensive.

### 1. Unit tests for value types and pure helpers (fast, exhaustive)

Every `Setting<T>::clamp` invariant, every `Settings` round-trip (default → save → load → equal), every channel-layout heuristic, every PCM unpacking helper, every spectral feature formula gets a unit test. These are gtest cases that run in milliseconds, exhaustive across boundary values, and re-run on every commit.

### 2. Property-based tests for DSP stages (catch invariant violations)

Each `IDspStage` gets property tests that exercise it across the input space, asserting invariants the stage must maintain. This is the cornerstone of the strategy — it lets us catch whole bug *classes* without a behavior baseline.

Examples of the invariants we'll pin:

| Stage | Invariants |
|---|---|
| `VolumeStage` | Output is in [0, 1] for any input bounded by [-1, 1]. Output scales linearly with input gain (within ε). Output is 0 for zero input. |
| `FftStage` | Output magnitudes are non-negative. Output length is `fft_size / 2`. Magnitudes for zero input are zero. Parseval's theorem: input energy ≈ output energy. |
| `BandsStage` | All bands non-negative. Sum of band energies is bounded by total magnitude energy. For a pure sine at frequency f, the band containing f has nonzero energy and adjacent bands have ≤ ε leakage. |
| `EqualizerStage` | Identity-EQ (all bands at 1.0) leaves bands unchanged. Output bands = input bands × Gaussian-weighted EQ multiplier. |
| `LogBoostStage` | Higher-index bands receive higher gain. Zero log_strength is identity. |
| `FluxStage` | Flux is non-negative (half-wave rectified). Flux is 0 when current and previous magnitudes are equal. Per-band flux sums to global flux. |
| `BeatStage` | Output beat is in [0, 1]. With no flux, beat decays monotonically. Detector state is reset cleanly across `start()` / `stop()`. |
| `PanStage` | \|pan\| ≤ 1.0 for any input. Pan = 0 for mono or balanced stereo. Pan → -1 for pure-left content. Pan → +1 for pure-right content. Within the deadzone, pan = exactly 0. |
| `DirectionalStage` | Each direction component non-negative. Sum bounded by total energy. For a 7.1 input with energy only in `front`, direction8[0] dominates. |
| `SpectralCentroidStage` | Output in [0, 1] (normalized to Nyquist). Centroid for low-frequency-only content < centroid for high-frequency-only content. Centroid = 0 for silence. |
| `LoudnessStage` (K-weighted) | Output ≥ 0. Identical RMS in / RMS out within the K-weighting curve. Updates at expected rate (≥ 10 Hz). |

Property test framework: **`rapidcheck`** (header-only C++ port of QuickCheck). Cheap to integrate with gtest. Generators for `AnalysisFrame`, `Settings`, audio buffers (random, sine, click train, white noise) live in a `tests/generators.h`.

### 3. Replay tests via `FileSource` (end-to-end determinism)

A small set of curated input WAV files lives in `tests/data/`:

- `silence_5s.wav` — assert all outputs zero or at-rest.
- `sine_440hz_3s.wav` — assert bands peak in the band containing 440 Hz; pan = 0; beat absent.
- `sine_left_3s.wav` — pan = -1.0.
- `click_train_120bpm_10s.wav` — assert tempo locks at 120 ± 1 BPM, confidence rises, `beat_phase` advances correctly.
- `pink_noise_5s.wav` — assert spectral centroid stable, AGC normalization converges to 1.0 over the window.
- `genre_*` — a few representative musical genres for smoke testing the full pipeline.

`FileSource` reads the WAV at real-time pace (or accelerated for offline tests) and pushes frames through the live pipeline. The test assertion runs against the published `AudioSnapshot` after a settling period.

These tests are the v2 equivalent of "open a game, play music, watch the bars" — but headless, deterministic, and runnable in CI. They're the safety net that catches any regression that property tests miss.

### 4. `SignalGeneratorSource` for synthetic determinism

For tests that need exact reproducibility (e.g. unit-testing `FluxStage` requires a known sequence of magnitudes), `SignalGeneratorSource` synthesizes the input deterministically. No file I/O, no float-precision drift between machines. Used for the property tests in layer 2 and for any unit test that needs known-input known-output exactness.

### What we don't do: golden output parity against v1

ADR-0001 commits to allowing v2 to surpass v1's behavior (better tempo tracker, K-weighted loudness, AGC normalization). A test that asserts `v2_snapshot ≈ v1_snapshot within ε` would fight that mandate. We trust the property tests + replay tests to cover *correctness*, and the beta period to cover *perceptual quality*.

If a v2 algorithm regresses something v1 got right (e.g. a beat that v1 detected and v2 misses), the failure mode is "beta user reports the regression" + "we tune the algorithm." Not "test goes red."

## Coverage targets

- **Unit + property tests:** 80% line coverage on `src/audio/dsp/`, `src/audio/source/` (excluding WASAPI-bound code), `src/config/`, `src/output/`. Measured via gcov/lcov in CI.
- **Replay tests:** every curated WAV file passes its assertion suite.
- **Manual:** WASAPI-bound code (the actual `WasapiLoopbackSource` driver) is not covered by automated tests. It's exercised manually during the beta period via shader-author feedback and `TeeSource` recordings of bug reports.

## Test layout

```
tests/
├── CMakeLists.txt
├── generators.h              # rapidcheck generators for AnalysisFrame, Settings, buffers
├── data/
│   ├── silence_5s.wav
│   ├── sine_440hz_3s.wav
│   ├── click_train_120bpm_10s.wav
│   └── ...
├── stages/
│   ├── test_volume_stage.cpp
│   ├── test_fft_stage.cpp
│   ├── test_bands_stage.cpp
│   ├── test_pan_stage.cpp
│   └── ...                   # one file per stage; unit + property tests
├── sources/
│   ├── test_file_source.cpp
│   ├── test_signal_generator_source.cpp
│   ├── test_off_source.cpp
│   └── test_tee_source.cpp
├── snapshot/
│   └── test_seqlock.cpp      # writer/reader concurrency tests
├── config/
│   ├── test_settings_roundtrip.cpp
│   └── test_validators.cpp
├── replay/
│   ├── test_silence.cpp
│   ├── test_sine_440.cpp
│   ├── test_click_train_120bpm.cpp
│   └── test_pink_noise.cpp
└── integration/
    └── test_pipeline_e2e.cpp # full Source → Ring → Pipeline → Snapshot
```

## Discipline rules

1. **A new `IDspStage` cannot land without at least one property test.** Code review enforces this. The property test pins the invariants the stage commits to.
2. **A bug fix lands with a regression test that fails before the fix.** The v1 SIMD bug and the v1 logStrength bug both qualify; both will have a regression test in the v2 suite.
3. **Replay test data is reviewed and version-controlled.** WAV files in `tests/data/` are committed; their assertions are documented in adjacent `.md` files explaining what each test verifies.
4. **Tests that exercise `AudioSystem` end-to-end use `FileSource` or `SignalGeneratorSource`.** Never `WasapiLoopbackSource` in CI — that path is non-deterministic and machine-dependent.

## Consequences

### Positive

- **Correctness without baseline.** Property tests pin invariants that hold regardless of how v1 behaved.
- **Headless + deterministic + CI-runnable** for the entire DSP pipeline. `FileSource` makes the audio-reactive addon testable like any other library.
- **Bug-class coverage.** Property tests find edge cases unit tests miss. The v1 SIMD bug would have been caught in seconds by a property test on `FftStage`.
- **Replay-driven bug reports.** Beta users record audio with `TeeSource`, attach to bug reports; we replay through `FileSource` and observe the same failure.

### Negative

- **rapidcheck is a new dependency.** Header-only, BSD, well-maintained. Acceptable.
- **WAV test data adds ~few MB to the repository.** Curated files are short (3–10 s each); total is small. Acceptable.
- **Property tests can hide deterministic bugs.** A property test that passes on 100 random samples might miss the one input that triggers the bug. Mitigation: deterministic unit tests for known-tricky cases sit alongside property tests, not instead of them.

### Neutral

- **Coverage of WASAPI-bound code is manual + beta.** This is the right tradeoff — automating WASAPI testing would require complex device-mocking with low return on investment.

## Alternatives considered

### Golden-recording parity test against v1
Capture v1 outputs, assert v2 matches within ε. **Rejected** per ADR-0001's beta scope.

### Mock the entire audio pipeline for tests
Build `MockAudioSource`, `MockPipeline`, `MockSnapshot`, etc. **Rejected** — `FileSource` and `SignalGeneratorSource` *are* the test doubles. They're real implementations of `IAudioSource`, used in production for replay/test; they don't need to be mocked again.

### Snapshot testing (record stage outputs once, assert no change)
Standard pattern in some test ecosystems. **Rejected** for the same reason as golden-recording: it pins behavior without justifying it. A snapshot test passes whether the algorithm is correct or just consistent.

### Skip property tests, rely only on unit + replay
**Rejected.** Property tests catch the invariant violations unit tests routinely miss. The v1 SIMD bug demonstrates this: any unit test we'd have written would have happened to use SIMD-on inputs; a property test exploring the input space would have hit cfg_simd=false within a handful of iterations.

### TDD (write tests before stages)
**Adopted partially.** New stages are written with their property test in the same change. We don't insist on test-first ordering — both halves land together.

## References

- ADR-0001 — beta scope (no parity baseline).
- ADR-0002 — pipeline structure that makes per-stage testing feasible.
- ADR-0003 — adapter policy that gives `IAudioSource` multiple test-friendly implementations.
- v1 bugs the strategy would have caught: SIMD-toggle freeze (covered by `FftStage` property test); logStrength drift (covered by `Settings` round-trip test); provider-switch freeze (covered by `AudioSystem` lifecycle integration test against `FileSource` + state machine assertions).
