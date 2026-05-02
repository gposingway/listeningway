// BeatStage — onset detection + autocorrelation tempo + PLL beat phase.
// Implements the algorithm choices from ADR-0007 / research-notes.md §1:
//
//   1. Onset envelope = flux_total (history-buffered at ~100 Hz frame rate).
//   2. Adaptive threshold: median(window) + λ·mean(window), aubio-style.
//   3. Refractory period prevents double-triggers.
//   4. Tempo via autocorrelation of the onset envelope over an 8-second
//      sliding window, with a log-Gaussian prior centered at 120 BPM.
//   5. Beat phase via PLL (k_p, k_i) soft-corrected on confident onsets.
//
// Reads:  FluxTotal
// Writes: Beat, BeatPhase, TempoBpm, TempoConfidence, TempoDetected
#pragma once

#include <chrono>
#include <vector>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class BeatStage final : public IDspStage {
public:
    std::string_view name() const override { return "beat"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::FluxTotal};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::Beat, FieldId::BeatPhase,
            FieldId::TempoBpm, FieldId::TempoConfidence, FieldId::TempoDetected,
        };
        return w;
    }

    void reset() override;
    void process(AnalysisFrame& frame, const config::Settings& cfg) override;

private:
    bool detect_onset(float flux, const config::BeatConfig& bc);
    void update_tempo(const config::BeatConfig& bc);

    // Onset envelope ring (one entry per call).
    std::vector<float> envelope_;
    size_t env_head_ = 0;
    size_t env_size_ = 0;
    static constexpr size_t kEnvCapacity = 1024;  // ~10 s at 100 Hz

    // Threshold window (small).
    std::vector<float> thresh_window_;
    size_t thresh_head_ = 0;

    // Beat output state
    float beat_value_  = 0.0f;
    std::chrono::steady_clock::time_point last_t_{};
    std::chrono::steady_clock::time_point last_onset_t_{};

    // Tempo / phase
    float tempo_bpm_        = 0.0f;
    float tempo_confidence_ = 0.0f;
    bool  tempo_detected_   = false;
    float beat_phase_       = 0.0f;

    // For tempo recompute throttling.
    int frames_since_tempo_update_ = 0;
};

}  // namespace lw::dsp
