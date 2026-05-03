// ---------------------------------------------
// AudioSnapshot — the immutable POD published to consumers (ADR-0002, 0005)
//
// Trivially-copyable so the seqlock can `memcpy` the body. No std::vector,
// no heap. Fixed-size arrays for variable-length data; a `_count` companion
// for each gives the live size.
// ---------------------------------------------
#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include "../../listeningway_constants.h"  // DEFAULT_NUM_BANDS
#include "../dsp/stage_profile.h"          // StageTimings, kMaxStages

namespace lw {

/// Hard cap on the number of frequency bands the snapshot carries.
/// Settings::frequency::bands may be ≤ this; published count is in
/// `freq_band_count`. 128 covers any reasonable user configuration.
constexpr size_t kMaxBands = 128;

/// Hard cap on the volume_history ring length. Sized for ~1 second at
/// typical 60 Hz publish rate.
constexpr size_t kVolumeHistoryLength = 64;

/// Pure value type. Trivially-copyable; no member is allowed to break that.
/// The seqlock relies on this so the writer can `memcpy` and the reader can
/// validate via the seq counter without per-field atomic ops.
struct AudioSnapshot {
    // --- Time-domain ---
    float volume       = 0.0f;
    float volume_left  = 0.0f;
    float volume_right = 0.0f;
    float volume_norm  = 1.0f;  // AGC-normalized, 1.0 = average
    float volume_att   = 1.0f;  // smoothed AGC

    // --- Frequency-domain ---
    std::array<float, kMaxBands> freq_bands{};  // post-EQ, post-LogBoost
    std::array<float, kMaxBands> freq_bands_raw{};  // pre-EQ
    uint32_t freq_band_count = 0;

    // Coarser pre-binned views derived from freq_bands.
    std::array<float, 16> freq_bands_16{};
    std::array<float, 32> freq_bands_32{};

    // 3-band macro normalization (running-mean ratio).
    float bass_norm = 1.0f;
    float mid_norm  = 1.0f;
    float treb_norm = 1.0f;
    float bass_att  = 1.0f;
    float mid_att   = 1.0f;
    float treb_att  = 1.0f;

    float spectral_centroid = 0.0f;  // [0, 1] normalized to Nyquist
    float loudness          = 0.0f;  // K-weighted 400 ms momentary RMS

    // --- Event-domain (beat) ---
    float beat              = 0.0f;
    float beat_phase        = 0.0f;  // [0, 1) PLL-locked
    float tempo_bpm         = 0.0f;
    float tempo_confidence  = 0.0f;
    bool  tempo_detected    = false;

    // BeatStage's working pulse strength (whatever Auto / Profile / Custom
    // is currently using). Surfaced so the UI can seed the Custom slider
    // from Auto's converged value when the user switches modes.
    float beat_pulse_strength = 1.0f;
    // True when the Auto adapter has been stable for ~3 s. Drives the
    // "Adapting…" / "Locked" status badge in the overlay.
    bool  beat_auto_locked    = false;

    // --- Chronotensity (energy-accumulated phases, robust where BPM is not) ---
    float phase_volume  = 0.0f;
    float phase_bass    = 0.0f;
    float phase_treble  = 0.0f;

    // --- Spatial ---
    float audio_pan = 0.0f;     // [-1, +1]
    float audio_format = 0.0f;  // channel count, exposed as float for shaders
    std::array<float, 8> direction8{};

    // --- History (for waterfall/trail effects) ---
    std::array<float, kVolumeHistoryLength> volume_history{};
    uint32_t volume_history_head = 0;  // index of the most-recent sample

    // Per-band history ring for waterfall / spectrogram-style shaders.
    // Stored frame-major (one inner array per snapshotted frame); the
    // UniformPublisher unrolls into a flat band-major linear-time view
    // (`band * frames + frame`, frame=0 oldest) before writing the
    // shader uniform. Storage is sized for the worst case (kMaxBands),
    // but the publisher writes only the active prefix (`freq_band_count`
    // columns) so shaders declare their uniform at the build-time
    // DEFAULT_NUM_BANDS constant.
    //
    // Frame count is independent of kVolumeHistoryLength and capped at
    // 32 so the resulting shader uniform (DEFAULT_NUM_BANDS × frames =
    // 64 × 32 = 2048 float4 slots at the default band count) fits in a
    // single D3D11 constant buffer (max 4096 float4 slots) with room to
    // spare for the rest of the Listeningway uniforms and any user-shader
    // additions. At 60 fps this is ~0.5 s of history; at 30 fps ~1 s.
    static constexpr size_t kBandsHistoryFrames = 32;
    std::array<std::array<float, kMaxBands>, kBandsHistoryFrames>
        freq_bands_history{};
    uint32_t freq_bands_history_head = 0;

    // --- Profiling (per-stage EMA-smoothed wall-clock) ---
    dsp::StageTimings stage_timings{};
    uint32_t          stage_count    = 0;
    float             pipeline_micros = 0.0f;

    // --- Provenance ---
    uint64_t frame_index = 0;
    std::chrono::steady_clock::time_point captured_at{};
};

static_assert(std::is_trivially_copyable_v<AudioSnapshot>,
              "AudioSnapshot must be trivially copyable for seqlock memcpy");

}  // namespace lw
