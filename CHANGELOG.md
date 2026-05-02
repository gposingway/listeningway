# Changelog

All notable changes to Listeningway will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0-beta.1] — 2026-05-02

Major version. Clean-room rebuild of the engine; v1 behavior is preserved
at the shader-uniform layer. Architectural decisions are captured in the
ADRs under [`docs/adr/`](docs/adr/).

### Engine

- **Clean-room rewrite** of audio capture, DSP, and uniform publication.
  v1 source tree retired; v2 is built around a five-layer pipeline:
  `IAudioSource → FrameRing → DSP Pipeline → AudioSnapshot → Consumers`
  (ADR-0002).
- **Adapter pattern at three points only** — `IAudioSource`, `IDspStage`,
  and the beat-detector strategy (ADR-0003). Everything else is a concrete
  type.
- **State machine for capture lifecycle** — `Off → Starting → Running →
  Stopping → Off | Error`. Eliminates the v1 atomic-flag race that froze
  visuals on provider switch.
- **Lock-free SPSC ring** between capture and DSP threads
  (`moodycamel::ReaderWriterQueue`).
- **Seqlock snapshot** publication of an immutable POD `AudioSnapshot`;
  consumers read without locks.

### Audio sources

- **`WasapiLoopbackSource`** (default) — system loopback, with the v1
  pinning fixes (`AUTOCONVERTPCM`, `MMCSS L"Audio"`, polling cadence
  driven by `GetDevicePeriod`).
- **`ProcessAudioSource`** — per-process loopback via
  `ActivateAudioInterfaceAsync` + `PROCESS_LOOPBACK_MODE`. Captures the
  game's audio only — Discord, browser tabs, system notifications no
  longer bleed into the visualization. Available on Windows 10 22H2
  (build 20348+) and Windows 11. See [ADR-0009](docs/adr/0009-process-audio-source.md).
- **`OffSource`** — explicit "no analysis" provider; zeros the snapshot.

### Configuration

- **`Settings` struct + `Setting<T>` declarative bounds** — single
  source of truth for default / min / max / persistence key / tooltip
  per tunable (ADR-0004).
- **`nlohmann::json` round-tripping** via intrusive macros — replaces
  the v1 hand-rolled parser that silently dropped `frequency.minFreq` /
  `maxFreq`.
- **Atomic version-counter hot-reload** — DSP thread sees overlay
  changes without restart.

### Shader uniforms

All v1 uniform names preserved (ADR-0005). New additions in v2:

- **AGC normalization** — `volume_norm`, `bass_norm`, `mid_norm`,
  `treb_norm`, plus `*_att` smoothed siblings.
- **Pre-binned band reductions** — `freqbands16`, `freqbands32`.
- **Spectral centroid** — `spectral_centroid` (brightness input).
- **K-weighted loudness** — `loudness` (BS.1770 momentary, linear).
- **Tempo + PLL phase** — `tempo_bpm`, `tempo_confidence`, `beat_phase`.
- **Chronotensity phases** — `phase_volume`, `phase_bass`, `phase_treble`.
  Robust energy-accumulator alternatives to `beat_phase` when tempo
  isn't locked.
- **History sources** — `volume_history[64]`, `freqbands_history[N×64]`
  (band-major, time-ascending) for waterfall / spectrogram shaders.

See [STABILITY.md](STABILITY.md) for the full registry and stability
classification.

### Overlay

- Redesigned with collapsed-mode visuals + expanded-mode settings.
- Per-stage DSP profiler (EMA-smoothed wall-clock per stage and total
  pipeline µs).
- All settings reachable from the overlay; changes persist atomically.

### Testing

- 19 GoogleTest unit tests covering audit-found bugs (Phase 3).
- Property-based testing infrastructure via rapidcheck (ADR-0006);
  full property coverage rolls in across v2.x.

### Removed

- v1 hand-rolled JSON parser.
- `ThreadSafetyManager` / `AudioCaptureManager` / global graph state.
- v1 SSE3 SIMD path in FFT magnitude (the `simdEnabled` toggle is gone;
  v2 is scalar by default. xsimd path is on the v1.5 roadmap if the
  per-stage profiler shows demand).
- Process-specific capture provider scaffolding from v1 (replaced by
  the new clean-room `ProcessAudioSource`).

### Compatibility

- **Shader uniform names**: byte-compatible with v1. Existing shaders
  continue to work; new uniforms are additive.
- **Configuration file format**: schema reset. v1 `Listeningway.json`
  is **not** migrated; v2 writes a fresh file with v2 defaults on first
  run. Re-save your tuning from the overlay.

---

## [1.2.0.4] — pre-v2 (rolled into 2.0.0-beta.1)

Final v1 release. Subsequent v1 work was merged into the v2 rebuild.

### Added (v1.2.0.4)
- `Listeningway_NumBands` uniform — live band count.
- `Listeningway_Direction8` and FRBL aliases — directional intensity rose.
- Split Amplifier controls (Volume / Bands / Direction).
- Runtime SIMD-toggle (since removed in v2).
- SPSC ring buffer between capture and analysis (since replaced by
  moodycamel in v2).
- Background TempoWorker for BPM (since replaced by the v2 autocorrelation
  + PLL beat detector).
- Beat Profiles with a Custom option.

### Fixed (v1.2.0.4)
- Provider-switch freezes (System → Off → System).
- Default provider selection now honors `is_default`.

## [1.2.0.3] — 2025-06-07

### Refactor
- Restructured audio and provider modules.
- Removed process-specific audio capture providers (re-introduced as
  `ProcessAudioSource` in v2.0).
- Renamed and clarified audio capture provider classes.
- Introduced `AudioCaptureManager` (since retired in v2).

### Bug fixes
- Refined logarithmic gain in band calculations.
- Pan calculation: deadzone + surround channel handling.

## [1.2.0.2] — 2025-06-05

- Centralized configuration; live application of changes to capture and
  analysis.
- Configuration persistence switched to JSON (v1 hand-rolled parser).
- Audio overlay shader redesigned.

## [1.2.0.0] — 2025-06-04

- Amplifier slider for overlay and uniforms.

## [1.1.0] — 2025-06-02

- Stereo spatialization uniforms (`VolumeLeft`, `VolumeRight`, `AudioPan`).
- Audio format detection uniform.
- Pan smoothing setting.

## [1.1.0.2] — 2025-06-04

- Build system enforces ReShade v6.3.3 / ImGui docking match.

## [1.0.x]

Initial public releases. Real-time FFT, beat detection, 32-band frequency
analysis, 5-band equalizer, time uniforms, settings + overlay UI.
