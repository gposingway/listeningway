// LogBoostStage — exponential gain curve over band index (preserves v1
// "log_strength" knob behavior). In-place on raw_bands when log scale is
// enabled; otherwise a no-op. Order: runs BEFORE EqualizerStage so the EQ
// operates on the already-boosted spectrum.
//
// Reads:  RawBands
// Writes: RawBands (in-place)
#pragma once

#include <cmath>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class LogBoostStage final : public IDspStage {
public:
    std::string_view name() const override { return "log_boost"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::RawBands};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::RawBands};
        return w;
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        if (cfg.frequency.band_scale != config::FrequencyConfig::BandScale::Log
            || cfg.frequency.log_strength == 0.0f) {
            return;
        }
        const float ls = cfg.frequency.log_strength / 3.0f;
        for (size_t b = 0; b < frame.raw_bands.size(); ++b) {
            const float gain = std::exp(static_cast<float>(b + 1) * ls);
            frame.raw_bands[b] *= gain;
        }
    }
};

}  // namespace lw::dsp
