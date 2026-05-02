<div align="center">

# Listeningway

**Real-time audio analysis for ReShade shaders, OSC, and OpenRGB.**

![Listeningway Showcase](https://github.com/user-attachments/assets/8a11d6b6-bdea-4c31-9614-dbfb7ad8819f)

[<img src="https://github.com/user-attachments/assets/20794810-9e43-4167-bb0e-faf46275186e">](https://github.com/gposingway/Listeningway/releases/latest)

</div>

---

Listeningway is a ReShade addon that listens to system or per-game audio, runs analysis on a dedicated DSP thread, and publishes the results to anything that wants to react: ReShade shaders as annotation-bound uniforms, generative-art and VJ tools over OSC, and RGB peripherals through OpenRGB. One audio capture, three output channels, all toggleable from the in-game overlay.

> v2.x is in beta. Shader uniform names are committed against breakage; OSC and OpenRGB outputs landed in 2.0.0-beta.2. Coming from v1? The v1 uniforms still work; new ones are additive. See [CHANGELOG.md](CHANGELOG.md) for the migration notes.

---

## Quick start

You need ReShade 6.3.3 or newer (API 14+) on Windows 10 or 11. AuroraShade R10 is compatible.

1. Download the latest release from [the releases page](https://github.com/gposingway/Listeningway/releases/latest). The ZIP contains `Listeningway.addon`, `Listeningway.fx`, and `ListeningwayUniforms.fxh`.
2. Drop the files in:

   | File | Where | Why |
   |---|---|---|
   | `Listeningway.addon` | Game folder, next to your ReShade DLL (`dxgi.dll`, `d3d11.dll`, ...). Not inside `reshade-shaders`. | ReShade loads `.addon` files from the same folder as the DLL. |
   | `Listeningway.fx` | `reshade-shaders\Shaders\` | Reference shader showing all uniforms in use. |
   | `ListeningwayUniforms.fxh` | `reshade-shaders\Shaders\` | Header to `#include` from your own shaders. |

3. Launch the game and open the ReShade overlay. The **Listeningway** panel appears. Pick a source from the dropdown, then enable `Listeningway.fx` to see it react.

That's the whole setup for shader use. OSC and OpenRGB integrations are off by default; flip them on in the overlay's **Integrations** section when you want them.

---

## What you can do with it

### Drive ReShade shaders

Include the generated header and read the uniforms you want.

```hlsl
#include "ReShade.fxh"
#include "ListeningwayUniforms.fxh"

float4 PS_AudioReactive(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float bass_pulse = saturate((Listeningway_BassNorm - 0.8) * 2.0);
    float beat_flash = Listeningway_Beat;

    float3 c = tex2D(ReShade::BackBuffer, uv).rgb;
    c += float3(1.0, 0.4, 0.1) * bass_pulse * 0.3;
    c += beat_flash * 0.2;
    return float4(saturate(c), 1.0);
}

technique AudioReactive {
    pass { VertexShader = PostProcessVS; PixelShader = PS_AudioReactive; }
}
```

A few uniforms worth knowing:

| Uniform | What it is | When you'd reach for it |
|---|---|---|
| `listeningway_volume_norm` | AGC-normalized energy. 1.0 = recent average. | Replaces per-effect "sensitivity" sliders. Reacts the same way to loud and quiet music. |
| `listeningway_volume_att` | Smoothed `volume_norm` (asymmetric attack/release) | When you want the AGC value but not the jitter. |
| `listeningway_bass_norm`, `mid_norm`, `treb_norm` | AGC-normalized macro bands | "Bass kick" or "treble glitter" effects without genre-specific tuning. |
| `listeningway_freqbands16`, `freqbands32` | Pre-binned spectrum reductions | Fixed-size spectrum for shaders that don't want to depend on `numbands`. |
| `listeningway_spectral_centroid` | [0, 1] brightness | Color-temperature shifts; "warm vs bright" mapping. |
| `listeningway_loudness` | K-weighted (BS.1770) momentary loudness | Perceptually-weighted intensity. Linear, not LUFS log. |
| `listeningway_phase_volume`, `phase_bass`, `phase_treble` | Energy-accumulator phase, [0, 1) | BPM-independent phase: a "loudness counter" that advances faster when the music is louder. Useful where `beat_phase` falls back to 0. |
| `listeningway_volume_history[64]` | Last 64 frames of `volume`, oldest at index 0 | Waterfall and trail effects without shader-side ring buffers. |
| `listeningway_freqbands_history[N×64]` | Per-band history, **band-major**: `[band * 64 + frame]`, frame 0 oldest | Spectrogram-grade material. |

The full uniform registry, including stability tier (Stable vs Experimental), lives in [STABILITY.md](STABILITY.md). That document is the public API contract.

### Send to TouchDesigner, Resolume, Max/MSP, vvvv, ...

Flip the **OSC** toggle in the overlay's **Integrations** section. The default destination is `127.0.0.1:9000` (TouchDesigner's default OSC In port). OSC addresses mirror the shader uniforms under a `/listeningway/` prefix; for example `/listeningway/volume`, `/listeningway/freqbands`, `/listeningway/beat`.

To verify the stream, run the bundled receiver:

```
python samples/osc_receiver.py
```

It listens on `127.0.0.1:9000` and prints every message it sees.

Full address schema, settings, integration recipes for popular hosts, and limitations are in **[docs/osc.md](docs/osc.md)**.

### Drive RGB peripherals through OpenRGB

Install [OpenRGB](https://openrgb.org), enable its SDK server (**Settings → SDK Server → Enable Server**), and flip the **OpenRGB** toggle in the overlay. Default destination `127.0.0.1:6742`.

The default mapping paints all LEDs as a spectrum-driven gradient (bass → blue, treble → red), modulated by AGC volume and beat flash. It's opinionated rather than configurable in v1, so plugging in a new device works out of the box.

Prerequisites, the full mapping math, failure-mode behavior, and limitations are in **[docs/openrgb.md](docs/openrgb.md)**.

---

## Pick an audio source

The overlay has a single **Audio Source** dropdown:

- **System Audio (WASAPI Loopback)** is the default. Captures everything you hear on the default playback device. Works on Windows 10 and 11.
- **Game Audio Only (Process Loopback)** captures the host process only, so Discord, browsers, and music apps stop bleeding into the visualization. Requires Windows 10 22H2 (build 20348+) or Windows 11; the option is grayed on older builds. Design notes in [ADR-0009](docs/adr/0009-process-audio-source.md).
- **None (Off)** disables analysis cleanly; uniforms zero out, OSC stops sending, OpenRGB goes idle.

You can change the source at any time from the overlay.

---

## Configure

The overlay is the primary UI. Each section has a Settings disclosure on the right; engineer-only knobs hide behind an Advanced sub-disclosure inside it. Save with the **Save** button at the bottom; settings persist to `Listeningway.json` next to the addon. Editing the JSON by hand is fine; restart the game to apply.

Settings are declared once with default, min, max, JSON key, and tooltip via `Setting<T>`. UI sliders, validation clamp, and persistence all read from the same declaration, so `frequency.logStrength` (and the rest) can't drift across UI / load / save the way v1 sometimes allowed. See [ADR-0004](docs/adr/0004-configuration-strategy.md).

Trimmed `Listeningway.json` example:

```jsonc
{
  "schema_version": 1,
  "audio": {
    "analysis_enabled": true,
    "capture_source_code": "system",  // "system" | "process" | "off"
    "pan_smoothing": 0.1
  },
  "frequency": {
    "band_count": 64,
    "fft_size": 2048,
    "band_scale": "Mel",              // "Linear" | "Log" | "Mel"
    "log_strength": 0.10,
    "min_freq": 30.0,
    "max_freq": 22050.0,
    "equalizer_bands": [1.11, 1.29, 2.11, 1.80, 1.63]
  },
  "beat": {
    "algorithm": 1,                   // 0 = simple-energy, 1 = autocorrelation + PLL
    "profile": "general",
    "tempo_prior_bpm": 120.0
  },
  "agc":      { "window_seconds": 5.0, "clamp_max": 4.0 },
  "loudness": { "window_ms": 400.0 },
  "network": {
    "osc":     { "enabled": false, "host": "127.0.0.1", "port": 9000, "rate_hz": 60 },
    "openrgb": { "enabled": false, "host": "127.0.0.1", "port": 6742, "rate_hz": 30 }
  }
}
```

V1 configurations are not migrated; v2 writes a fresh file with v2 defaults on first run.

---

## Compatible shader collections

| Collection | Author | Description |
|---|---|---|
| **AS-StageFX** | Leon Aquitaine | Stage and concert visual effects: light beams, strobes, atmosphere. [Repo](https://github.com/LeonAquitaine/as-stagefx) |

If you maintain a shader pack that consumes Listeningway uniforms, send a pull request to add it.

---

## Hacking on Listeningway

If you want to add a new audio source, DSP stage, output consumer, or shader uniform, start with [CONTRIBUTING.md](CONTRIBUTING.md) for the source layout, build, and code style. Architectural rationale lives in [`docs/adr/`](docs/adr/); read in numerical order if you're touching anything cross-cutting. The five-layer pipeline (Source → Ring → DSP → Snapshot → Consumers) is in [ADR-0002](docs/adr/0002-pipeline-architecture.md); the adapter usage policy in [ADR-0003](docs/adr/0003-adapter-usage-policy.md); the `IOutputConsumer` abstraction in [ADR-0010](docs/adr/0010-network-outputs.md).

---

## Credits

The wire-layer libraries for the OSC and OpenRGB consumers are vendored under [`third_party/`](third_party/) with their licenses and full attribution. They're the reason those integrations exist:

- **OSC**: [`mhroth/tinyosc`](https://github.com/mhroth/tinyosc) by **Martin Roth**, ISC-licensed. Two-file C99 OSC encoder; we use the encoder side and bring our own Winsock plumbing. Detail in [`third_party/tinyosc/ATTRIBUTION.md`](third_party/tinyosc/ATTRIBUTION.md).
- **OpenRGB**: [`Youda008/OpenRGB-cppSDK`](https://github.com/Youda008/OpenRGB-cppSDK) by **Jan Broz**, MIT-licensed. The serious C++ client for the OpenRGB protocol; handles the version negotiation correctly so we don't have to. Builds on Youda008's [`CppUtils-Essential`](https://github.com/Youda008/CppUtils-Essential) and [`CppUtils-Network`](https://github.com/Youda008/CppUtils-Network) libraries (also MIT). Detail in [`third_party/Youda008-OpenRGB-cppSDK/ATTRIBUTION.md`](third_party/Youda008-OpenRGB-cppSDK/ATTRIBUTION.md).

The rest of the dependency stack:

| Library / API | Author | Purpose |
|---|---|---|
| [ReShade](https://github.com/crosire/reshade) | crosire | Core framework and SDK |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut | Overlay GUI |
| [KissFFT](https://github.com/mborgerding/kissfft) | Mark Borgerding | FFT engine |
| [nlohmann/json](https://github.com/nlohmann/json) | Niels Lohmann | JSON marshalling |
| [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) | Cameron Desrochers | Lock-free SPSC ring |
| [GoogleTest](https://github.com/google/googletest) | Google | Unit tests |
| [rapidcheck](https://github.com/emil-e/rapidcheck) | Emil Eriksson | Property tests |
| [Microsoft WASAPI](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi/wasapi-api) | Microsoft | Windows audio capture |

All linked statically. No extra DLLs ship beside the `.addon`.

---

## Feedback

Bug reports, ideas, and pull requests are welcome on [GitHub](https://github.com/gposingway/Listeningway). If you make something with it, share it.
