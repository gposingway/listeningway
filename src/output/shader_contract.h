// ---------------------------------------------
// Shader contract — uniform source-string registry (ADR-0005)
//
// Every shader uniform name is declared exactly here. The UniformPublisher
// reads from this list; ReShade uniform variables annotated with `source =
// "..."` matching one of these strings are populated each frame.
//
// All 22 v1 names are preserved verbatim. New v2 uniforms are added at the
// bottom of each section. Any change to an existing name supersedes ADR-0005
// via a new ADR — this file is a public API surface.
// ---------------------------------------------
#pragma once

#include <string_view>

namespace lw::shader_contract {

// --- v1 preserved (Stable) ----------------------------------------------
inline constexpr std::string_view kVolume          = "listeningway_volume";
inline constexpr std::string_view kVolumeLeft      = "listeningway_volumeleft";
inline constexpr std::string_view kVolumeRight     = "listeningway_volumeright";
inline constexpr std::string_view kAudioPan        = "listeningway_audiopan";
inline constexpr std::string_view kAudioFormat     = "listeningway_audioformat";

inline constexpr std::string_view kFreqBands       = "listeningway_freqbands";
inline constexpr std::string_view kNumBands        = "listeningway_numbands";
inline constexpr std::string_view kBeat            = "listeningway_beat";

inline constexpr std::string_view kTimeSeconds       = "listeningway_timeseconds";
inline constexpr std::string_view kTimePhase60Hz     = "listeningway_timephase60hz";
inline constexpr std::string_view kTimePhase120Hz    = "listeningway_timephase120hz";
inline constexpr std::string_view kTotalPhases60Hz   = "listeningway_totalphases60hz";
inline constexpr std::string_view kTotalPhases120Hz  = "listeningway_totalphases120hz";

inline constexpr std::string_view kDirection8     = "listeningway_direction8";
inline constexpr std::string_view kFront          = "listeningway_front";
inline constexpr std::string_view kFrontRight     = "listeningway_front_right";
inline constexpr std::string_view kRight          = "listeningway_right";
inline constexpr std::string_view kBackRight      = "listeningway_back_right";
inline constexpr std::string_view kBack           = "listeningway_back";
inline constexpr std::string_view kBackLeft       = "listeningway_back_left";
inline constexpr std::string_view kLeft           = "listeningway_left";
inline constexpr std::string_view kFrontLeft      = "listeningway_front_left";

// --- v2 additive (Stable: AGC pattern is the most-requested missing item) -
inline constexpr std::string_view kVolumeNorm     = "listeningway_volume_norm";
inline constexpr std::string_view kVolumeAtt      = "listeningway_volume_att";
inline constexpr std::string_view kBassNorm       = "listeningway_bass_norm";
inline constexpr std::string_view kMidNorm        = "listeningway_mid_norm";
inline constexpr std::string_view kTrebNorm       = "listeningway_treb_norm";
inline constexpr std::string_view kBassAtt        = "listeningway_bass_att";
inline constexpr std::string_view kMidAtt         = "listeningway_mid_att";
inline constexpr std::string_view kTrebAtt        = "listeningway_treb_att";

inline constexpr std::string_view kFreqBands16    = "listeningway_freqbands16";
inline constexpr std::string_view kFreqBands32    = "listeningway_freqbands32";

inline constexpr std::string_view kSpectralCentroid = "listeningway_spectral_centroid";

// --- v2 additive (Experimental, see STABILITY.md) -----------------------
inline constexpr std::string_view kLoudness       = "listeningway_loudness";
inline constexpr std::string_view kBeatPhase      = "listeningway_beat_phase";
inline constexpr std::string_view kTempoBpm       = "listeningway_tempo_bpm";
inline constexpr std::string_view kTempoConfidence = "listeningway_tempo_confidence";
inline constexpr std::string_view kPhaseVolume    = "listeningway_phase_volume";
inline constexpr std::string_view kPhaseBass      = "listeningway_phase_bass";
inline constexpr std::string_view kPhaseTreble    = "listeningway_phase_treble";
inline constexpr std::string_view kVolumeHistory  = "listeningway_volume_history";

// Native-band × 64-frame history. Layout: band-major, time-ascending
// (frame 0 = oldest, frame 63 = most recent). Band axis matches
// `listeningway_freqbands` / `listeningway_numbands` so a shader using
// freqbands today can read the history with `freqbands_history[band *
// 64 + frame]`. Shader array size is DEFAULT_NUM_BANDS * 64 (substituted
// by build.bat into the .fxh template). At runtime only the first
// `numbands * 64` entries are updated each render; the remaining tail
// is whatever was last written.
inline constexpr std::string_view kFreqBandsHistory = "listeningway_freqbands_history";

}  // namespace lw::shader_contract
