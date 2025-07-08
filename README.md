<div align="center">

# Listeningway

**Real-time audio visualization for ReShade shaders**

![Listeningway Showcase](https://github.com/user-attachments/assets/8a11d6b6-bdea-4c31-9614-dbfb7ad8819f)

[<img src="https://github.com/user-attachments/assets/20794810-9e43-4167-bb0e-faf46275186e">](https://github.com/gposingway/Listeningway/releases/latest)






</div>

---

Listeningway listens to your system's audio, analyzes it live, and exposes data like volume, frequency bands, and beat detection directly to your `.fx` files.

## For End Users: Get Started!

Let's add audio reactivity to your existing ReShade presets or try out effects designed for Listeningway! Getting started is super easy:

**What You'll Need (The Recipe):**

* **ReShade:** Version 6.3.3 or newer (API 14+). Using older versions (such as 5.2.0) is not supported and may cause crashes. If you are building from source, ensure you use the `v6.3.3` tag of the ReShade repository. AuroraShade R10 (based on ReShade 6.3.3) is also compatible.
* **Windows:** Version 10 or 11 (required for the WASAPI audio capture magic).

**Installation:**

1.  **Download the Release ZIP:** Head over to the **Latest Release page** on GitHub. Find the main release archive file (usually named something like `Listeningway-vX.Y.Z.zip`) in the 'Assets' section and download it.
    * [**Go to Latest Listeningway Release**](https://github.com/gposingway/Listeningway/releases/latest)
2.  **Extract the ZIP:** Unzip the downloaded archive file to a temporary location on your computer using a tool like 7-Zip or Windows Explorer. Inside the extracted folder, you should find files like `Listeningway.addon`, `Listeningway.fx`, and `ListeningwayUniforms.fxh`.
3.  **Place the Extracted Files:** Now, move these extracted files to their correct final destinations. Use this table as your guide:

    | File Name                  | Where to Place It                                                                | Notes / Purpose                                  |
    | :------------------------- | :------------------------------------------------------------------------------- | :----------------------------------------------- |
    | `Listeningway.addon`       | FFXIV Game Directory (Same folder as `ffxiv_dx11.exe` & ReShade DLL `dxgi.dll`) | ReShade loads `.addon` files from this directory. |
    | `Listeningway.fx`          | Main ReShade Shaders Folder (e.g., `...\reshade-shaders\Shaders\`)               | The example shader effect file.                  |
    | `ListeningwayUniforms.fxh` | Main ReShade Shaders Folder (e.g., `...\reshade-shaders\Shaders\`)               | Include file needed by shaders using `#include`.   |

    *(**Important Reminder:** The `.addon` file goes directly into your main game folder with the ReShade DLL, **not** inside `reshade-shaders`!)*

4.  **Test Drive**
    * Launch your game! The addon should load automatically if placed correctly (you might see a message about it in the ReShade log/startup banner).
    * Open the ReShade menu, find and enable the `Listeningway.fx` effect in your shader list to see it react!

**Tuning (Optional):**

* For most users, the default settings work great! If you want to fine-tune the audio analysis (like how sensitive beat detection is), you can use the built-in overlay UI (accessible through the ReShade menu) or edit the `Listeningway.ini` file located in the same directory as the .addon file. More details on this in the section for developers below.
* **New in 1.2.0.0:** The overlay UI now features an **Amplifier** slider. This setting multiplies all overlay visualizations and Listeningway_* uniforms (volume, beat, frequency bands, left/right volume) for enhanced visual feedback, but does not affect the underlying audio analysis.

<div align="center">

## Compatible Shader Collections

</div>

These shader collections are specifically designed to work with Listeningway's audio reactivity features:

<table>
  <tr>
    <th align="left">Collection</th>
    <th align="left">Author</th>
    <th align="left">Description</th>
    <th align="left">Link</th>
  </tr>
  <tr>
    <td><strong>AS-StageFX</strong></td>
    <td>Leon Aquitaine</td>
    <td>A collection of stage/concert-like visual effects that react to music. Includes various light beams, strobes, and atmospheric effects.</td>
    <td><a href="https://github.com/LeonAquitaine/as-stagefx">GitHub Repository</a></td>
  </tr>
</table>

Want to add your shader collection to this list? Create a pull request with your compatible shaders!

---

## For Shader Creators

Want to make your *own* effects react to audio? Listeningway makes it quite simple by exposing audio data as `uniform` variables.

**How to Use the Uniforms:**

You *must* use annotation-based uniforms to access the data. This is robust and avoids potential conflicts. Simply declare a uniform in your shader with the `source` annotation pointing to the Listeningway data you want:

```hlsl
// In your .fx file: Define uniforms using the 'source' annotation

// Example: Get overall volume
uniform float MyAwesomeVolume < source = "listeningway_volume"; >;

// Example: Get the 32 frequency bands (0=bass -> 31=treble)
uniform float MyCoolFreqBands[32] < source = "listeningway_freqbands"; >;

// Example: Get the beat trigger (1.0 on beat, fades down)
uniform float MyFunkyBeat < source = "listeningway_beat"; >;

// Example: Get elapsed time
uniform float MyTime < source = "listeningway_timeseconds"; >;
```

**Tip:** For convenience, you can `#include "ListeningwayUniforms.fxh"` (make sure you placed it in `reshade-shaders\Shaders\` as per installation steps) which contains pre-defined declarations for all available Listeningway uniforms.

**Available Uniforms:**

Here's the data Listeningway provides:

<table>
  <tr>
    <th align="left">Uniform</th>
    <th align="left">Description</th>
    <th align="left">Value Range</th>
  </tr>
  <tr>
    <td><strong>Listeningway_Volume</strong></td>
    <td>Current overall audio volume (normalized, good for intensity/brightness).</td>
    <td>0.0 to 1.0</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_Volume &lt; source="listeningway_volume"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_FreqBands</strong></td>
    <td>Amplitude of 32 frequency bands (Index 0 = Low Bass ... Index 31 = High Treble). Great for spectrum visualizations or driving different effects based on frequency.</td>
    <td>0.0 to 1.0 (per band)</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_FreqBands[32] &lt; source="listeningway_freqbands"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_Beat</strong></td>
    <td>Beat detection value. Typically pulses to 1.0 on a detected beat and then quickly falls off. Perfect for triggering flashes or movements.</td>
    <td>0.0 to 1.0</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_Beat &lt; source="listeningway_beat"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_TimeSeconds</strong></td>
    <td>Time elapsed (in seconds) since the addon started. Useful for continuous animations.</td>
    <td>0.0 to ∞</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_TimeSeconds &lt; source="listeningway_timeseconds"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_TimePhase60Hz</strong></td>
    <td>Phase (0.0 to 1.0) cycling at 60Hz. Good for smooth, fast oscillations.</td>
    <td>0.0 to 1.0 (cycling)</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_TimePhase60Hz &lt; source="listeningway_timephase60hz"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_TimePhase120Hz</strong></td>
    <td>Phase (0.0 to 1.0) cycling at 120Hz.</td>
    <td>0.0 to 1.0 (cycling)</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_TimePhase120Hz &lt; source="listeningway_timephase120hz"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_TotalPhases60Hz</strong></td>
    <td>Total number of 60Hz cycles elapsed (float).</td>
    <td>0.0 to ∞</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_TotalPhases60Hz &lt; source="listeningway_totalphases60hz"; &gt;;</code><br/><br/></td>
  </tr>  <tr>
    <td><strong>Listeningway_TotalPhases120Hz</strong></td>
    <td>Total number of 120Hz cycles elapsed (float).</td>
    <td>0.0 to ∞</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_TotalPhases120Hz &lt; source="listeningway_totalphases120hz"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_VolumeLeft</strong></td>
    <td>Volume level for left audio channels (0.0 to 1.0).</td>
    <td>0.0 to 1.0</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_VolumeLeft &lt; source="listeningway_volumeleft"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_VolumeRight</strong></td>
    <td>Volume level for right audio channels (0.0 to 1.0).</td>
    <td>0.0 to 1.0</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_VolumeRight &lt; source="listeningway_volumeright"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_AudioPan</strong></td>
    <td>Stereo pan position (-1.0 = full left, 0.0 = center, +1.0 = full right). Enables positional audio effects.</td>
    <td>-1.0 to +1.0</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_AudioPan &lt; source="listeningway_audiopan"; &gt;;</code><br/><br/></td>
  </tr>
  <tr>
    <td><strong>Listeningway_AudioFormat</strong></td>
    <td>Detected audio format (0.0=none, 1.0=mono, 2.0=stereo, 6.0=5.1 surround, 8.0=7.1 surround). Useful for format-specific effects.</td>
    <td>0.0, 1.0, 2.0, 6.0, 8.0</td>
  </tr>
  <tr>
    <td colspan="3"><code>uniform float Listeningway_AudioFormat &lt; source="listeningway_audioformat"; &gt;;</code><br/><br/></td>
  </tr>
</table>

**Tip:** For convenience, you can `#include "ListeningwayUniforms.fxh"` which contains all these declarations ready to use.

**Example Shader Snippet:**

Here’s a super basic example of using some uniforms in your shader's main pixel shader function:

```hlsl
// Make sure uniforms are declared above (or use the include)

float4 PS_MyAudioReactiveEffect(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    // Enhanced example: Use stereo spatialization for more dynamic effects
    float bass_intensity = Listeningway_FreqBands[0] * 0.5;  // Bass contribution
    float overall_volume = Listeningway_Volume * 0.5;        // Overall volume
    float beat_flash = Listeningway_Beat * 1.0;              // Beat flash effect
    
    // NEW: Use stereo information for spatial effects
    float left_volume = Listeningway_VolumeLeft;
    float right_volume = Listeningway_VolumeRight;
    float pan_position = Listeningway_AudioPan; // -1.0 (left) to +1.0 (right)
    
    // Create a stereo-aware color effect
    float3 color = tex2D(ReShade::BackBuffer, uv).rgb;
    
    // Apply different colors based on stereo pan
    if (pan_position < -0.1) {
        // Left-heavy audio: Blue tint
        color = lerp(color, float3(0.2, 0.4, 1.0), left_volume * 0.3);
    } else if (pan_position > 0.1) {
        // Right-heavy audio: Red tint
        color = lerp(color, float3(1.0, 0.2, 0.4), right_volume * 0.3);
    } else {
        // Centered audio: Green tint
        color = lerp(color, float3(0.2, 1.0, 0.4), overall_volume * 0.3);
    }
    
    // Add beat flash that respects stereo positioning
    float beat_contribution = beat_flash * (0.5 + abs(pan_position) * 0.5);
    color += float3(1.0, 1.0, 1.0) * beat_contribution * 0.2;

    return float4(saturate(color), 1.0);
}

technique MyAudioReactiveEffect {
    pass {
        VertexShader = PostProcessVS;
        PixelShader = PS_MyAudioReactiveEffect;
    }
}
```

Now go make something awesome\! ✨

-----

## For Addon Developers

<div align="center">

### Building from Source

</div>

It's streamlined with batch scripts\!

1.  **Prepare Dependencies:**
      * Open a command prompt in the project root.
      * Run: `.\prepare.bat`
      * *(This clones the ReShade SDK & vcpkg, installs dependencies via vcpkg - grab a coffee\!)*
2.  **Build:**
      * Run: `.\build.bat`
      * *(This builds in Release mode, renames the DLL to `.addon`, and copies it to `dist\`)*

<div align="center">

### Configuration Deep Dive

</div>

All tunable parameters—such as FFT size, number of bands, smoothing factors, beat detection thresholds, and overlay/visualization options—are stored in `Listeningway.json`, located in the same directory as the ReShade `.addon` file. Changes made through the overlay UI are saved automatically to this file. If you edit the JSON manually, restart your game or ReShade to apply changes. Any missing settings will fall back to sensible defaults.

**Example `Listeningway.json` (as of June 2025):**

```json
{
  "audio": {
    "analysisEnabled": true,
    "captureProviderCode": "system",
    "panSmoothing": 0.0
  },
  "beat": {
    "algorithm": 1,
    "falloffDefault": 2.0,
    "timeScale": 0.000000001,
    "timeInitial": 0.5,
    "timeMin": 0.05,
    "timeDivisor": 0.1,
    "spectralFluxThreshold": 0.05,
    "spectralFluxDecayMultiplier": 2.0,
    "tempoChangeThreshold": 0.25,
    "beatInductionWindow": 0.10,
    "octaveErrorWeight": 0.60,
    "minFreq": 0.0,
    "maxFreq": 400.0,
    "fluxLowAlpha": 0.35,
    "fluxLowThresholdMultiplier": 2.0
  },
  "frequency": {
    "logScaleEnabled": true,
    "logStrength": 0.5,
    "minFreq": 80.0,
    "maxFreq": 13000.0,
    "equalizerBands": [1.0, 1.5, 2.0, 2.5, 3.0],
    "equalizerWidth": 1.5,
    "amplifier": 1.0,
    "bands": 32,
    "fftSize": 512,
    "bandNorm": 0.1
  },
  "debug": {
    "debugEnabled": false,
    "overlayEnabled": true
  }
}
```

**Overlay UI:**

The overlay UI (open via the ReShade menu) allows real-time adjustment of all major settings, including:
- Volume normalization, band normalization, pan smoothing, and more

All changes made in the overlay UI are saved back to `Listeningway.json` atomically.

**Band-Limited Beat Detection:**

Listeningway features band-limited spectral flux detection for more accurate beat detection, especially in music with strong bass beats like electronic, hip-hop, and rock. This feature focuses the beat detection on low frequencies (by default 0-400Hz) where kick drums and bass hits typically occur, making it less sensitive to other sounds like vocals, synths, or high-frequency percussion.

You can fine-tune this feature through the overlay UI (available in the ReShade menu) or directly through these settings in `Listeningway.json`:

```json
{
  "beat": {
    "minFreq": 0.0,
    "maxFreq": 400.0,
    "fluxLowAlpha": 0.35,
    "fluxLowThresholdMultiplier": 2.0
  }
}
```

**Tuning Tips (JSON & Overlay UI):**

You can fine-tune Listeningway's audio reactivity for your needs using the overlay UI (in the ReShade menu) or by editing `Listeningway.json` directly. All field names below match the JSON config and overlay UI labels.

**Beat Detection**
- `beat.minFreq` / `beat.maxFreq`: Restrict beat detection to a frequency range. Lower values (e.g. 20–150 Hz) focus on bass/kick drums. Defaults (0–400 Hz) work for most music. For acoustic, try 40–250 Hz.
- `beat.fluxLowThresholdMultiplier`: Lower (1.1–1.3) = more sensitive, higher (1.5–2.0) = more selective (fewer false positives).
- `beat.fluxLowAlpha`: Lower = slower adaptation to volume changes (smoother, less jitter), higher = more responsive.
- `beat.algorithm`: 0 = Simple Energy (good for strong, simple beats), 1 = Spectral Flux + Autocorrelation (better for complex rhythms).
- Advanced: `beat.spectralFluxThreshold`, `beat.spectralFluxDecayMultiplier`, `beat.tempoChangeThreshold`, `beat.beatInductionWindow`, `beat.octaveErrorWeight`—tune only if you want to experiment with advanced beat detection.

**Frequency Bands**
- `frequency.logScaleEnabled`: `true` (default) matches human hearing; `false` for linear mapping.
- `frequency.minFreq` / `frequency.maxFreq`: Set the frequency range for band analysis. Lower min or higher max makes bands more/less sensitive to certain content.
- `frequency.logStrength`: Higher = more detail in bass bands.
- `frequency.bands`: Number of bands (e.g. 32). Must match your shader's uniform array size.
- `frequency.fftSize`: FFT window size (e.g. 512). Higher = more frequency detail, but slower response.

**Amplifier**
- `frequency.amplifier`: Multiplies all overlay visualizations and Listeningway_* uniforms (volume, beat, bands, left/right volume). Use if your system/game is quiet or you want more visual punch. Does not affect underlying analysis.

**Pan Smoothing**
- `audio.panSmoothing`: 0.0 = no smoothing (fast, but jittery), 0.1–0.3 = light smoothing, 0.4–0.7 = medium, 0.8–1.0 = heavy smoothing (very stable, but slow to react).

**User Panning Adjustment**
- `audio.panOffset`: User panning adjustment, range -1.0 (full left) to +1.0 (full right), default 0.0. This value is added to the detected pan before smoothing/output. Use to compensate for system or room bias.

**5-Band Equalizer**
- `frequency.equalizerBands`: Array of 5 multipliers for low to high frequencies (e.g. `[1.0, 1.5, 2.0, 2.5, 3.0]`). Boost or cut specific ranges for more visible bass, mids, or treble.
- `frequency.equalizerWidth`: Controls how wide each band's effect is (in octaves). Higher = smoother transitions, lower = more focused boosts.

**Workflow Tips**
- Use the overlay UI for real-time feedback and tuning. All changes are saved to `Listeningway.json` automatically.
- If you edit the JSON manually, restart your game or ReShade to apply changes.
- Any missing settings in the JSON will fall back to sensible defaults.

For most users, the defaults work well! Tweak only if you want to optimize for a specific genre, visualization style, or hardware setup.

**Frequency Analysis Improvements:**
- FFT processing now uses a Hann window function to reduce spectral leakage, resulting in cleaner frequency analysis and more accurate beat detection.

**Architecture Overview:**

  * `audio_capture.*`: Handles WASAPI audio capture thread.
  * `audio_analysis.*`: Performs FFT, calculates volume, bands, beat detection.
  * `uniform_manager.*`: Manages updating shader uniforms via the ReShade API.
  * `overlay.*`: Renders the ImGui debug overlay.
  * `logging.*`: Simple thread-safe logging.
  * `listeningway_addon.cpp`: Main addon entry point, event handling, initialization.
  * `settings.*`: Loads/saves settings from `.json`, holds the `ListeningwaySettings` struct.

**Dependencies & Credits:**

| Library / API                                                                                  | Author / Project   | Purpose                              |
| :--------------------------------------------------------------------------------------------- | :----------------- | :----------------------------------- |
| [ReShade](https://github.com/crosire/reshade)                                                  | crosire            | Core framework & SDK                 |
| [ImGui](https://github.com/ocornut/imgui)                                                      | Omar Cornut        | Debug Overlay GUI                    |
| [KissFFT](https://github.com/mborgerding/kissfft)                                              | Mark Borgerding    | Fast Fourier Transform Calculation |
| [Microsoft WASAPI](https://www.google.com/search?q=https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi/wasapi-api) | Microsoft          | Windows Audio Capture                |

**Developer Notes:**

  * All dependencies (like KissFFT) are linked statically; no extra DLLs needed beside the `.addon` file.
  * Check the ReShade log file (`ReShade.log` or `d3d11.log` etc. in game dir) for any addon errors.
  * If you change the number of frequency bands (`NumBands` in `.json`), you MUST update it in `settings.h` (`DEFAULT_NUM_BANDS`) AND adjust your shader code (array sizes, uniform source annotations if needed) accordingly\! Same applies if adding new uniforms.
  * Doxygen documentation can be generated using the `Doxyfile` in `third_party/reshade`.

  HUGE thanks to the ReShade community and the creators of these libraries\!

-----

<div align="center">

## Feedback & Show Us What You Make!

</div>

Feedback, ideas, bug reports, and pull requests are very welcome over on the [GitHub Repository](https://github.com/gposingway/Listeningway)!

And most importantly – if you use Listeningway to create some cool audio-reactive shaders, **please share them!** Post screenshots or videos on GitHub Discussions, Discord, or wherever you hang out! We'd love to see your creativity in action!

<div align="center">

**Hope you have a blast making your visuals groove!**

Happy visualizing! =)

</div>
