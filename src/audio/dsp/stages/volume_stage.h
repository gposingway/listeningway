// VolumeStage — RMS volume + per-channel L/R + AGC-normalized siblings.
// Reads:  Samples, Format
// Writes: Volume, VolumeLeft, VolumeRight, VolumeNorm, VolumeAtt
#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class VolumeStage final : public IDspStage {
public:
    std::string_view name() const override { return "volume"; }

    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Samples, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::Volume, FieldId::VolumeLeft, FieldId::VolumeRight,
            FieldId::VolumeNorm, FieldId::VolumeAtt,
        };
        return w;
    }

    void reset() override {
        running_mean_ = 0.0f;
        att_ = 1.0f;
        last_t_ = std::chrono::steady_clock::time_point{};
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        const size_t channels = frame.format.channels;
        if (channels == 0 || frame.samples.empty()) {
            frame.volume = 0.0f;
            frame.volume_left = 0.0f;
            frame.volume_right = 0.0f;
            frame.volume_norm = 1.0f;
            frame.volume_att = 1.0f;
            return;
        }

        const size_t total = frame.samples.size();
        const size_t frames = total / channels;
        if (frames == 0) {
            frame.volume = 0.0f;
            frame.volume_left = 0.0f;
            frame.volume_right = 0.0f;
            frame.volume_norm = 1.0f;
            frame.volume_att = 1.0f;
            return;
        }

        // Single pass: mean-abs (used as `volume` for snappy reactivity per
        // visualizer-design research) + per-channel sum-of-squares.
        double abs_sum = 0.0;
        double sum_l = 0.0;
        double sum_r = 0.0;
        for (size_t i = 0; i < frames; ++i) {
            for (size_t c = 0; c < channels; ++c) {
                const float s = frame.samples[i * channels + c];
                abs_sum += std::fabs(s);
                if (c == 0) sum_l += static_cast<double>(s) * s;
                else if (c == 1) sum_r += static_cast<double>(s) * s;
            }
        }
        const float mean_abs = static_cast<float>(abs_sum / total);
        const float volume = std::min(1.0f, mean_abs * cfg.frequency.amplifier_volume);

        const float left  = (channels >= 1) ?
            std::sqrt(static_cast<float>(sum_l / static_cast<double>(frames))) : 0.0f;
        const float right = (channels >= 2) ?
            std::sqrt(static_cast<float>(sum_r / static_cast<double>(frames))) : left;

        frame.volume = volume;
        frame.volume_left = left;
        frame.volume_right = right;

        // AGC: instantaneous ÷ exponential running mean.
        // Time delta from previous frame; on first call, init from `volume`.
        auto now = frame.captured_at;
        float dt_sec = 1.0f / 60.0f;
        if (last_t_.time_since_epoch().count() != 0) {
            dt_sec = std::chrono::duration<float>(now - last_t_).count();
        }
        last_t_ = now;
        if (running_mean_ <= 0.0f) running_mean_ = std::max(volume, 1e-4f);

        const float window = std::max(0.1f, cfg.agc.window_seconds);
        const float alpha = std::clamp(dt_sec / window, 0.0f, 1.0f);
        running_mean_ = (1.0f - alpha) * running_mean_ + alpha * std::max(volume, 1e-6f);

        const float ratio = volume / std::max(running_mean_, 1e-6f);
        const float clamped = std::min(ratio, cfg.agc.clamp_max);
        frame.volume_norm = clamped;

        // Asymmetric attack/decay smoothing for `_att` sibling.
        const float att_alpha = (clamped > att_)
            ? std::clamp(dt_sec / std::max(0.001f, cfg.agc.att_attack_ms * 0.001f), 0.0f, 1.0f)
            : std::clamp(dt_sec / std::max(0.001f, cfg.agc.att_release_ms * 0.001f), 0.0f, 1.0f);
        att_ = (1.0f - att_alpha) * att_ + att_alpha * clamped;
        frame.volume_att = att_;
    }

private:
    float running_mean_ = 0.0f;
    float att_ = 1.0f;
    std::chrono::steady_clock::time_point last_t_{};
};

}  // namespace lw::dsp
