// DirectionalStage — 8-bucket spatial intensity from the channel layout.
// Buckets (index): 0=Front, 1=FR, 2=Right, 3=BR, 4=Back, 5=BL, 6=Left, 7=FL.
// Reads:  Samples, Format
// Writes: Direction8
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

    void process(AnalysisFrame& frame, const config::Settings&) override {
        std::array<float, 8> dir{};
        const auto channels = frame.format.channels;
        const auto layout   = frame.format.layout;
        const size_t total  = frame.samples.size();
        if (channels == 0 || total == 0) {
            frame.direction8 = dir;
            return;
        }
        const size_t frames = total / channels;
        if (frames == 0) {
            frame.direction8 = dir;
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

        auto add = [&](int idx, float v) { if (idx >= 0 && idx < 8) dir[idx] += std::max(0.0f, v); };

        if (channels == 1) {
            add(0, rms[0]);  // mono → front
        } else if (channels == 2 || layout == source::ChannelLayout::Stereo) {
            const float l = rms[0], r = rms[1];
            const float common = std::min(l, r);
            const float sl = std::max(0.0f, l - common);
            const float sr = std::max(0.0f, r - common);
            add(0, common);
            add(7, sl * 0.7f); add(6, sl * 0.3f);
            add(1, sr * 0.7f); add(2, sr * 0.3f);
        } else if (channels == 6 || layout == source::ChannelLayout::Surround51) {
            // 5.1: FL FR FC LFE SL SR
            add(7, rms[0]);                    // FL → Front-Left
            add(1, rms[1]);                    // FR → Front-Right
            add(0, rms[2]);                    // FC → Front
            add(6, rms[4] * 0.6f);              // SL → Left
            add(2, rms[5] * 0.6f);              // SR → Right
            add(4, (rms[4] + rms[5]) * 0.2f);   // some Back from sides
        } else if (channels == 8) {
            add(7, rms[0]);
            add(1, rms[1]);
            add(0, rms[2]);
            if (layout == source::ChannelLayout::Surround71Side
                || layout == source::ChannelLayout::Unknown) {
                // Side-first: SL,SR,BL,BR after LFE
                add(6, rms[4]);
                add(2, rms[5]);
                add(5, rms[6]);
                add(3, rms[7]);
            } else {
                // Rear-first: BL,BR,SL,SR after LFE
                add(5, rms[4]);
                add(3, rms[5]);
                add(6, rms[6]);
                add(2, rms[7]);
            }
            add(4, 0.5f * (rms[4] + rms[5]));
        } else {
            // Unknown layout: distribute evenly by RMS sum.
            float sum = 0.0f;
            for (size_t c = 0; c < channels && c < 8; ++c) sum += rms[c];
            const float per = sum / 8.0f;
            for (auto& v : dir) v += per;
        }
        for (auto& v : dir) v = std::max(0.0f, v);
        frame.direction8 = dir;
    }
};

}  // namespace lw::dsp
