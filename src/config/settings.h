// ---------------------------------------------
// Settings — the v2 configuration value type (ADR-0004)
//
// One struct, organized into sub-structs by concern, marshalled to/from
// JSON via nlohmann's intrusive macros. The Store class (settings_store.h)
// handles persistence and version-counter publication.
// ---------------------------------------------
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace lw::config {

/// Schema version. Bumped when on-disk schema changes; old loaders dispatch
/// by version. v1 ships with version 1; v2 is a clean break (ADR-0001), so
/// v1 configs are not migrated.
inline constexpr int kCurrentSchemaVersion = 1;

/// Audio capture-related tunables.
struct AudioConfig {
    bool        analysis_enabled    = true;
    std::string capture_source_code = "system";  // matches IAudioSource::Info::code
    bool        simd_enabled        = true;       ///< SSE/AVX in DSP stages

    // Pan stage tunables
    float pan_smoothing = 0.1f;   ///< 0 = none, 1 = max smoothing
    float pan_offset    = 0.0f;   ///< user pan bias, [-1, +1]
};

/// Beat / tempo detection tunables.
///
/// Single user-facing knob. The detector inside (multi-band onset
/// aggregation with adaptive thresholds + smooth pulse curve + tempo
/// autocorrelation as instrumentation) is fully auto-tuned from the
/// running AGC window and per-band flux baselines. `pulse_strength`
/// biases the adaptive threshold so users can dial visual reactivity
/// up or down without thinking about gains, refractory windows, or
/// PLL constants.
struct BeatConfig {
    /// 0 = no triggers, 1 = balanced default, > 1 = more reactive.
    /// Internally maps to an inverse threshold multiplier on the
    /// per-band sigma-above-baseline onset criterion.
    float pulse_strength = 1.0f;
};

/// Frequency-domain tunables.
struct FrequencyConfig {
    int    band_count    = 64;        ///< runtime; ≤ kMaxBands
    int    fft_size      = 2048;      ///< must be power of two
    enum class BandScale { Linear, Log, Mel };
    BandScale band_scale = BandScale::Mel;  ///< Slaney mel by default
    float  log_strength  = 0.10f;     ///< log-boost stage gain curve
    float  band_norm     = 0.10f;     ///< raw-magnitude → band amplitude scaling
    float  min_freq      = 30.0f;     ///< band coverage lower bound
    float  max_freq      = 22050.0f;  ///< band coverage upper bound

    // 5-band equalizer (Bass / LowMid / Mid / HighMid / Treble),
    // applied as Gaussian-weighted contributions per band index.
    std::array<float, 5> equalizer_bands { 1.11f, 1.29f, 2.11f, 1.80f, 1.63f };
    float equalizer_width = 0.15f;

    // Per-uniform amplifiers (visual-only, do not affect analysis).
    float amplifier_volume    = 1.0f;
    float amplifier_bands     = 1.0f;
    float amplifier_direction = 1.0f;

    // Directional stage tunables. spatial_spread controls how much each
    // channel's energy bleeds into its two neighbouring direction buckets
    // (0 = sharp peaks per channel, 0.5 = soft glow). spatial_smoothing is
    // a per-frame EMA on the direction8 vector to calm flicker on
    // percussive content (0 = no smoothing, → 1 = heavily smoothed).
    float spatial_spread    = 0.25f;
    float spatial_smoothing = 0.10f;
};

/// AGC normalization (running-mean ratios).
struct AgcConfig {
    float window_seconds = 5.0f;   ///< exponential-moving-mean window
    float clamp_max      = 4.0f;   ///< cap on the normalized value
    float att_attack_ms  = 50.0f;  ///< for the *_att smoothed siblings
    float att_release_ms = 200.0f;
};

/// Chronotensity phase accumulator (accumulates per-band energy modulo 1.0).
struct ChronotensityConfig {
    float gain_volume = 0.5f;  // accumulator rate per "average" energy
    float gain_bass   = 0.5f;
    float gain_treble = 0.5f;
};

/// K-weighted loudness (BS.1770) tunables.
struct LoudnessConfig {
    float window_ms = 400.0f;  ///< momentary integration window
};

/// Debug toggles.
struct DebugConfig {
    bool debug_logging   = false;
    bool overlay_enabled = false;
};

/// Overlay/UI viewing preferences. Visual-only; doesn't affect analysis or
/// any uniform output.
struct UiConfig {
    enum class SpectrumOrientation { Vertical, Horizontal };
    SpectrumOrientation spectrum_orientation = SpectrumOrientation::Horizontal;
};

/// OSC sender (Open Sound Control). Off by default; on enable, sends
/// messages mirroring the shader uniform names ("/listeningway/volume",
/// "/listeningway/freqbands", ...) to host:port at rate_hz.
/// Send-only — no port is opened on this machine.
struct OscConfig {
    bool        enabled = false;
    std::string host    = "127.0.0.1";
    int         port    = 9000;        ///< TouchDesigner default
    int         rate_hz = 60;          ///< clamp 1..120
};

/// OpenRGB client. Off by default; on enable, connects as a TCP client to
/// an OpenRGB server (typically running on the local machine), enumerates
/// controllers, and pushes per-LED frames at rate_hz. Listening port is
/// NOT opened on this machine — we are the client.
struct OpenRgbConfig {
    bool        enabled    = false;
    std::string host       = "127.0.0.1";
    int         port       = 6742;     ///< OpenRGB server default
    int         rate_hz    = 30;       ///< clamp 5..60 (server has wake-up issues above 60)
    float       brightness = 1.0f;     ///< 0..1 global multiplier
};

/// Network outputs umbrella.
struct NetworkConfig {
    OscConfig     osc;
    OpenRgbConfig openrgb;
};

/// Top-level settings POD. Every persisted tunable lives here. Runtime state
/// (sample rate detected at capture, current beat detector state, etc.) does
/// NOT live here.
struct Settings {
    int schema_version = kCurrentSchemaVersion;

    AudioConfig         audio;
    BeatConfig          beat;
    FrequencyConfig     frequency;
    AgcConfig           agc;
    ChronotensityConfig chronotensity;
    LoudnessConfig      loudness;
    DebugConfig         debug;
    NetworkConfig       network;
    UiConfig            ui;
};

}  // namespace lw::config
