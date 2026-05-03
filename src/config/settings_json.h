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

NLOHMANN_JSON_SERIALIZE_ENUM(BeatConfig::Mode, {
    {BeatConfig::Mode::Auto,    "auto"},
    {BeatConfig::Mode::Profile, "profile"},
    {BeatConfig::Mode::Custom,  "custom"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(BeatConfig::Profile, {
    {BeatConfig::Profile::Percussive, "percussive"},
    {BeatConfig::Profile::Melodic,    "melodic"},
    {BeatConfig::Profile::Sustained,  "sustained"},
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BeatConfig,
    mode, profile, pulse_strength)

NLOHMANN_JSON_SERIALIZE_ENUM(FrequencyConfig::BandScale, {
    {FrequencyConfig::BandScale::Linear, "linear"},
    {FrequencyConfig::BandScale::Log,    "log"},
    {FrequencyConfig::BandScale::Mel,    "mel"},
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FrequencyConfig,
    band_count, fft_size, band_scale, log_strength, band_norm,
    min_freq, max_freq,
    equalizer_bands, equalizer_width,
    amplifier_volume, amplifier_bands, amplifier_direction,
    spatial_spread, spatial_smoothing)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AgcConfig,
    window_seconds, clamp_max, att_attack_ms, att_release_ms)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChronotensityConfig,
    gain_volume, gain_bass, gain_treble)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LoudnessConfig, window_ms)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DebugConfig,
    debug_logging, overlay_enabled)

NLOHMANN_JSON_SERIALIZE_ENUM(UiConfig::SpectrumOrientation, {
    {UiConfig::SpectrumOrientation::Vertical,   "vertical"},
    {UiConfig::SpectrumOrientation::Horizontal, "horizontal"},
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UiConfig,
    spectrum_orientation)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OscConfig,
    enabled, host, port, rate_hz)

NLOHMANN_JSON_SERIALIZE_ENUM(OpenRgbConfig::SinglePattern, {
    {OpenRgbConfig::SinglePattern::BeatFlash,          "beat_flash"},
    {OpenRgbConfig::SinglePattern::VolumePulse,        "volume_pulse"},
    {OpenRgbConfig::SinglePattern::SpectralHue,        "spectral_hue"},
    {OpenRgbConfig::SinglePattern::ChronotensityCycle, "chronotensity_cycle"},
    {OpenRgbConfig::SinglePattern::Static,             "static"},
    {OpenRgbConfig::SinglePattern::Off,                "off"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(OpenRgbConfig::LinearPattern, {
    {OpenRgbConfig::LinearPattern::SpectrumBar,     "spectrum_bar"},
    {OpenRgbConfig::LinearPattern::VuMeter,         "vu_meter"},
    {OpenRgbConfig::LinearPattern::ChaseOrbit,      "chase_orbit"},
    {OpenRgbConfig::LinearPattern::PulseFromCenter, "pulse_from_center"},
    {OpenRgbConfig::LinearPattern::StereoSplit,     "stereo_split"},
    {OpenRgbConfig::LinearPattern::ColorWash,       "color_wash"},
    {OpenRgbConfig::LinearPattern::Static,          "static"},
    {OpenRgbConfig::LinearPattern::Off,             "off"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(OpenRgbConfig::MatrixPattern, {
    {OpenRgbConfig::MatrixPattern::SpatialMap,           "spatial_map"},
    {OpenRgbConfig::MatrixPattern::EqualizerColumns,     "equalizer_columns"},
    {OpenRgbConfig::MatrixPattern::PerRegion,            "per_region"},
    {OpenRgbConfig::MatrixPattern::SpectrogramWaterfall, "spectrogram_waterfall"},
    {OpenRgbConfig::MatrixPattern::BeatFlash,            "beat_flash"},
    {OpenRgbConfig::MatrixPattern::ColorWash,            "color_wash"},
    {OpenRgbConfig::MatrixPattern::Static,               "static"},
    {OpenRgbConfig::MatrixPattern::Off,                  "off"},
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OpenRgbConfig,
    enabled, host, port, rate_hz, brightness,
    pattern_single, pattern_linear, pattern_matrix)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(NetworkConfig, osc, openrgb)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings,
    schema_version,
    audio, beat, frequency, agc, chronotensity, loudness, debug, network, ui)

}  // namespace lw::config
