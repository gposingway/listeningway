// EqualizerStage — 5-band Gaussian-weighted equalizer applied to raw_bands.
// Reads:  RawBands
// Writes: Bands  (post-EQ)
//
// Each of the five EQ knobs (Bass / LowMid / Mid / HighMid / Treble) is
// centered at a normalized position 0/0.25/0.5/0.75/1.0 across the band
// range; per-band multiplier is the Gaussian-weighted blend.
#pragma once

#include <algorithm>
#include <cmath>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class EqualizerStage final : public IDspStage {
public:
    std::string_view name() const override { return "equalizer"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::RawBands};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::Bands};
        return w;
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        if (frame.raw_bands.empty()) {
            frame.bands.clear();
            return;
        }
        const size_t n = frame.raw_bands.size();
        frame.bands.resize(n);

        const auto& eq = cfg.frequency.equalizer_bands;
        const float width = std::max(0.001f, cfg.frequency.equalizer_width);
        constexpr float centers[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

        for (size_t b = 0; b < n; ++b) {
            const float pos = (n > 1)
                ? static_cast<float>(b) / static_cast<float>(n - 1)
                : 0.0f;
            float weighted = 0.0f;
            float total_w = 0.0f;
            for (int i = 0; i < 5; ++i) {
                const float d = pos - centers[i];
                const float w = std::exp(-(d * d) / (2.0f * width * width));
                weighted += w * eq[i];
                total_w  += w;
            }
            const float mult = (total_w > 0.0f) ? (weighted / total_w) : 1.0f;
            frame.bands[b] = frame.raw_bands[b] * mult;
        }
    }
};

}  // namespace lw::dsp
