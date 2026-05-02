<div align="center">

# Listeningway

**Real-time audio analysis exposed to ReShade shaders as uniforms.**

![Listeningway Showcase](https://github.com/user-attachments/assets/8a11d6b6-bdea-4c31-9614-dbfb7ad8819f)

[<img src="https://github.com/user-attachments/assets/20794810-9e43-4167-bb0e-faf46275186e">](https://github.com/gposingway/Listeningway/releases/latest)

</div>

---

Listeningway listens to audio, runs the analysis on a dedicated DSP thread, and publishes the results to your `.fx` files: volume, frequency bands, beat, tempo, AGC-normalized energies, spectral centroid, K-weighted loudness, history rings. Existing v1 shaders keep working. New uniforms are additive.

> **v2 status: beta.** The engine is a clean-room rebuild on a five-layer pipeline. The shader uniform contract is committed; v1 names are preserved verbatim. See the [ADRs](docs/adr/) for design context and [STABILITY.md](STABILITY.md) for the public uniform registry.

## Highlights in v2

- Three audio sources, picked from the overlay dropdown:
  - **System Audio (WASAPI Loopback)** is the default. It captures everything you hear and works on Windows 10 and 11.
  - **Game Audio Only (Process Loopback)** captures the host process only, so Discord, browsers, and music apps stop bleeding into the visualization. Requires Windows 10 22H2 (build 20348+) or Windows 11. See [ADR-0009](docs/adr/0009-process-audio-source.md).
  - **None (Off)** disables analysis cleanly; uniforms zero out.
- Stable provider switching via an explicit state machine, so visuals no longer freeze when you change sources.
- AGC-normalized uniforms (`volume_norm`, `bass_norm`, `mid_norm`, `treb_norm` and their `*_att` smoothed siblings) so shaders react consistently across loud and quiet content without per-track tuning.
- Tempo with PLL phase, plus chronotensity for stable beat sync when the BPM estimator isn't locked.
- Per-band history (64 frames × 64 bands) for waterfall and spectrogram shaders.
- Per-stage DSP profiler in the overlay so you can see exactly where the cost is.

---

## For end users

You'll need:

- ReShade 6.3.3 or newer (API 14+). Older versions (5.2.0 and earlier) are not supported and may crash. AuroraShade R10 is compatible.
- Windows 10 or 11. The "Game Audio Only" source additionally needs Windows 10 22H2 (build 20348+) or Windows 11; on older builds that source is grayed out.

Install:

1. Download the latest release from [the releases page](https://github.com/gposingway/Listeningway/releases/latest). Inside the ZIP you'll find `Listeningway.addon`, `Listeningway.fx`, and `ListeningwayUniforms.fxh`.
2. Place the files:

   | File | Location | Why |
   |---|---|---|
   | `Listeningway.addon` | Game directory (next to your ReShade DLL: `dxgi.dll`, `d3d11.dll`, etc., **not** inside `reshade-shaders`) | ReShade loads `.addon` files from the same directory as the DLL |
   | `Listeningway.fx` | `reshade-shaders\Shaders\` | Sample shader showing all uniforms in use |
   | `ListeningwayUniforms.fxh` | `reshade-shaders\Shaders\` | Header for `#include` from your shader |

3. Launch the game. Open the ReShade overlay; you'll see the **Listeningway** panel. Pick a source from the dropdown. Enable `Listeningway.fx` to see it react.

To tune, open the Listeningway overlay (in the ReShade menu) and expand the section you want to change. Save with the Save button at the bottom; settings persist to `Listeningway.json` next to the addon. If you'd rather edit the JSON directly, restart the game to apply.

---

## For shader authors

You consume Listeningway data via annotation-bound uniforms. Reference shape:

```hlsl
uniform float Listeningway_Volume          < source = "listeningway_volume"; >;
uniform float Listeningway_FreqBands[64]   < source = "listeningway_freqbands"; >;
uniform float Listeningway_NumBands        < source = "listeningway_numbands"; >;
uniform float Listeningway_Beat            < source = "listeningway_beat"; >;
uniform float Listeningway_TimeSeconds     < source = "listeningway_timeseconds"; >;
```

For convenience, `#include "ListeningwayUniforms.fxh"` declares every uniform with the right `source` annotation. The header is generated at build time from `templates/ListeningwayUniforms.fxh.template`, with `LISTENINGWAY_NUM_BANDS` substituted from the addon's `DEFAULT_NUM_BANDS` constant.

The full uniform registry, including names, types, ranges, semantics, and stability tier (Stable vs Experimental), lives in [**STABILITY.md**](STABILITY.md). That document is the public API contract.

### Quick tour of the v2 uniforms most worth knowing

| Uniform | What it is | When you'd reach for it |
|---|---|---|
| `listeningway_volume_norm` | AGC-normalized energy (1.0 = recent average) | Replaces per-effect "sensitivity" sliders. Reacts the same way to loud and quiet music. |
| `listeningway_volume_att` | Smoothed `volume_norm` (asymmetric attack/release) | When you want the AGC value but not the jitter. |
| `listeningway_bass_norm`, `mid_norm`, `treb_norm` | AGC-normalized macro bands | "Bass kick" or "treble glitter" effects without genre-specific tuning. |
| `listeningway_freqbands16`, `freqbands32` | Pre-binned spectrum reductions | Fixed-size spectrum for shaders that don't want to depend on `numbands`. |
| `listeningway_spectral_centroid` | [0, 1] brightness | Color-temperature shifts; "warm vs bright" mapping. |
| `listeningway_loudness` | K-weighted (BS.1770) momentary | Perceptually-weighted intensity. Linear, not LUFS log. |
| `listeningway_phase_volume`, `phase_bass`, `phase_treble` | Energy-accumulator phase, [0, 1) | BPM-independent phase: a "loudness counter" that advances faster when the music is louder. Useful where `beat_phase` falls back to 0. |
| `listeningway_volume_history[64]` | Last 64 frames of `volume`, oldest at index 0 | Waterfall and trail effects without shader-side ring buffers. |
| `listeningway_freqbands_history[N×64]` | Per-band history, **band-major**: `[band * 64 + frame]`, frame 0 oldest | Spectrogram-grade material. |

### Minimal example

```hlsl
#include "ReShade.fxh"
#include "ListeningwayUniforms.fxh"

// AGC-aware bass intensity, decoupled from absolute volume.
float bass_pulse = saturate((Listeningway_BassNorm - 0.8) * 2.0);
float beat_flash = Listeningway_Beat;

float4 PS_AudioReactive(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 c = tex2D(ReShade::BackBuffer, uv).rgb;
    c += float3(1.0, 0.4, 0.1) * bass_pulse * 0.3;
    c += beat_flash * 0.2;
    return float4(saturate(c), 1.0);
}

technique AudioReactive {
    pass { VertexShader = PostProcessVS; PixelShader = PS_AudioReactive; }
}
```

For a richer example covering pan, the directional rose, time phase, and the full overlay layout, see [`assets/Listeningway.fx`](assets/Listeningway.fx).

---

## For addon developers

### Layout

```
src/
├── audio/
│   ├── source/    IAudioSource implementations (WASAPI / Process / Off)
│   ├── ring/      Lock-free SPSC frame ring (moodycamel wrapper)
│   ├── dsp/       IDspStage interface and 13 stages (Volume, FFT, Bands,
│   │              Equalizer, LogBoost, BandNorm, SpectralCentroid, Flux,
│   │              Beat, Chronotensity, Pan, Directional, Loudness)
│   ├── snapshot/  AudioSnapshot POD + seqlock
│   └── pipeline/  AudioSystem orchestrator + state machine
├── config/        Setting<T>, Settings, Store (JSON via nlohmann)
├── output/        shader_contract.h, UniformPublisher
├── overlay/       ImGui panels (visual + settings)
└── listeningway_addon.cpp   DllMain, ReShade event hooks
docs/adr/         Architecture decisions (read in numerical order)
templates/        Build-time substituted templates (.fxh, .rc)
tests/            GoogleTest unit + replay tests
```

The five-layer pipeline (Source → Ring → DSP → Snapshot → Consumers) is documented in [ADR-0002](docs/adr/0002-pipeline-architecture.md). Adapter usage policy in [ADR-0003](docs/adr/0003-adapter-usage-policy.md).

### Build from source

1. Run `prepare.bat` to clone the ReShade SDK at v6.3.3, clone vcpkg, install dependencies (kissfft, nlohmann-json, readerwriterqueue, gtest, rapidcheck) using the `x64-windows-static` triplet, and generate the CMake build tree.
2. Run `build.bat` to build Release, rename the DLL to `.addon`, copy it to `dist/`, and (if `LISTENINGWAY_DEPLOY_DIR` is set or the FFXIV default path exists) copy it into the game folder for live testing. Set `LISTENINGWAY_DEPLOY_DIR=...` in your environment to retarget.

Toolchain: Visual Studio 2022 (MSVC 19.x), C++20, vcpkg in manifest mode. See [ADR-0008](docs/adr/0008-language-and-dependencies.md) for the full pinning rationale.

### Configuration schema (`Listeningway.json`)

Lives next to the `.addon` file. Written by the overlay's Save button; missing fields fall back to defaults; unknown fields are preserved on round-trip. **v1 configurations are not migrated**: the schema is a clean reset (ADR-0001). Trimmed example:

```jsonc
{
  "schema_version": 1,
  "audio": {
    "analysis_enabled": true,
    "capture_source_code": "system",   // "system" | "process" | "off"
    "simd_enabled": true,              // reserved for v1.5 xsimd path
    "pan_smoothing": 0.1,
    "pan_offset": 0.0
  },
  "frequency": {
    "band_count": 64,
    "fft_size": 2048,
    "band_scale": "Mel",               // "Linear" | "Log" | "Mel"
    "log_strength": 0.10,
    "band_norm": 0.10,
    "min_freq": 30.0,
    "max_freq": 22050.0,
    "equalizer_bands": [1.11, 1.29, 2.11, 1.80, 1.63],
    "equalizer_width": 0.15,
    "amplifier_volume": 1.0,
    "amplifier_bands": 1.0,
    "amplifier_direction": 1.0
  },
  "beat": {
    "algorithm": 1,                    // 0 = simple-energy, 1 = autocorrelation + PLL
    "profile": "general",              // "general" | "edm" | "acoustic" | "custom"
    "threshold_lambda": 0.10,
    "threshold_window_ms": 60.0,
    "refractory_ms": 50.0,
    "phase_kp": 0.15,
    "phase_ki": 0.01,
    "tempo_prior_bpm": 120.0,
    "tempo_prior_sigma": 0.7,
    "tempo_window_sec": 8.0,
    "beat_decay_per_sec": 1.2
  },
  "agc": {
    "window_seconds": 5.0,
    "clamp_max": 4.0,
    "att_attack_ms": 50.0,
    "att_release_ms": 200.0
  },
  "chronotensity": { "gain_volume": 0.5, "gain_bass": 0.5, "gain_treble": 0.5 },
  "loudness":      { "window_ms": 400.0 },
  "debug":         { "debug_logging": false, "overlay_enabled": false }
}
```

Settings flow through `Setting<T>` declarations (default, min, max, persistence key, tooltip). See [ADR-0004](docs/adr/0004-configuration-strategy.md).

### Compatible shader collections

| Collection | Author | Description |
|---|---|---|
| **AS-StageFX** | Leon Aquitaine | Stage and concert visual effects: light beams, strobes, atmosphere. [Repo](https://github.com/LeonAquitaine/as-stagefx) |

Want to add yours? Open a pull request.

### Dependencies and credits

| Library / API | Author | Purpose |
|---|---|---|
| [ReShade](https://github.com/crosire/reshade) | crosire | Core framework and SDK |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut | Overlay GUI |
| [KissFFT](https://github.com/mborgerding/kissfft) | Mark Borgerding | FFT engine |
| [nlohmann/json](https://github.com/nlohmann/json) | Niels Lohmann | JSON marshalling |
| [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) | Cameron Desrochers | Lock-free SPSC |
| [GoogleTest](https://github.com/google/googletest) | Google | Unit tests |
| [rapidcheck](https://github.com/emil-e/rapidcheck) | Emil Eriksson | Property tests |
| [Microsoft WASAPI](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi/wasapi-api) | Microsoft | Windows audio capture |

All linked statically. No extra DLLs ship beside the `.addon`.

---

## Feedback

Bug reports, ideas, and pull requests are welcome on [GitHub](https://github.com/gposingway/Listeningway). If you make something with it, share it.
