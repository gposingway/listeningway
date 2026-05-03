# Changelog

All notable changes to Listeningway will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.1] - 2026-05-03

Consolidates the work shipped under the v2.0.0 beta line into a
release-quality cut. Headline changes since 2.0.0-beta.2:

- **Engine rewrite** for proper DSP work — five-layer pipeline (Source
  → SPSC ring → DSP stages → seqlock snapshot → consumers), per-stage
  profiler in the overlay, lock-free reads. (See 2.0.0-beta.3 entry
  for detail.)
- **Beat tracker** replaced with a clean-room implementation of the
  Davies / Plumbley / Stark algorithm chain (CSD-HWR onset → comb-
  filterbank tempo with 41-state Viterbi → cumulative-score with
  forward beat prediction). Tempo confidence reads honestly and locks
  on dance music within seconds. ADR-0013.
- **OpenRGB integration redesigned** around a per-zone-type pattern
  matrix (Single / Linear / Matrix dropdowns, each catalogue ordered
  active → soothing). Includes a Spatial Map for keyboards that
  projects the audio direction rose onto the matrix layout. ADR-0014.
- **Stereo spatial routing fixed** — left audio → L bucket, right
  audio → R bucket (rather than the v1 bias toward FR/FL that
  assumed listener-in-front-of-speakers). Two new tunables:
  `spatial_spread`, `spatial_smoothing`.
- **New shader uniforms**: AGC-normalized `volume_norm`/`bass_norm`/
  `mid_norm`/`treb_norm` (+ smoothed `*_att`), `freqbands16`/`32`,
  `spectral_centroid`, `loudness`, `tempo_bpm`/`confidence`,
  chronotensity phases, history rings. All v1 names preserved.
- **OSC integration** — send-only UDP, mirrors the shader uniform
  contract under `/listeningway/*`. Default 127.0.0.1:9000.
- **Per-process audio capture** option (Windows 10 22H2+).
- **Persistent diagnostic log** (`listeningway.log` next to the
  addon, gated by the Debug logging checkbox).

Granular per-beta detail remains in the [2.0.0-beta.1] through
[2.0.0-beta.4] entries below.

### Documentation

- `docs/openrgb.md` gains a "device shows up but doesn't physically
  respond" entry under Failure Modes. Covers the firmware-stuck
  state some RGB peripherals can fall into where they ignore all
  host commands (OpenRGB, vendor software, anything). Calls out the
  Roccat Vulcan Pro / Roccat Swarm "Reset to Factory Defaults"
  workaround, with general advice that this isn't Listeningway- or
  OpenRGB-specific.
- ADR-0013 (beat tracker rewrite) and ADR-0014 (OpenRGB pattern
  matrix) added to `docs/adr/`.

### Compatibility

- Shader uniform names: byte-compatible with v1. Existing v1 shaders
  continue to work; new uniforms are additive.
- Settings JSON: new fields default through nlohmann's
  `_WITH_DEFAULT` macro when missing from old configs. v1 configs
  are not migrated; v2 writes a fresh file with v2 defaults on
  first run.

## [2.0.0-beta.4] - 2026-05-03

OpenRGB integration redesigned around a per-zone-type pattern matrix:
single-LED accents, linear strips, and matrix grids each get their own
catalogue of behaviours. Plus a new persistent log file that pays for
itself the first time a user reports an OpenRGB enumeration problem.

### Changed

- **OpenRGB integration: per-zone-type pattern dispatch** (ADR-0014).
  The old single-formula spectrum-bar mapping is replaced with three
  named-pattern catalogues, one per OpenRGB zone type. The settings
  panel now exposes three dropdowns:

  ```
  Single (1)    [ Spectral Hue        ▾ ]
  Linear (2)    [ Spectrum Bar        ▾ ]
  Matrix (1)    [ Per-Region          ▾ ]
  ```

  Counters show how many non-empty zones of each type are connected
  (live-updated on hot-plug). One keyboard or five = same setting.
  Each catalogue is ordered active → soothing.

  - **Single** (1-LED accents — GPU, AIO pump, single-LED mouse):
    Beat Flash · Volume Pulse · **Spectral Hue** *(default)* ·
    Chronotensity Cycle · Static · Off
  - **Linear** (RAM, case strips, fan rings, motherboard accents):
    **Spectrum Bar** *(default)* · VU Meter · Chase / Orbit ·
    Pulse from Center · Stereo Split · Color Wash · Static · Off
  - **Matrix** (keyboards, mouse pads):
    Spatial Map · Equalizer Columns · **Per-Region** *(default)* ·
    Spectrogram Waterfall · Beat Flash · Color Wash · Static · Off

  Headline patterns worth flagging:
  - **Spatial Map** (matrix) projects `direction8` onto the keyboard's
    XY layout — left columns light when left-channel audio is loud,
    top rows when "Front" audio is loud. Uniquely Listeningway —
    no competitor has the spatial rose signal to drive this.
  - **Chase / Orbit** (linear) drives a moving comet from
    `phase_volume` (chronotensity), which keeps moving even when
    tempo confidence collapses. Solves the "every other tool's
    beat-locked effect dies on classical / speech / ambient" failure
    on fan rings and AIO loops.
  - **Per-Region** (matrix) maps bass→bottom rows, mid→middle,
    treble→top, beat→bottom row (where the spacebar usually lives).
    Geometric — no key-label lookup, works on any matrix.

  Defaults are calmer-end picks that work out of the box; users who
  want maximum activity flip each dropdown to its top entry.

  Per-device pattern overrides, named global profiles, and the
  canvas-and-place rendering architecture are all deferred to v1.1+
  per ADR-0014's out-of-scope section.

- **OpenRGB consumer iterates per zone, not per device.** A multi-zone
  device (e.g. ASUS motherboard with one Linear "Aura Mainboard"
  zone + three Linear "Aura Addressable" header zones) now renders
  each zone under the pattern picked for its type, then assembles
  the full per-LED color array in zone order for one
  `setDeviceColors()` call. Empty zones (typically unconnected ARGB
  headers) are skipped and don't count toward the type counter.

### Added

- **`listeningway.log` — persistent diagnostic log.** Lives next to
  the addon DLL, written when `Debug logging` is checked in the
  Settings panel. Already-promised in the tooltip for several
  releases; now actually wired up. Surfaces full OpenRGB connect
  trace including protocol version, device count from
  `requestDeviceCount`, devices returned from `requestDeviceList`,
  per-device + per-zone enumeration (name, type, LED count, matrix
  dimensions), `switchToCustomMode` failures, and the "device list
  empty after N attempts" diagnostic from the previous release.
  Errors are always written; INFO/WARN gated by the checkbox.

- **OpenRGB diagnostic retry + count-vs-list mismatch surfacing.** On
  empty-device-list responses the consumer retries 3× with 250 ms
  backoff before reporting; if the retry attempts return Success but
  zero devices the status line shows the diagnostic
  ("device list empty after 3 attempts (count: N / Success)") so
  users can tell the difference between "OpenRGB really has no
  devices" and "we connected to the wrong OpenRGB instance."

### Documentation

- ADR-0014 (new): OpenRGB visualisation patterns — per-zone-type
  matrix. Full design rationale, pattern catalogues, defaults,
  safety constraints (epilepsy band, silence handling, sustained
  max-white cap), and the alternatives considered (per-device
  picker, canvas-and-place, profiles).
- ADR index updated.

### Compatibility

- Settings JSON: backward-compatible. New fields (`pattern_single`,
  `pattern_linear`, `pattern_matrix`) default through nlohmann's
  `_WITH_DEFAULT` macro when missing from old configs. The removed
  v1 OpenRGB-specific fields don't exist; nothing was removed.
- Shader uniform contract: unchanged.

## [2.0.0-beta.3] - 2026-05-02

Clean-room beat-tracker rewrite, stereo spatial mapping fix, two new
spatial tunables, and a D3D11 cbuffer fix that gets `Listeningway.fx`
compiling again.

### Changed

- **Beat detector replaced with a clean-room implementation** of the
  Davies / Plumbley / Stark algorithm chain (CSD-HWR onset function →
  shift-invariant comb-filterbank tempo with Rayleigh prior and
  41-state Viterbi smoother → cumulative-score beat tracker with
  log-Gaussian transition window and forward-projected beat
  prediction). Replaces the v1 amplitude-flux + naive-autocorrelation
  detector that produced ~20% tempo confidence even on metronomic
  dance music. The new detector reports honest aubio-style peak/sum
  confidence in a usable range, locks tempo within a single octave to
  mitigate octave errors, and predicts the next beat sub-hop ahead
  rather than only reporting beats after they happen. See
  [ADR-0013](docs/adr/0013-beat-tracker-rewrite.md) for the design and
  [docs/adr/research-notes-beat.md](docs/adr/research-notes-beat.md)
  for the underlying study of BTrack and aubio.
- **Beat Detection panel redesigned around Auto / Profile / Custom**,
  following the convergent UX of pro audio tools (Logic Smart Tempo,
  iZotope Master Assistant, LANDR, Ableton Warp). Mode segmented
  control at the top of the Settings disclosure:
  - **Auto** (default). pulse_strength = 1.0; the underlying tracker
    self-tunes via its own Viterbi and cumulative-score state. A
    subtle status badge shows "Adapting…" while tempo confidence is
    settling and "Locked" once it has been above threshold for ~3 s.
    No knob to tune.
  - **Profile**. A second segmented control picks one of three named
    signal-character presets — **Percussive** (snappy 120 ms decay
    for drums / EDM / hip-hop), **Melodic** (160 ms decay for vocal /
    rock / jazz / classical), **Sustained** (230 ms decay for ambient /
    cinematic / sparse). Names follow Ableton Warp's "describe the
    signal, not the genre" convention so they don't age badly. Each
    profile sets `(pulse_strength, decay_tau)` together.
  - **Custom**. The Pulse Strength slider (0..3) is exposed. Switching
    from Auto or Profile into Custom seeds the slider with whatever
    the system was using a moment ago — no jarring reset (Lightroom's
    "Auto button moves the visible sliders" pattern).
  All ten v1 knobs (`threshold_lambda`, `threshold_window_ms`,
  `refractory_ms`, `phase_kp`, `phase_ki`, `tempo_prior_bpm`,
  `tempo_prior_sigma`, `tempo_window_sec`, `beat_decay_per_sec`, the
  `algorithm` switch) are gone from the UI and from `BeatConfig`.
- **`listeningway_beat` is now a continuous pulse curve** driven by
  the new tracker. Same [0, 1] range, same shader name. Each predicted
  beat attacks instantly to `pulse_strength` and decays exponentially
  with the mode's `decay_tau` — no per-frame jitter, no broken
  spikes when the tracker is unsure. Existing v1 shaders that read
  `Listeningway_Beat` as "a value that gets bigger on beats and
  smaller otherwise" continue to work; the value just feels better.
- **`listeningway_beat_phase` is now a forward prediction.** Sub-hop
  time-to-next-beat divided by the beat period — a smooth countdown
  between detected beats rather than the v1 PLL's "stale value
  between detections."
- Tempo (`tempo_bpm`, `tempo_confidence`, `tempo_detected`) and
  `beat_phase` stay as instrumentation but no longer have any UI
  knobs. Tempo search restricted internally to one octave (80..160
  BPM via the Viterbi observation vector, with the comb filterbank's
  harmonic summing absorbing 2× and 4× aliases). Shaders gate on
  `tempo_confidence > 0.05` (the new aubio-style metric) and fall
  back to the chronotensity phases when not locked, per the AudioLink
  design pattern.
- **FFT stage now also publishes per-bin phase** (`AnalysisFrame::phases`)
  alongside magnitudes. Required by the new CSD-HWR onset function
  in BeatStage; ignored by every other stage.

### Fixed

- **`Listeningway.fx` failed to compile under D3D11.** The
  `Listeningway_FreqBandsHistory[N*64]` uniform alone consumed 4096
  float4 slots at the default 64 bands, exactly the D3D11 constant
  buffer cap. With every other Listeningway uniform on top, the
  cbuffer overflowed (`Index Dimension 2 out of range, 4289 specified
  max 4096`) and the shader rejected with X8000. The history array is
  now 32 frames instead of 64 (`Listeningway_FreqBandsHistory[N*32]`,
  ~8 KB at 64 bands), leaving plenty of headroom in the cbuffer for
  user-shader uniforms. `kBandsHistoryFrames` is decoupled from
  `kVolumeHistoryLength` (volume history stays at 64).
- **Stereo spatial mapping.** Hard-Right and hard-Left audio no longer
  bias toward the front-right / front-left buckets. The previous v1
  routing put right-only content at 70% FR + 30% R, which assumed a
  "speakers in front of the listener" model and didn't match the
  headphone use case where R should mean R. The DirectionalStage now
  routes mid (in-both) energy to F, left-only energy to L, right-only
  energy to R. 5.1 and 7.1 keep their per-channel routing; LFE is no
  longer faked into Back.

### Added

- **`frequency.spatial_spread`** (default 0.25, range [0, 0.5]). How
  much each direction bucket's energy bleeds into its two ring
  neighbours. At the default, hard-Right shows R = 1.0 with FR = BR
  = 0.25 so the rose feels alive without overweighting any single
  bucket. 0 = sharp peaks per channel, 0.5 = soft glow.
- **`frequency.spatial_smoothing`** (default 0.10, range [0, 0.95]).
  Per-frame EMA on the `direction8` vector to calm flicker on
  percussive content. Independent of `audio.pan_smoothing` (which
  affects the L/R pan readout, not the rose).

### Changed

- The Spatial section's Settings disclosure exposes the two new
  tunables (Spread and Smoothing) alongside the existing Direction
  Boost.

## [2.0.0-beta.2] - 2026-05-02

Network outputs, loader-lock-safe boot, and a substantial overlay UX pass.

### Added

- **OSC consumer.** Send-only UDP broadcaster that mirrors the shader
  uniform contract under `/listeningway/*`. Default destination
  `127.0.0.1:9000` (TouchDesigner default). Wire layer: `mhroth/tinyosc`
  (ISC, two-file embed under `third_party/tinyosc/`). See
  [ADR-0011](docs/adr/0011-osc-output.md).
- **OpenRGB consumer.** TCP client that drives RGB peripherals from a
  running OpenRGB server. Default destination `127.0.0.1:6742`. Wire
  layer: `Youda008/OpenRGB-cppSDK` (MIT, full upstream tree embedded
  under `third_party/Youda008-OpenRGB-cppSDK/`). See
  [ADR-0012](docs/adr/0012-openrgb-output.md).
- **Self-disarm.** When a network consumer's worker fails the initial
  connect, the toggle in the overlay flips off automatically so the UI
  matches reality.
- **Integration harness** (`samples/integration_harness.py`).
  Single-file, stdlib-only Python that simultaneously receives OSC
  (validates address schema, tracks Hz / range / NaN per uniform) and
  mocks the OpenRGB server (implements the protocol subset cppSDK
  uses). Lets you verify the OpenRGB consumer end-to-end without
  installing OpenRGB itself.
- **`IOutputConsumer` adapter** wrapping all output destinations
  (uniform publisher, overlay, OSC, OpenRGB) under one lifecycle
  contract. See [ADR-0010](docs/adr/0010-network-outputs.md).

### Changed

- **Loader-lock-safe boot.** DllMain no longer does any heavy work.
  All initialization runs on the render thread via a per-frame boot
  scheduler (`src/boot/scheduler.{h,cpp}`) that walks five phases
  (`load_settings → build_audio_system → start_audio →
  build_consumers → start_consumers`), one phase per frame. Fixes a
  startup hang where worker-thread `WSAStartup` collided with the
  loader lock from `DllMain`.
- **`lw::App` lifecycle coordinator.** DllMain reduces to: register
  the addon, construct the App, hook a frame tick. The App owns
  subsystem ordering. Adding a new subsystem is a one-file change in
  `src/app.cpp`.
- **Overlay UX pass.** Section renames (Frequency Bands → Spectrum,
  Directional Intensity → Spatial, DSP Profiler → Performance,
  Network Outputs → Integrations, Settings Management → Settings).
  Each section header carries a live one-glance hint, e.g. "Beat
  Detection (128 BPM)" or "Spectrum (64 bands)". Per-section Settings
  disclosures are subdued (transparent button, dim "settings" text,
  marker fills in when open). Engineer-only knobs hide behind an
  Advanced sub-disclosure. Friendly per-control labels: `volume_norm`
  shows as "Auto-leveled volume", `spectral_centroid` as "Brightness",
  etc. Uniform names in shaders are unchanged.
- **Integration row layout.** Each integration is a single visible
  row: toggle button (highlighted green when on), inline status,
  per-row Settings disclosure on the right.
- **Save / Load / Reset** buttons dropped their "to disk" / "from disk"
  / "to defaults" suffixes.

### Fixed

- Startup hang when `network.openrgb.enabled = true` was saved (loader
  lock vs `WSAStartup`).
- The `?` glyphs in the overlay (em-dashes, bullets, ellipsis) when
  using ImGui's default font; non-Latin-1 codepoints replaced with
  ASCII fallbacks. Latin-1 supplement (µ, ·) is kept where it
  renders correctly.
- Hardcoded `v2.0.0-beta.` suffix in the addon `DESCRIPTION` string;
  ReShade displays the rc-derived version separately.

### Compatibility

- Shader uniform contract: unchanged; STABILITY.md unmodified.
- Settings JSON: backward-compatible. New `network.osc` and
  `network.openrgb` sub-structs default to `enabled = false`. Missing
  fields fall back to defaults via
  `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`.
- No new vcpkg dependencies. Both new wire-layer libraries are
  embedded under `third_party/` with `LICENSE` and `ATTRIBUTION.md`.

## [2.0.0-beta.1] - 2026-05-02

Major version. Clean-room rebuild of the engine; v1 behavior is preserved at the shader-uniform layer. Architectural decisions are captured in the ADRs under [`docs/adr/`](docs/adr/).

### Engine

- Clean-room rewrite of audio capture, DSP, and uniform publication.
  v1 source tree retired; v2 is built around a five-layer pipeline:
  `IAudioSource → FrameRing → DSP Pipeline → AudioSnapshot →
  Consumers` (ADR-0002).
- Adapter pattern at three points only: `IAudioSource`, `IDspStage`,
  and the beat-detector strategy (ADR-0003). Everything else is a
  concrete type.
- State machine for capture lifecycle: `Off → Starting → Running →
  Stopping → Off | Error`. Eliminates the v1 atomic-flag race that
  froze visuals on provider switch.
- Lock-free SPSC ring between capture and DSP threads
  (`moodycamel::ReaderWriterQueue`).
- Seqlock snapshot publication of an immutable POD `AudioSnapshot`;
  consumers read without locks.

### Audio sources

- `WasapiLoopbackSource` (default). System loopback, with the v1
  pinning fixes (`AUTOCONVERTPCM`, `MMCSS L"Audio"`, polling cadence
  driven by `GetDevicePeriod`).
- `ProcessAudioSource`. Per-process loopback via
  `ActivateAudioInterfaceAsync` and `PROCESS_LOOPBACK_MODE`. Captures
  the game's audio only, so Discord, browser tabs, and system
  notifications no longer bleed into the visualization. Available on
  Windows 10 22H2 (build 20348+) and Windows 11. See
  [ADR-0009](docs/adr/0009-process-audio-source.md).
- `OffSource`. Explicit "no analysis" provider; zeros the snapshot.

### Configuration

- `Settings` struct with `Setting<T>` declarative bounds: single
  source of truth for default, min, max, persistence key, and tooltip
  per tunable (ADR-0004).
- `nlohmann::json` round-tripping via intrusive macros, replacing the
  v1 hand-rolled parser that silently dropped `frequency.minFreq`
  and `maxFreq`.
- Atomic version-counter hot-reload. The DSP thread sees overlay
  changes without restart.

### Shader uniforms

All v1 uniform names preserved (ADR-0005). New additions in v2:

- AGC normalization: `volume_norm`, `bass_norm`, `mid_norm`,
  `treb_norm`, plus `*_att` smoothed siblings.
- Pre-binned band reductions: `freqbands16`, `freqbands32`.
- Spectral centroid: `spectral_centroid` (brightness input).
- K-weighted loudness: `loudness` (BS.1770 momentary, linear).
- Tempo and PLL phase: `tempo_bpm`, `tempo_confidence`, `beat_phase`.
- Chronotensity phases: `phase_volume`, `phase_bass`, `phase_treble`.
  Energy-accumulator alternatives to `beat_phase` for use when tempo
  isn't locked.
- History sources: `volume_history[64]`, `freqbands_history[N×64]`
  (band-major, time-ascending) for waterfall and spectrogram shaders.

See [STABILITY.md](STABILITY.md) for the full registry and stability
classification.

### Overlay

- Redesigned with collapsed-mode visuals and expanded-mode settings.
- Per-stage DSP profiler (EMA-smoothed wall-clock per stage and total
  pipeline µs).
- All settings reachable from the overlay; changes persist atomically.

### Testing

- 19 GoogleTest unit tests covering audit-found bugs (Phase 3).
- Property-based testing infrastructure via rapidcheck (ADR-0006).
  Full property coverage rolls in across v2.x.

### Removed

- v1 hand-rolled JSON parser.
- `ThreadSafetyManager`, `AudioCaptureManager`, and global graph state.
- v1 SSE3 SIMD path in FFT magnitude. The `simdEnabled` toggle is gone;
  v2 is scalar by default. An xsimd path is on the v1.5 roadmap if the
  per-stage profiler shows demand.
- Process-specific capture provider scaffolding from v1, replaced by
  the new clean-room `ProcessAudioSource`.

### Compatibility

- Shader uniform names: byte-compatible with v1. Existing shaders
  continue to work; new uniforms are additive.
- Configuration file format: schema reset. v1 `Listeningway.json`
  is **not** migrated; v2 writes a fresh file with v2 defaults on
  first run. Re-save your tuning from the overlay.

---

## [1.2.0.4] - pre-v2 (rolled into 2.0.0-beta.1)

Final v1 release. Subsequent v1 work was merged into the v2 rebuild.

### Added (v1.2.0.4)
- `Listeningway_NumBands` uniform: live band count.
- `Listeningway_Direction8` and FRBL aliases: directional intensity rose.
- Split Amplifier controls (Volume, Bands, Direction).
- Runtime SIMD toggle (since removed in v2).
- SPSC ring buffer between capture and analysis (since replaced by
  moodycamel in v2).
- Background TempoWorker for BPM (since replaced by the v2
  autocorrelation + PLL beat detector).
- Beat Profiles with a Custom option.

### Fixed (v1.2.0.4)
- Provider-switch freezes (System → Off → System).
- Default provider selection now honors `is_default`.

## [1.2.0.3] - 2025-06-07

### Refactor
- Restructured audio and provider modules.
- Removed process-specific audio capture providers (reintroduced as
  `ProcessAudioSource` in v2.0).
- Renamed and clarified audio capture provider classes.
- Introduced `AudioCaptureManager` (since retired in v2).

### Bug fixes
- Refined logarithmic gain in band calculations.
- Pan calculation: deadzone and surround channel handling.

## [1.2.0.2] - 2025-06-05

- Centralized configuration; live application of changes to capture
  and analysis.
- Configuration persistence switched to JSON (replacing the v1
  hand-rolled parser).
- Audio overlay shader redesigned.

## [1.2.0.0] - 2025-06-04

- Amplifier slider for overlay and uniforms.

## [1.1.0] - 2025-06-02

- Stereo spatialization uniforms (`VolumeLeft`, `VolumeRight`,
  `AudioPan`).
- Audio format detection uniform.
- Pan smoothing setting.

## [1.1.0.2] - 2025-06-04

- Build system enforces ReShade v6.3.3 / ImGui docking match.

## [1.0.x]

Initial public releases. Real-time FFT, beat detection, 32-band frequency analysis, 5-band equalizer, time uniforms, settings and overlay UI.
