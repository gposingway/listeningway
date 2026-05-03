// BeatStage — mode-aware beat detection with smooth pulse curve and
// best-effort tempo as instrumentation.
//
// Three modes (cfg.beat.mode):
//
//   Auto    — observes the recent onset rate and adapts the working
//             pulse-strength multiplier to hit a target rate (~2/s)
//             over a few seconds. Falls back to neutral during
//             silence. Reports a "locked" indicator when the rate
//             has been stable for a few seconds.
//
//   Profile — applies one of three pre-cooked signal-character
//             presets (Percussive / Melodic / Sustained), each
//             setting the working strength, per-band weights, and
//             pulse decay tau to known-good values for that signal
//             class. Names follow Ableton's "describe the signal,
//             not the genre" convention so they don't age badly.
//
//   Custom  — the working strength is taken directly from
//             cfg.beat.pulse_strength. Per-band weights and decay
//             tau use the same defaults as Auto.
//
// Output uniforms (`beat`, `beat_phase`, `tempo_bpm`, `tempo_confidence`,
// `tempo_detected`) keep their shapes. Two extra fields flow back via
// the snapshot for the overlay UI: `beat_pulse_strength` (the working
// value, so the Custom slider can seed off Auto's converged value) and
// `beat_auto_locked` (drives the Adapting / Locked status badge).
//
// Reads:  FluxLow, FluxMid, FluxHigh, FluxTotal
// Writes: Beat, BeatPhase, TempoBpm, TempoConfidence, TempoDetected,
//         BeatPulseStrength, BeatAutoLocked
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
            FieldId::BeatPulseStrength, FieldId::BeatAutoLocked,
        };
        return w;
    }

    void reset() override;
    void process(AnalysisFrame& frame, const config::Settings& cfg) override;

private:
    void update_tempo();
    void update_auto_strength(float dt_sec, bool onset_fired, float audio_energy);

    // --- Working parameters (set per-frame from current Mode/Profile) ----
    float working_strength_ = 1.0f;
    float weight_low_  = 1.00f;
    float weight_mid_  = 0.70f;
    float weight_high_ = 0.50f;
    float decay_tau_sec_ = 0.150f;

    // Last-applied mode, so we can seed working_strength_ on transition.
    int last_mode_ = -1;

    // --- Per-band onset state -------------------------------------------
    float baseline_low_  = 0.0f, baseline_mid_  = 0.0f, baseline_high_ = 0.0f;
    float var_low_       = 0.0f, var_mid_       = 0.0f, var_high_      = 0.0f;

    // --- Pulse output ---------------------------------------------------
    float beat_value_ = 0.0f;
    std::chrono::steady_clock::time_point last_t_{};
    std::chrono::steady_clock::time_point last_onset_t_{};

    // --- Auto-mode adaptation state -------------------------------------
    // Onset count over a sliding window for rate estimation.
    static constexpr float kAutoWindowSec = 5.0f;
    std::vector<std::chrono::steady_clock::time_point> auto_onset_log_;
    std::chrono::steady_clock::time_point last_auto_step_t_{};
    std::chrono::steady_clock::time_point last_auto_change_t_{};
    bool auto_locked_ = false;

    // --- Tempo / phase --------------------------------------------------
    std::vector<float> envelope_;
    size_t env_head_ = 0, env_size_ = 0;
    static constexpr size_t kEnvCapacity = 1024;

    float tempo_bpm_        = 0.0f;
    float tempo_confidence_ = 0.0f;
    bool  tempo_detected_   = false;
    float beat_phase_       = 0.0f;

    int frames_since_tempo_update_ = 0;
};

}  // namespace lw::dsp
