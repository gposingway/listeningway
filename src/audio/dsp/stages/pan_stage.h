// PanStage — stereo / surround pan computation with deadzone, silence
// threshold, and significant threshold (preserves v1 perceptual behavior).
// Reads:  Samples, Format, VolumeLeft, VolumeRight
// Writes: AudioPan
#pragma once

#include <algorithm>
#include <cmath>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class PanStage final : public IDspStage {
public:
    std::string_view name() const override { return "pan"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {
            FieldId::Samples, FieldId::Format,
            FieldId::VolumeLeft, FieldId::VolumeRight,
        };
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::AudioPan};
        return w;
    }

    void reset() override { smoothed_pan_ = 0.0f; pan_initialized_ = false; }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        const float l = frame.volume_left.value_or(0.0f);
        const float r = frame.volume_right.value_or(0.0f);

        // Tuned thresholds (preserved from v1).
        constexpr float kDeadzone   = 0.02f;   // 2% relative-diff considered balanced
        constexpr float kSilence    = 0.00001f;
        constexpr float kSignificant = 0.05f;

        float pan = 0.0f;
        if (frame.format.channels >= 2 && l + r > 0.0001f) {
            const float diff = std::abs(r - l);
            const float sum = l + r;
            const float rel = diff / sum;
            if (rel < kDeadzone) {
                pan = 0.0f;
            } else if (l < kSilence && r > kSignificant) {
                pan = 1.0f;
            } else if (r < kSilence && l > kSignificant) {
                pan = -1.0f;
            } else {
                pan = std::clamp((r - l) / sum, -1.0f, 1.0f);
            }
        }
        // User offset bias.
        pan = std::clamp(pan + std::clamp(cfg.audio.pan_offset, -1.0f, 1.0f), -1.0f, 1.0f);

        // Optional smoothing (alpha = 1 / (1 + 10·smoothing)).
        const float s = std::clamp(cfg.audio.pan_smoothing, 0.0f, 1.0f);
        if (s > 0.0f) {
            if (!pan_initialized_) { smoothed_pan_ = pan; pan_initialized_ = true; }
            else {
                const float alpha = 1.0f / (1.0f + s * 10.0f);
                smoothed_pan_ = (1.0f - alpha) * smoothed_pan_ + alpha * pan;
            }
            frame.audio_pan = smoothed_pan_;
        } else {
            pan_initialized_ = false;
            frame.audio_pan = pan;
        }
    }

private:
    float smoothed_pan_   = 0.0f;
    bool  pan_initialized_ = false;
};

}  // namespace lw::dsp
