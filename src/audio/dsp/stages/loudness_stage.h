// LoudnessStage — K-weighted (BS.1770) momentary loudness over 400 ms.
// Two cascaded biquads per channel; running mean-square in a sliding window
// gives momentary loudness. Output exposed as linear sqrt(mean_square),
// clamped to [0, 1].
//
// Coefficients (48 kHz reference) per ITU-R BS.1770-5; for other sample
// rates we re-derive on first frame via bilinear transform of the spec
// filters.
//
// Reads:  Samples, Format
// Writes: Loudness
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class LoudnessStage final : public IDspStage {
public:
    std::string_view name() const override { return "loudness"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Samples, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::Loudness};
        return w;
    }

    void reset() override {
        ch_state_.clear();
        ring_.clear();
        ring_pos_ = 0;
        running_sum_ = 0.0;
        configured_for_ = 0;
        configured_window_ = 0.0f;
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        const uint32_t sr = frame.format.sample_rate;
        const uint16_t ch = frame.format.channels;
        if (sr == 0 || ch == 0 || frame.samples.empty()) {
            frame.loudness = 0.0f;
            return;
        }
        configure(sr, ch, cfg.loudness.window_ms);

        const size_t frames = frame.samples.size() / ch;
        for (size_t i = 0; i < frames; ++i) {
            double frame_sq = 0.0;
            for (uint16_t c = 0; c < ch; ++c) {
                const float x = frame.samples[i * ch + c];
                auto& st = ch_state_[c];
                const float y1 = static_cast<float>(
                    cs_.b0 * x + cs_.b1 * st.x1_s1 + cs_.b2 * st.x2_s1
                  - cs_.a1 * st.y1_s1 - cs_.a2 * st.y2_s1);
                st.x2_s1 = st.x1_s1; st.x1_s1 = x;
                st.y2_s1 = st.y1_s1; st.y1_s1 = y1;

                const float y2 = static_cast<float>(
                    ch_.b0 * y1 + ch_.b1 * st.x1_s2 + ch_.b2 * st.x2_s2
                  - ch_.a1 * st.y1_s2 - ch_.a2 * st.y2_s2);
                st.x2_s2 = st.x1_s2; st.x1_s2 = y1;
                st.y2_s2 = st.y1_s2; st.y2_s2 = y2;

                frame_sq += static_cast<double>(y2) * y2;
            }
            // Push into the sliding ring (sum of all channel mean-squares).
            const double leaving = ring_[ring_pos_];
            running_sum_ -= leaving;
            ring_[ring_pos_] = frame_sq;
            running_sum_ += frame_sq;
            ring_pos_ = (ring_pos_ + 1) % ring_.size();
        }

        const double n_active = std::max<size_t>(1, ring_.size() * ch);
        const double mean_sq = running_sum_ / static_cast<double>(n_active);
        frame.loudness = std::min(1.0f, static_cast<float>(std::sqrt(mean_sq)));
    }

private:
    struct Biquad { double b0, b1, b2, a1, a2; };

    static Biquad shelf(double f0, double Q, double gain_db, double fs) {
        const double A = std::pow(10.0, gain_db / 40.0);
        const double w0 = 2.0 * std::numbers::pi * f0 / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double sqrtA = std::sqrt(A);
        Biquad q;
        q.b0 = A * ((A + 1) - (A - 1) * cw + 2 * sqrtA * alpha);
        q.b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        q.b2 = A * ((A + 1) - (A - 1) * cw - 2 * sqrtA * alpha);
        const double a0 = (A + 1) + (A - 1) * cw + 2 * sqrtA * alpha;
        q.a1 = -2 * ((A - 1) + (A + 1) * cw);
        q.a2 = (A + 1) + (A - 1) * cw - 2 * sqrtA * alpha;
        // Normalize
        q.b0 /= a0; q.b1 /= a0; q.b2 /= a0;
        q.a1 /= a0; q.a2 /= a0;
        return q;
    }
    static Biquad highpass(double f0, double Q, double fs) {
        const double w0 = 2.0 * std::numbers::pi * f0 / fs;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        Biquad q;
        const double a0 = 1 + alpha;
        q.b0 = (1 + cw) / 2.0 / a0;
        q.b1 = -(1 + cw) / a0;
        q.b2 = (1 + cw) / 2.0 / a0;
        q.a1 = -2 * cw / a0;
        q.a2 = (1 - alpha) / a0;
        return q;
    }

    void configure(uint32_t sr, uint16_t channels, float window_ms) {
        if (sr == configured_for_ && std::abs(window_ms - configured_window_) < 0.5f
            && ch_state_.size() == channels) return;

        // BS.1770 K-weighting filter prototypes:
        //   shelf at 1681.9744 Hz, +3.99984 dB, Q=0.707
        //   HPF   at 38.13547 Hz,  Q=0.5
        cs_ = shelf(1681.9744, 0.7071, 3.99984, sr);
        ch_ = highpass(38.13547, 0.5, sr);

        const size_t window_samples = std::max<size_t>(
            32, static_cast<size_t>(sr * window_ms / 1000.0f));
        ring_.assign(window_samples, 0.0);
        ring_pos_ = 0;
        running_sum_ = 0.0;

        ch_state_.assign(channels, ChState{});
        configured_for_ = sr;
        configured_window_ = window_ms;
    }

    struct ChState {
        // Stage 1 (shelf)
        float x1_s1 = 0, x2_s1 = 0, y1_s1 = 0, y2_s1 = 0;
        // Stage 2 (HPF)
        float x1_s2 = 0, x2_s2 = 0, y1_s2 = 0, y2_s2 = 0;
    };

    Biquad cs_{}, ch_{};                  // shelf, high-pass
    std::vector<ChState> ch_state_;       // per-channel filter state
    std::vector<double> ring_;            // sliding window of frame mean-squares
    size_t ring_pos_ = 0;
    double running_sum_ = 0.0;

    uint32_t configured_for_ = 0;
    float    configured_window_ = 0.0f;
};

}  // namespace lw::dsp
