// ChronotensityStage — energy-accumulated phase modulo 1.0 (AudioLink's
// preferred replacement for BPM-locked phase; robust where tempo isn't).
// Reads:  VolumeNorm, BassNorm, TrebNorm
// Writes: PhaseVolume, PhaseBass, PhaseTreble
#pragma once

#include <chrono>
#include <cmath>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class ChronotensityStage final : public IDspStage {
public:
    std::string_view name() const override { return "chronotensity"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {
            FieldId::VolumeNorm, FieldId::BassNorm, FieldId::TrebNorm,
        };
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::PhaseVolume, FieldId::PhaseBass, FieldId::PhaseTreble,
        };
        return w;
    }

    void reset() override {
        phase_v_ = phase_b_ = phase_t_ = 0.0f;
        last_t_ = std::chrono::steady_clock::time_point{};
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        auto now = frame.captured_at;
        float dt = 1.0f / 60.0f;
        if (last_t_.time_since_epoch().count() != 0) {
            dt = std::chrono::duration<float>(now - last_t_).count();
        }
        last_t_ = now;

        // 1.0 + k·(norm - 1) means at "average" energy (norm=1) the phase
        // advances at 1 Hz; loud accelerates, quiet decelerates.
        auto step = [dt](float norm, float k) {
            return dt * std::max(0.0f, 1.0f + k * (norm - 1.0f));
        };
        const float vn = frame.volume_norm.value_or(1.0f);
        const float bn = frame.bass_norm.value_or(1.0f);
        const float tn = frame.treb_norm.value_or(1.0f);

        phase_v_ += step(vn, cfg.chronotensity.gain_volume); phase_v_ -= std::floor(phase_v_);
        phase_b_ += step(bn, cfg.chronotensity.gain_bass);   phase_b_ -= std::floor(phase_b_);
        phase_t_ += step(tn, cfg.chronotensity.gain_treble); phase_t_ -= std::floor(phase_t_);

        frame.phase_volume = phase_v_;
        frame.phase_bass   = phase_b_;
        frame.phase_treble = phase_t_;
    }

private:
    float phase_v_ = 0.0f, phase_b_ = 0.0f, phase_t_ = 0.0f;
    std::chrono::steady_clock::time_point last_t_{};
};

}  // namespace lw::dsp
