// ---------------------------------------------
// Settings ↔ JSON marshalling via nlohmann's intrusive macros (ADR-0004).
// One macro per sub-struct lists the field names; nlohmann generates
// `to_json` / `from_json` automatically. Adding a field = one line per
// place. _WITH_DEFAULT means missing fields fall through to the default-
// constructed value of the struct.
// ---------------------------------------------
#pragma once

#include <nlohmann/json.hpp>

#include "settings.h"

namespace lw::config {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AudioConfig,
    analysis_enabled, capture_source_code, simd_enabled,
    pan_smoothing, pan_offset)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BeatConfig,
    algorithm, profile,
    threshold_lambda, threshold_window_ms, refractory_ms,
    phase_kp, phase_ki,
    tempo_prior_bpm, tempo_prior_sigma, tempo_window_sec,
    beat_decay_per_sec)

NLOHMANN_JSON_SERIALIZE_ENUM(FrequencyConfig::BandScale, {
    {FrequencyConfig::BandScale::Linear, "linear"},
    {FrequencyConfig::BandScale::Log,    "log"},
    {FrequencyConfig::BandScale::Mel,    "mel"},
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FrequencyConfig,
    band_count, fft_size, band_scale, log_strength, band_norm,
    min_freq, max_freq,
    equalizer_bands, equalizer_width,
    amplifier_volume, amplifier_bands, amplifier_direction)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AgcConfig,
    window_seconds, clamp_max, att_attack_ms, att_release_ms)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChronotensityConfig,
    gain_volume, gain_bass, gain_treble)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LoudnessConfig, window_ms)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DebugConfig,
    debug_logging, overlay_enabled)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OscConfig,
    enabled, host, port, rate_hz)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OpenRgbConfig,
    enabled, host, port, rate_hz, brightness)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(NetworkConfig, osc, openrgb)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings,
    schema_version,
    audio, beat, frequency, agc, chronotensity, loudness, debug, network)

}  // namespace lw::config
