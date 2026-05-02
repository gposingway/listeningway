# ADR-0005: Shader uniform contract — preserve names, expand additively

## Status

Accepted — 2026-05-01

## Context

Listeningway's shader uniform names are its public API. Every published shader that consumes Listeningway data depends on specific uniform source strings — `listeningway_volume`, `listeningway_freqbands`, `listeningway_beat`, etc. ReShade resolves these strings at effect load time; renaming any of them silently breaks every shader that referenced the old name.

ADR-0001 commits the v2 engine to a beta release with broad permission to change behavior. The shader uniform contract is the **one stability frontier** that does not change: existing shaders must continue to work unmodified against v2.

The visualizer-design research ([research-notes.md](research-notes.md) §4) surfaced concrete additions that v2 should make to the contract — additions that match what shader authors in adjacent ecosystems (MilkDrop, projectM, AudioLink, Wallpaper Engine) actually consume. v2 expands additively: every existing uniform stays at its existing name and semantic; new uniforms get new namespaced names.

## Decision

### 1. Preserve all existing uniforms exactly

Every uniform source string currently in `assets/ListeningwayUniforms.fxh` continues to work in v2 with identical semantics. The full list of preserved uniforms (22 total):

| Source string | Field type | Semantic |
|---|---|---|
| `listeningway_volume` | float | Smoothed amplifier-scaled RMS, clamped [0, 1] |
| `listeningway_volumeleft` | float | Left channel volume (or downmix), [0, 1] |
| `listeningway_volumeright` | float | Right channel volume, [0, 1] |
| `listeningway_audiopan` | float | Stereo / surround pan, [-1, +1] |
| `listeningway_audioformat` | float | Channel count as float (1, 2, 6, 8) |
| `listeningway_freqbands` | float[N] | Frequency band amplitudes, default N=64 |
| `listeningway_numbands` | float | Live band count |
| `listeningway_beat` | float | Beat impulse value, [0, 1], spikes to 1 on detect |
| `listeningway_timeseconds` | float | Seconds since addon start |
| `listeningway_timephase60hz` | float | `fmod(t * 60, 1.0)` |
| `listeningway_timephase120hz` | float | `fmod(t * 120, 1.0)` |
| `listeningway_totalphases60hz` | float | `t * 60` (cumulative cycle count) |
| `listeningway_totalphases120hz` | float | `t * 120` |
| `listeningway_direction8` | float[8] | 8-bucket spatial intensity (Front, FR, R, BR, B, BL, L, FL) |
| `listeningway_front` | float | direction8[0] |
| `listeningway_front_right` | float | direction8[1] |
| `listeningway_right` | float | direction8[2] |
| `listeningway_back_right` | float | direction8[3] |
| `listeningway_back` | float | direction8[4] |
| `listeningway_back_left` | float | direction8[5] |
| `listeningway_left` | float | direction8[6] |
| `listeningway_front_left` | float | direction8[7] |

Numerical scaling is preserved — values produced by v2 land in the same envelopes shader authors expect, even if the underlying detection algorithm has changed (per ADR-0007's improvements). When a v2 algorithmic upgrade meaningfully changes a uniform's distribution (e.g. a better tempo tracker producing a more stable `beat` envelope), this is treated as a bug-fix-grade behavior change covered by the beta acceptance from ADR-0001, not as a contract break.

### 2. Expand additively for v2

The visualizer research ([research-notes.md](research-notes.md) §4) identifies five high-value additions. All are new names; none rename or repurpose existing names.

#### AGC-normalized variants (highest priority — addresses the biggest v1 gap)

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_volume_norm` | float | `volume / running_mean(volume)` over ~5–10 s window. 1.0 = "average loudness," >1.3 = "loud," <0.7 = "quiet." |
| `listeningway_volume_att` | float | Time-attenuated (smoothed) version of the normalized volume (asymmetric attack/decay). |
| `listeningway_bass_norm` | float | Bass-band (≤ ~250 Hz) normalized to running average, same envelope. |
| `listeningway_mid_norm` | float | Mid-band (~250 Hz – ~4 kHz) normalized. |
| `listeningway_treb_norm` | float | Treble-band (≥ ~4 kHz) normalized. |
| `listeningway_bass_att`, `listeningway_mid_att`, `listeningway_treb_att` | float | Smoothed siblings. |

Justification: every preset-author-facing visualizer system in research (MilkDrop, projectM, butterchurn) exposes these and shader authors strongly prefer them — they make presets work across loud/quiet sources without per-preset tuning. v1 exposes only raw values; this is the single biggest gap.

#### K-weighted perceptual loudness

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_loudness` | float | K-weighted (BS.1770) momentary loudness over 400 ms window. Slower musical envelope; complementary to `volume`. |

Justification: gives shaders a perceptually meaningful slow envelope distinct from snappy `volume`. Cost is two biquads per channel + a sliding-window sum (negligible vs FFT). Coefficients in [research-notes.md](research-notes.md) §5.

#### Chronotensity-style phase accumulators

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_phase_volume` | float | Accumulator advancing proportional to `volume_norm`, modulo 1.0. |
| `listeningway_phase_bass` | float | Accumulator advancing proportional to `bass_norm`. |
| `listeningway_phase_treble` | float | Accumulator advancing proportional to `treb_norm`. |

Justification: AudioLink's most-praised feature. Lets shaders do tempo-locked motion (rotation, scroll, hue cycle) without `_Time.y`, *robust to the genre-fragility of explicit BPM detection*. See [research-notes.md](research-notes.md) §4 for AudioLink's rationale for preferring chronotensity over BPM-locked phase.

#### MIR-style beat phase (when tempo is confidently locked)

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_beat_phase` | float | PLL-locked beat phase, [0, 1). Continuously advances at the detected tempo, soft-corrected by detected onsets. |
| `listeningway_tempo_bpm` | float | Detected tempo in BPM. 0 if not locked. |
| `listeningway_tempo_confidence` | float | Tempo confidence, [0, 1]. |

Justification: complementary to chronotensity — when tempo is locked confidently, this is more "musically correct" than chronotensity for shaders that want true beat synchronization. When tempo is *not* locked, `tempo_confidence` is 0 and shaders should fall back to chronotensity.

#### Multiple band counts

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_freqbands16` | float[16] | Coarse 16-band reduction derived from the full FFT. |
| `listeningway_freqbands32` | float[32] | Mid-coarseness 32-band reduction. |

Justification: many bar-style shaders want a coarse 8 or 16-bin layout for ergonomic mapping (e.g. one bar per beat-grid position) without aliasing the 64-bin array down themselves. Wallpaper Engine offers exactly this; cheap to provide.

#### Spectral centroid

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_spectral_centroid` | float | Magnitude-weighted spectral centroid normalized to [0, 1] (divided by Nyquist). |

Justification: real shader-author demand for "brightness / timbre" → color-temperature effects. Cheap to compute (one weighted sum). Skip rolloff and flatness for v1; reconsider if requested.

#### Volume history

| New uniform | Field type | Semantic |
|---|---|---|
| `listeningway_volume_history` | float[64] | Ring-buffered last 64 frames of `volume` (oldest at index 0). |

Justification: AudioLink's most-praised UX feature is per-band history. Per-band history for all 64 bands is expensive (4096 floats); start with one volume row to enable waterfall/trail effects without shader-side ring buffers. Per-band history can be added in v1.5 if requested.

### 3. Centralized shader contract

All uniform source strings are declared once in a header (proposed: `src/output/shader_contract.h`):

```cpp
namespace lw::shader_contract {
inline constexpr std::string_view kVolume          = "listeningway_volume";
inline constexpr std::string_view kFreqBands       = "listeningway_freqbands";
inline constexpr std::string_view kBeat            = "listeningway_beat";
// ... all 22 existing + new uniforms
}
```

The `UniformPublisher` consumes these constants when writing into ReShade. Adding a new uniform requires adding its source string to this header and a corresponding publish step that reads from `AudioSnapshot`. Compile errors if a publish step references a missing constant.

### 4. STABILITY.md classifies each uniform

A `STABILITY.md` document in the repo root accompanies v2 release and classifies every uniform:

- **Stable** — guaranteed to remain at its current name, range, and semantic across v2 minor versions. All 22 preserved uniforms; the AGC `_norm` / `_att` siblings; the multiple-band-count uniforms; spectral centroid.
- **Experimental** — semantics may evolve during v2 beta, name is stable. `phase_*` chronotensity accumulators; `beat_phase`, `tempo_bpm`, `tempo_confidence`; `loudness`; `volume_history`.
- **Removed in v2.0.0** — none.

Experimental uniforms move to Stable in a subsequent v2.x release once shader-author feedback validates their semantics.

## Consequences

### Positive

- **Existing shaders work unchanged.** ADR-0001's beta-release permission applies to internal behavior, not to public API; this ADR holds the line on the public side.
- **Clear extensibility path.** Adding new uniforms is a documented two-step (add to contract header + add publish step from snapshot field).
- **Compile-time error if a uniform references a missing snapshot field.** Stringly-typed dispatch (the v1 pattern) becomes typed dispatch.
- **Shader authors get materially more capability.** AGC-normalized values, history, multiple band counts, phase accumulators, perceptual loudness — all cheap to compute, all unlock effects that v1 forces shader authors to reinvent.
- **STABILITY.md documents promises.** Future contributors know which names are sacred and which can evolve.

### Negative

- **`AudioSnapshot` grows.** Adding ~15 fields. Still tens of bytes, well within seqlock-friendly POD size.
- **Per-frame uniform write count grows by ~50%.** Each uniform write is a `runtime->set_uniform_value_float` call — already O(1) per uniform. Still negligible at typical frame rates.
- **Some new uniforms may not be ideal in beta** — chronotensity rate constants, AGC time windows. Beta period covers tuning; STABILITY.md flags them as Experimental.

### Neutral

- **The `extern "C" SwitchAudioProvider` DLL export** is reviewed separately. v2 may keep it as a thin wrapper for backwards compatibility with any external script callers; if no callers exist, it can be removed. Decided at v2 release time, not now.

## Alternatives considered

### Rename existing uniforms to a cleaner namespace
e.g. `listeningway.beat.value`, `listeningway.tempo.bpm`. **Rejected.** Breaks every existing shader. ReShade uniform sources are flat strings; there's no actual hierarchy. Cleaner-looking, not cleaner-functioning.

### Drop legacy direction-individual uniforms (`listeningway_front`, etc.) in favor of `direction8`
**Rejected.** They exist; shaders consume them; removing breaks shaders. The publisher writes both — cost is 8 extra `set_uniform_value_float` calls per frame. Trivial.

### Expose every internal feature as a uniform
**Rejected.** Per-band beats, spectral rolloff, spectral flatness, autocorrelation peaks — each is computed but not exposed. Visualizer research showed shader-author demand is concentrated in a small set; over-exposure increases the surface to maintain without increasing the value to shaders.

### Use ReShade buffer/SRV for high-bandwidth analysis
e.g. expose the full FFT as a 1024-bin buffer instead of 64-band uniforms. **Deferred.** Designed-in but not implemented in v1. The snapshot type doesn't preclude it; we can add a buffer-publishing path in v1.5 if a shader author requests it.

### Output binary structure layout (memory-mapped struct)
**Rejected.** Coupling the binary layout of `AudioSnapshot` to shader uniform names creates a viral compatibility constraint. The string-keyed uniform contract is the right abstraction.

## References

- [research-notes.md §4](research-notes.md) — visualizer design research (MilkDrop, projectM, butterchurn, Wallpaper Engine, AudioLink).
- [research-notes.md §5](research-notes.md) — K-weighting / LUFS research informing the `loudness` uniform.
- [research-notes.md §1](research-notes.md) — MIR research informing tempo / beat phase uniforms.
- ADR-0001 — beta-release scope; contract preservation is the one frontier that doesn't break.
- ADR-0007 — implementation timing for new uniforms.
