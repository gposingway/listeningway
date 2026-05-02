// DirectionalStage — 8-bucket spatial intensity from the channel layout.
// Buckets (index): 0=Front, 1=FR, 2=Right, 3=BR, 4=Back, 5=BL, 6=Left, 7=FL.
// Reads:  Samples, Format
// Writes: Direction8
//
// Stereo handling uses a mid/side decomposition: the part of the signal
// that's the same in L and R goes to F (the listener's front), the L-only
// portion goes to L, the R-only portion goes to R. No 70/30 redistribution
// to FL/FR — for a typical headphone listener, "right-panned audio sounds
// to my right", not "to my front-right."
//
// After per-channel routing, two passes shape the result:
//   1. Spread: each bucket's energy bleeds additively into its two ring
//      neighbours by `frequency.spatial_spread` (default 0.25). At the
//      default, hard-Right (R=1.0) becomes R=1.0, FR=BR=0.25, others=0.
//   2. EMA smoothing: alpha = `frequency.spatial_smoothing` (default 0.10)
//      blends the previous frame's direction8 into the current frame to
//      reduce flicker on percussive content.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class DirectionalStage final : public IDspStage {
public:
    std::string_view name() const override { return "directional"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Samples, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::Direction8};
        return w;
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        std::array<float, 8> dir{};
        const auto channels = frame.format.channels;
        const auto layout   = frame.format.layout;
        const size_t total  = frame.samples.size();
        if (channels == 0 || total == 0) {
            frame.direction8 = prev_dir8_;  // hold previous on no-data
            return;
        }
        const size_t frames = total / channels;
        if (frames == 0) {
            frame.direction8 = prev_dir8_;
            return;
        }

        // Per-channel RMS.
        std::array<double, 8> sq{};  // up to 8 channels
        for (size_t i = 0; i < frames; ++i) {
            for (size_t c = 0; c < channels && c < 8; ++c) {
                const float s = frame.samples[i * channels + c];
                sq[c] += static_cast<double>(s) * s;
            }
        }
        std::array<float, 8> rms{};
        for (size_t c = 0; c < 8; ++c) {
            rms[c] = std::sqrt(static_cast<float>(sq[c] / static_cast<double>(frames)));
        }

        if (channels == 1) {
            dir[0] = rms[0];                                 // mono → F
        } else if (channels == 2 || layout == source::ChannelLayout::Stereo) {
            const float l = rms[0], r = rms[1];
            const float common = std::min(l, r);             // mid (in-both)
            const float sl = std::max(0.0f, l - common);     // left-only
            const float sr = std::max(0.0f, r - common);     // right-only
            dir[0] += common;                                 // F  ← mid
            dir[6] += sl;                                     // L  ← left-only
            dir[2] += sr;                                     // R  ← right-only
        } else if (channels == 6 || layout == source::ChannelLayout::Surround51) {
            // 5.1: FL FR FC LFE SL SR
            dir[7] += rms[0];                                 // FL
            dir[1] += rms[1];                                 // FR
            dir[0] += rms[2];                                 // FC → F
            dir[6] += rms[4];                                 // SL → L
            dir[2] += rms[5];                                 // SR → R
            // LFE (rms[3]) is omnidirectional; deliberately not routed.
        } else if (channels == 8) {
            // 7.1: FL FR FC LFE [SL SR BL BR] (Side variant) or
            //                   [BL BR SL SR] (Rear variant).
            dir[7] += rms[0];                                 // FL
            dir[1] += rms[1];                                 // FR
            dir[0] += rms[2];                                 // FC → F
            if (layout == source::ChannelLayout::Surround71Side
                || layout == source::ChannelLayout::Unknown) {
                dir[6] += rms[4]; dir[2] += rms[5];           // SL/SR → L/R
                dir[5] += rms[6]; dir[3] += rms[7];           // BL/BR → BL/BR
            } else {
                dir[5] += rms[4]; dir[3] += rms[5];           // BL/BR
                dir[6] += rms[6]; dir[2] += rms[7];           // SL/SR → L/R
            }
        } else {
            // Unknown layout: distribute RMS evenly across all 8 buckets.
            float sum = 0.0f;
            for (size_t c = 0; c < channels && c < 8; ++c) sum += rms[c];
            const float per = sum / 8.0f;
            for (auto& v : dir) v += per;
        }

        // Spread: bleed each bucket's energy into its two ring neighbours.
        // Buckets form a ring: F(0) FR(1) R(2) BR(3) B(4) BL(5) L(6) FL(7) F(0)…
        const float spread = std::clamp(cfg.frequency.spatial_spread, 0.0f, 0.5f);
        if (spread > 0.0f) {
            const std::array<float, 8> src = dir;
            for (int i = 0; i < 8; ++i) {
                const int prev_i = (i + 7) % 8;
                const int next_i = (i + 1) % 8;
                dir[i] += (src[prev_i] + src[next_i]) * spread;
            }
        }

        // Temporal smoothing (EMA) on the direction8 vector. Calms flicker
        // on percussive content without affecting steady-state values.
        const float alpha = std::clamp(cfg.frequency.spatial_smoothing, 0.0f, 0.95f);
        if (alpha > 0.0f) {
            for (int i = 0; i < 8; ++i) {
                dir[i] = alpha * prev_dir8_[i] + (1.0f - alpha) * dir[i];
            }
        }

        for (auto& v : dir) v = std::max(0.0f, v);
        prev_dir8_ = dir;
        frame.direction8 = dir;
    }

private:
    std::array<float, 8> prev_dir8_{};
};

}  // namespace lw::dsp
