// BeatStage — auto-tuned multi-band onset detection with smooth pulse
// curve, plus best-effort tempo estimation as instrumentation.
//
// Design follows the AudioLink philosophy: don't try to be a "correct"
// beat tracker, give shaders a robust pulse signal that's never wrong
// because it never claims certainty about discrete beats.
//
// Pulse pipeline:
//   1. Read per-band flux (low / mid / high).
//   2. Maintain per-band EMA baseline + variance, adapting slowly to the
//      content. This is the auto-sensitivity source — no user constant.
//   3. An onset in band b fires when flux_b > baseline_b + N · sigma_b,
//      where N is derived from the user's single `pulse_strength` knob
//      (lower N for higher strength = more reactive).
//   4. Aggregate per-band onsets with a bass-weighted max.
//   5. Output `beat` as a smooth pulse curve in [0, 1] with instant attack
//      to the onset strength and exponential decay (no binary cliff).
//
// Tempo / phase outputs (`tempo_bpm`, `tempo_confidence`, `tempo_detected`,
// `beat_phase`) are computed as instrumentation using internal constants.
// They have no UI knobs; shaders gate on `tempo_confidence > 0.4` and fall
// back to the always-on chronotensity phases when the tempo isn't locked.
//
// Reads:  FluxLow, FluxMid, FluxHigh, FluxTotal
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
        static constexpr FieldId r[] = {
            FieldId::FluxLow, FieldId::FluxMid, FieldId::FluxHigh, FieldId::FluxTotal,
        };
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
    void update_tempo();

    // --- Per-band onset state -------------------------------------------
    // EMA baseline (running mean of flux) and variance (EMA of squared
    // deviation from baseline). Used as the auto-sensitivity reference.
    float baseline_low_  = 0.0f, baseline_mid_  = 0.0f, baseline_high_ = 0.0f;
    float var_low_       = 0.0f, var_mid_       = 0.0f, var_high_      = 0.0f;

    // --- Pulse output ---------------------------------------------------
    float beat_value_ = 0.0f;
    std::chrono::steady_clock::time_point last_t_{};
    std::chrono::steady_clock::time_point last_onset_t_{};

    // --- Tempo / phase (instrumentation) --------------------------------
    std::vector<float> envelope_;          // ring of flux_total samples
    size_t env_head_ = 0, env_size_ = 0;
    static constexpr size_t kEnvCapacity = 1024;  // ~10 s at ~100 Hz

    float tempo_bpm_        = 0.0f;
    float tempo_confidence_ = 0.0f;
    bool  tempo_detected_   = false;
    float beat_phase_       = 0.0f;

    int frames_since_tempo_update_ = 0;
};

}  // namespace lw::dsp
