# Listeningway: Shader uniform stability contract

This document is the public API contract for shader authors. Each uniform
`source` string below has a stability classification:

- **Stable**. The name and semantic range are guaranteed not to change
  in any v2.x release. A v3 schema break would be announced in a new ADR
  and given a deprecation period. Safe to depend on.
- **Experimental**. The name is fixed but the semantic (range, scaling,
  edge-case behavior) may evolve during the v2 beta. Once a beta cycle
  validates the semantic, the uniform graduates to Stable.

Not listed here = not a contract. Anything else you find by spelunking
(internal helpers, debug fields) can change without notice.

The full uniform-name registry lives in
[`src/output/shader_contract.h`](src/output/shader_contract.h). The publish
logic lives in [`src/output/uniform_publisher.cpp`](src/output/uniform_publisher.cpp).

ReShade resolves `source` annotations at effect load. Reference shape:

```hlsl
uniform float Listeningway_Volume    < source = "listeningway_volume";    >;
uniform float Listeningway_FreqBands[64] < source = "listeningway_freqbands"; >;
```

---

## Stable uniforms (preserved verbatim from v1)

| Source string | Type | Range / units | Semantic |
|---|---|---|---|
| `listeningway_volume` | `float` | [0, 1] | Mean-absolute amplitude × volume amplifier, clamped. Snappy reactivity. |
| `listeningway_volumeleft` | `float` | [0, 1] | RMS of left channel (or downmix). |
| `listeningway_volumeright` | `float` | [0, 1] | RMS of right channel. |
| `listeningway_audiopan` | `float` | [-1, +1] | Stereo / surround pan. -1 = full left, +1 = full right. |
| `listeningway_audioformat` | `float` | {0, 1, 2, 6, 8} | Channel count as float. 0 = no audio. |
| `listeningway_freqbands` | `float[N]` | [0, 1] each | Post-EQ frequency band amplitudes. N = `listeningway_numbands`. |
| `listeningway_numbands` | `float` | [8, 128] | Live band count published. Cap is `kMaxBands` (128). |
| `listeningway_beat` | `float` | [0, 1] | 1.0 on detected onset, decays at config rate. |
| `listeningway_timeseconds` | `float` | seconds | Wall-clock seconds since addon load. |
| `listeningway_timephase60hz` | `float` | [0, 1) | `fmod(t * 60, 1)`. |
| `listeningway_timephase120hz` | `float` | [0, 1) | `fmod(t * 120, 1)`. |
| `listeningway_totalphases60hz` | `float` | seconds × 60 | Cumulative phase count at 60 Hz. |
| `listeningway_totalphases120hz` | `float` | seconds × 120 | Cumulative phase count at 120 Hz. |
| `listeningway_direction8` | `float[8]` | [0, 1] each | 8-bucket spatial intensity. Order: F, FR, R, BR, B, BL, L, FL. |
| `listeningway_front` | `float` | [0, 1] | direction8[0]. |
| `listeningway_front_right` | `float` | [0, 1] | direction8[1]. |
| `listeningway_right` | `float` | [0, 1] | direction8[2]. |
| `listeningway_back_right` | `float` | [0, 1] | direction8[3]. |
| `listeningway_back` | `float` | [0, 1] | direction8[4]. |
| `listeningway_back_left` | `float` | [0, 1] | direction8[5]. |
| `listeningway_left` | `float` | [0, 1] | direction8[6]. |
| `listeningway_front_left` | `float` | [0, 1] | direction8[7]. |

## Stable uniforms (new in v2)

These are committed-to additions. The semantic may be tuned within the
documented range but the name will not change.

| Source string | Type | Range / units | Semantic |
|---|---|---|---|
| `listeningway_volume_norm` | `float` | [0, ~4] | AGC-normalized volume. 1.0 = recent average; >1 = loud, <1 = quiet. Clamped at `agc.clamp_max`. |
| `listeningway_volume_att` | `float` | [0, ~4] | Time-attenuated `volume_norm` (asymmetric attack/release). |
| `listeningway_bass_norm` | `float` | [0, ~4] | AGC-normalized bass-band energy (lowest band-third). |
| `listeningway_mid_norm` | `float` | [0, ~4] | AGC-normalized mid-band energy. |
| `listeningway_treb_norm` | `float` | [0, ~4] | AGC-normalized treble-band energy. |
| `listeningway_bass_att` | `float` | [0, ~4] | Time-attenuated `bass_norm`. |
| `listeningway_mid_att` | `float` | [0, ~4] | Time-attenuated `mid_norm`. |
| `listeningway_treb_att` | `float` | [0, ~4] | Time-attenuated `treb_norm`. |
| `listeningway_freqbands16` | `float[16]` | [0, 1] each | 16-band reduction of `freqbands` (averaged buckets). |
| `listeningway_freqbands32` | `float[32]` | [0, 1] each | 32-band reduction of `freqbands`. |
| `listeningway_spectral_centroid` | `float` | [0, 1] | Magnitude-weighted spectral center, normalized to Nyquist. Useful for "brightness" / color-temperature shaders. |

## Experimental uniforms

| Source string | Type | Range / units | Semantic | Notes |
|---|---|---|---|---|
| `listeningway_loudness` | `float` | [0, 1] | K-weighted (BS.1770) momentary loudness over `loudness.window_ms` (default 400 ms), linear sqrt of mean-square. | Output is linear, *not* LUFS log. May add a separate `_lufs` log uniform if asked. |
| `listeningway_beat_phase` | `float` | [0, 1) | PLL-locked beat phase. Continuously advances at the detected tempo when locked. | Falls back to 0 when tempo isn't locked. Use `tempo_confidence` to gate. |
| `listeningway_tempo_bpm` | `float` | BPM | Detected tempo. 0 if undetected. | Estimator quality is genre-dependent; chronotensity phases are more stable. |
| `listeningway_tempo_confidence` | `float` | [0, 1] | Peak-to-second-peak ratio of tempo autocorrelation, EMA-smoothed. | `tempo_detected` becomes true above 0.4. |
| `listeningway_phase_volume` | `float` | [0, 1) | Chronotensity phase: accumulates `1.0 + k·(volume_norm-1)` Hz mod 1.0. | Stable replacement for `beat_phase` when tempo isn't locked. |
| `listeningway_phase_bass` | `float` | [0, 1) | Chronotensity phase driven by `bass_norm`. | |
| `listeningway_phase_treble` | `float` | [0, 1) | Chronotensity phase driven by `treb_norm`. | |
| `listeningway_volume_history` | `float[64]` | [0, 1] each | Last 64 frames of `volume`, oldest at index 0. | Enables waterfall/trail effects without shader-side ring buffers. |
| `listeningway_freqbands_history` | `float[N*64]` where N = DEFAULT_NUM_BANDS | [0, 1] each | Last 64 frames of the post-EQ band amplitudes at native resolution. Layout: **band-major**, time-ascending. Index helper: `freqbands_history[band * 64 + frame]`. `frame=0` is oldest, `frame=63` is newest. Band axis matches `listeningway_freqbands` / `listeningway_numbands`. At runtime the addon writes `numbands * 64` entries; the remaining uniform tail is whatever was last written. | Per-band waterfall / spectrogram material. ~16 KB uniform read at the default 64 bands. Shaders that want fewer bins (e.g. 16) can average down in-shader. |

---

## Compatibility guarantees

- **v1 → v2:** every v1 uniform name above continues to work. Some semantics
  may be tuned (better tempo tracker, AGC-aware behavior), but the
  numerical range is preserved.
- **v2.x → v2.y (minor):** Stable uniforms are byte-compatible. Experimental
  uniforms may have semantic changes; CHANGELOG will call them out.
- **v2 → v3 (major):** breaking changes published as a new ADR superseding
  ADR-0005. We aim to avoid this for at least one release cycle.

If a shader you maintain uses an Experimental uniform and you want it
graduated to Stable, file an issue. That's the signal we're looking for.

---

## How to add a new uniform

1. Add a new `kFoo = "listeningway_foo"` line to [`src/output/shader_contract.h`](src/output/shader_contract.h).
2. Add the source field to [`AudioSnapshot`](src/audio/snapshot/audio_snapshot.h) (must remain trivially copyable).
3. Add a publish step to [`src/output/uniform_publisher.cpp`](src/output/uniform_publisher.cpp).
4. Add a `value_or` projection in [`AudioSystem::dsp_thread_main`](src/audio/pipeline/audio_system.cpp) so the snapshot field gets populated from the DSP pipeline.
5. Update this STABILITY.md.

The compile-time string-view match in `uniform_publisher.cpp` will fail
with a missing-symbol error if you reference a constant that doesn't exist
in `shader_contract.h`.
