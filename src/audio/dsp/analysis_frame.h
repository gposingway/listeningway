// ---------------------------------------------
// AnalysisFrame — carrier between DSP stages (ADR-0002)
//
// A stage transforms an AnalysisFrame in place. Optional<T> fields represent
// "this stage was disabled / skipped"; if a stage runs, it always populates
// its outputs. This is *not* a snapshot — it's a working set held only
// during a single pipeline pass on the DSP thread.
// ---------------------------------------------
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "../snapshot/audio_snapshot.h"  // for kMaxBands
#include "../source/source_format.h"

namespace lw::dsp {

struct AnalysisFrame {
    // --- Input from capture ---
    std::span<const float> samples;             ///< interleaved
    source::Format format;
    std::chrono::steady_clock::time_point captured_at;
    uint64_t frame_index = 0;

    // --- Time-domain features ---
    std::optional<float> volume;
    std::optional<float> volume_left;
    std::optional<float> volume_right;
    std::optional<float> volume_norm;
    std::optional<float> volume_att;

    // --- Frequency-domain features ---
    std::optional<std::span<const float>> magnitudes;  ///< FFT mag bins, half-spectrum
    std::optional<std::span<const float>> phases;      ///< FFT phase (atan2 of complex), half-spectrum, in radians (-π, π]

    // Heap-backed working buffers that stages can write into; we use vectors
    // here (not arrays) because band counts are configurable. Stages write
    // their outputs and the published Snapshot copies into fixed arrays.
    std::vector<float> raw_bands;        // pre-EQ, pre-LogBoost
    std::vector<float> bands;            // post-EQ, post-LogBoost
    std::array<float, 16> bands_16{};
    std::array<float, 32> bands_32{};
    std::optional<size_t> band_count;    // size of `bands`/`raw_bands`

    std::optional<float> bass_norm, mid_norm, treb_norm;
    std::optional<float> bass_att,  mid_att,  treb_att;
    std::optional<float> spectral_centroid;
    std::optional<float> loudness;

    std::optional<float> flux_total;
    std::optional<float> flux_low;
    std::optional<float> flux_mid;
    std::optional<float> flux_high;

    // --- Beat/tempo ---
    std::optional<float> beat;
    std::optional<float> beat_phase;
    std::optional<float> tempo_bpm;
    std::optional<float> tempo_confidence;
    std::optional<bool>  tempo_detected;
    std::optional<float> beat_pulse_strength;  ///< Auto/Profile/Custom working value
    std::optional<bool>  beat_auto_locked;     ///< Auto-mode stable indicator

    // --- Chronotensity phases ---
    std::optional<float> phase_volume;
    std::optional<float> phase_bass;
    std::optional<float> phase_treble;

    // --- Spatial ---
    std::optional<float> audio_pan;
    std::optional<std::array<float, 8>> direction8;
};

}  // namespace lw::dsp
