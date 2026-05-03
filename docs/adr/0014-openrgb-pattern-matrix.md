# ADR-0014: OpenRGB visualisation patterns — per-zone-type matrix

## Status

Accepted, 2026-05-03

## Context

The original OpenRGB consumer (ADR-0012) ships a single fixed mapping: every LED on every device is treated as a position in a 1D bass→treble spectrum, and the colour formula `freqband[t] · 1.5 + volume_norm · 0.3 + beat · 0.4` is applied per-LED with a five-stop ramp. This made every device look the same and made low-LED-count devices (a 1-LED GPU accent, a 4-LED motherboard zone) read as a single dim flicker.

User-wishlist research (logged separately) surveyed the audio-reactive RGB ecosystem — KeyboardVisualizer (the OpenRGB-author's reference visualiser), the OpenRGB Effects Plugin, SignalRGB, Aurora, Artemis, iCUE Murals, Razer Chroma Studio + Audio Visualizer, plus AudioLink as the most-influential audio-reactive contract in the visualiser world. Three convergent findings:

1. **Pattern preference is strongly tied to physical zone shape.** Single-LED accents want one slow scalar (centroid hue, smoothed volume). Linear strips want a spectrum bar or a chase. Keyboard matrices want either a per-region keyboard mapping or an equaliser-column pattern. Spectrogram waterfalls on keyboards are repeatedly described as "cool for 30 seconds, distracting forever."
2. **The architectural pattern that pays off long-term is canvas-and-place** — render any 2D effect to a virtual canvas, sample per-LED by physical position. Both KeyboardVisualizer and iCUE Murals use this. Defer; not v1 scope.
3. **The UX convention that scales is auto-by-zone-type → named profile dropdown → optional Advanced sliders.** Per-device pattern editors exist in Aurora and Artemis but only ~10–20% of users exercise them; the rest want sane defaults that don't require thinking about each device.

The user proposed (and this ADR adopts) a UI shape the research wasn't quite reaching for: **a single 3-row matrix exposing one dropdown per zone type**. Single, Linear, Matrix — no per-device editor, and a counter shows how many of each type are connected. One keyboard or five keyboards: same setting, no per-device fanout. The spatial example the user cited — keyboards that light up the left side when left-channel audio is loud — is a pattern that's genuinely unique to Listeningway because nobody else exposes the `direction8` 8-bucket spatial rose.

## Decision

The OpenRGB consumer dispatches per-LED behaviour by **zone type**, not per-device. Configuration is three dropdowns in the overlay (`Single`, `Linear`, `Matrix`), each exposing a catalogue of patterns ordered active → soothing.

### Per-type pattern catalogue

#### Single (1-LED zones — GPU accents, AIO pumps, single-LED mice)

| # | Pattern | Audio signal | Notes |
|---|---|---|---|
| 1 | **Beat Flash** | `beat` curve over a fixed accent colour | Showiest single-LED option |
| 2 | **Volume Pulse** | `volume_att` → brightness, fixed colour | No flashes; rises and falls |
| 3 | **Spectral Hue** *(default)* | `spectral_centroid` → hue, `volume_att` → brightness | Warm bass, cool treble; the "set it and forget it" choice |
| 4 | **Chronotensity Cycle** | hue rotates at `phase_volume` rate | Smooth always-moving even on silent content |
| 5 | **Static** | none | Fixed colour, no reaction |
| 6 | **Off** | none | Black |

#### Linear (1D strips — RAM, case strips, fan rings, motherboard accents, ARGB headers)

| # | Pattern | Audio signal | Notes |
|---|---|---|---|
| 1 | **Spectrum Bar** *(default)* | `freq_bands` across length, amplitude → brightness | Current behaviour; the canonical strip pattern |
| 2 | **VU Meter** | `volume_norm` fills from one end | Peak-hold dot at apex |
| 3 | **Chase / Orbit** | `phase_volume` drives a moving "comet" | Listeningway-distinctive — stays smooth even when tempo isn't locked; ideal for fan rings |
| 4 | **Pulse from Center** | `bass_norm` + `beat` ripple outward symmetrically | Best for symmetric fixtures |
| 5 | **Stereo Split** | left half = `volume_left`, right half = `volume_right` | Only meaningful on strips ≥ 10 LEDs |
| 6 | **Color Wash** | solid colour, hue from `spectral_centroid`, brightness from `volume_att` | Quiet ambient |
| 7 | **Static** | none | |
| 8 | **Off** | none | |

#### Matrix (2D grids — keyboards, mouse pads)

| # | Pattern | Audio signal | Notes |
|---|---|---|---|
| 1 | **Spatial Map** | `direction8` projected onto matrix XY | Genuinely unique to Listeningway; left columns light when left-side audio loud, top rows when "Front" loud, etc. |
| 2 | **Equalizer Columns** | N frequency bands as vertical bars across columns | Most-praised matrix pattern in community discussion ("easier to read than waterfall") |
| 3 | **Per-Region** *(default)* | bass→alphas, mid→numrow, treb→F-row, beat→spacebar | Praised for not looking noisy during typing |
| 4 | **Spectrogram Waterfall** | time scrolls down rows, freq across columns | Polarising — present for users who like it, not the default |
| 5 | **Beat Flash** | entire matrix pulses on a base colour | |
| 6 | **Color Wash** | solid, hue drifts with `spectral_centroid` | |
| 7 | **Static** | none | |
| 8 | **Off** | none | |

### Defaults (calmer end of each type's reactive range)

- Single → **Spectral Hue**
- Linear → **Spectrum Bar**
- Matrix → **Per-Region**

All three are reactive but on the "pleasant surprise" end rather than "wall of strobing." Users who want maximum reactivity flip each dropdown to its top entry; users who want quiet flip toward the bottom.

### UI shape

```
OpenRGB                  3 devices • 1 single 1 linear 1 matrix
  Enabled  [●]  Status: 30 Hz, 2627 frames                   settings ·
  ────────────────────────────────────────────────────────────────
  Single (1)    [Beat Flash       ▾]
  Linear (1)    [Spectrum Bar     ▾]
  Matrix (1)    [Spatial Map      ▾]
  Brightness    ───●──────── 1.00
  Test          [Flash all LEDs]
```

Counters auto-update on each device-list refresh. A row whose count is 0 reads "Matrix (0) — no devices" with the picker disabled.

### Iteration is per-zone, not per-device

A device can have multiple zones of different types (the screenshot showed an ASUS motherboard with 1 Linear "Aura Mainboard" + 3 Linear "Aura Addressable" ARGB headers, plus Listeningway-relevant zones on Single GPU and Matrix keyboard). The dispatcher iterates `device.zones`, picks the pattern for each zone's `ZoneType`, builds a per-zone colour vector, and assembles the full device colour array (size = `device.leds.size()`) by concatenating in zone order. One `setDeviceColors()` call per device per frame.

Empty zones (`leds_count == 0` — typically unconnected ARGB headers) are skipped entirely and don't count toward the type counter.

### Safety constraints (built-in, not user-tunable)

- **Photosensitive epilepsy**: cap visible-flash rate by smoothing the beat curve before it drives Beat Flash patterns. The new tracker (ADR-0013) already produces a smoothed continuous `beat` curve rather than raw onsets; preserve that.
- **Silence handling**: every pattern fades to black (or to its configured static colour) when `volume_att` falls below a hard noise gate. No flickering on silence.
- **Sustained max-white**: the Color Wash and Static patterns clamp brightness to 0.95 of full to avoid stress on some ARGB hardware. User-set Brightness multiplier composes on top.

### Out of scope for v1

- **Per-device pattern overrides**. Defer until usage data shows demand. The matrix-of-types covers 90% of cases.
- **Canvas-and-place architecture** (KeyboardVisualizer/iCUE-Murals style). Real win, but ~600 LOC of layout + sampling infrastructure. v1.1 candidate.
- **GLSL shader pattern pipeline**. Huge surface, marginal user delight.
- **Per-zone customisation within a device**. Requires per-zone editor UI; ~10% of users would touch it.

## Consequences

### Positive

- Each zone type finally feels appropriate to its physical form. A 1-LED GPU accent stops looking like a flicker and starts conveying spectral character via hue. A 104-key keyboard stops being a flat spectrum and becomes either spatial (left/right channel position), columnar EQ bars, or per-region (bass on the alphas).
- Configuration scales linearly with zone-type variety, not device count. One keyboard or five: one dropdown.
- The `Spatial Map` matrix pattern is genuinely unique to Listeningway. No competitor has the `direction8` signal. This is the headline feature for users with a keyboard.
- The `Chase / Orbit` linear pattern uses `phase_volume` (chronotensity), which stays smooth even when tempo confidence collapses. A pattern that doesn't stop being satisfying when the music changes.
- Defaults are calm, opt-in to high reactivity. Matches the "pleasant surprise" UX bar from research.
- Shared pattern-primitive helpers (hue ramps, smoothed amplitudes, Color RGB type) make adding patterns later cheap.

### Negative

- ~600–800 LOC of new pattern code (six patterns × three types, with shared helpers reducing per-pattern cost). Substantial vs the current ~100 LOC `compose_frame` function.
- More config surface in `OpenRgbConfig`: three new enum fields plus their JSON-marshalling tables. Old configs deserialise cleanly via `_WITH_DEFAULT`.
- Per-zone iteration changes the OpenRGB consumer's hot-loop shape (was: one composer per device; now: one composer per zone per device). The cost is negligible (each composer is a flat O(LEDs in zone) loop) but the code path is more layered.
- Documentation cost: each pattern's signal mapping needs a one-paragraph description in `docs/openrgb.md` so users know what they're picking.

### Neutral

- The Test button (`send_test_packet`) keeps its current behaviour (flashes all LEDs white briefly) — useful for "is the wiring alive" diagnostics regardless of pattern choice.
- Brightness multiplier and self-disarm semantics unchanged.

## Alternatives considered

### Per-device dropdown (Aurora / Artemis style)

**Rejected for v1.** Real flexibility but the configuration cost lands on every user, including the ones with one keyboard who'd rather not think about it. Research showed only 10–20% of users exercise per-device editors in mature tools. Adopt later if usage data shows demand.

### Per-zone dropdown within each device

**Rejected for v1.** Granularity below the device level is exercised by even fewer users, and most multi-zone devices (motherboard + ARGB headers) are best served by treating their Linear zones identically anyway.

### Profiles (Ambient / Energetic / Music) at the global level

**Considered, deferred.** A natural future addition layered on top of the per-type matrix — each profile sets all three dropdowns + the brightness slider in one click. Skip for v1 because the matrix itself is the story; profiles can be a v1.1 convenience.

### Canvas-and-place architecture

**Deferred to v1.1.** The right long-term answer (write a pattern once, get correct rendering on keyboards / strips / fan rings for free), but ~600 LOC of layout + sampling infrastructure for v1 is too much. Per-type dispatch is the cheap path that already gets us the user-felt benefit.

### Auto-detect best pattern from audio characteristics

**Rejected.** The "smart" choice would be to switch patterns based on whether the audio is percussive or sustained. But pattern preference is also aesthetic — some users want VU meter regardless of audio. Don't take agency away. Auto behaviour goes inside each pattern (sensitivity adapts), not at the pattern-selection level.

## References

- ADR-0010: `IOutputConsumer` abstraction + vendoring policy.
- ADR-0012: OpenRGB consumer (the per-LED-spectrum mapping this ADR replaces).
- Research notes (separate document) on the audio-reactive RGB ecosystem: KeyboardVisualizer, SignalRGB, Aurora, Artemis, iCUE Murals, Razer Chroma Studio.
- AudioLink (VRChat) — design influence for the always-on chronotensity-driven patterns.
- KeyboardVisualizer (CalcProgrammer1) — `https://github.com/CalcProgrammer1/KeyboardVisualizer`. The de facto OpenRGB audio visualiser; canvas-and-place architectural inspiration for v1.1.
