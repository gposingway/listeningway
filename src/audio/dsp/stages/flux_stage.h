// FluxStage — per-band spectral flux (positive half-wave rectified diff
// of log-magnitudes). Produces total + low/mid/high splits used by the
// beat detector.
// Reads:  Magnitudes, Format
// Writes: FluxTotal, FluxLow, FluxMid, FluxHigh
//
// Bands: low ≤ 150 Hz (kick), mid 150 Hz – 2 kHz (snare), high ≥ 6 kHz (hat).
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class FluxStage final : public IDspStage {
public:
    std::string_view name() const override { return "flux"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Magnitudes, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::FluxTotal, FieldId::FluxLow, FieldId::FluxMid, FieldId::FluxHigh,
        };
        return w;
    }

    void reset() override { prev_log_.clear(); }

    void process(AnalysisFrame& frame, const config::Settings&) override {
        if (!frame.magnitudes || frame.format.sample_rate == 0) {
            frame.flux_total = frame.flux_low = frame.flux_mid = frame.flux_high = 0.0f;
            return;
        }
        const auto mags = *frame.magnitudes;
        const float bin_hz = frame.format.sample_rate * 0.5f
                           / static_cast<float>(mags.size());

        // Convert to log-magnitude (compress dynamic range; flux on log spectra
        // is more perceptually flat than linear, per research-notes.md §1).
        constexpr float kEps = 1e-10f;
        if (prev_log_.size() != mags.size()) {
            prev_log_.assign(mags.size(), std::log(kEps));
            frame.flux_total = frame.flux_low = frame.flux_mid = frame.flux_high = 0.0f;
            return;
        }

        float total = 0.0f, low = 0.0f, mid = 0.0f, high = 0.0f;
        for (size_t k = 0; k < mags.size(); ++k) {
            const float lo = std::log(std::max(kEps, mags[k]));
            const float diff = std::max(0.0f, lo - prev_log_[k]);
            prev_log_[k] = lo;
            total += diff;

            const float f = static_cast<float>(k) * bin_hz;
            if (f <= 150.0f) low += diff;
            else if (f <= 2000.0f) mid += diff;
            else if (f >= 6000.0f) high += diff;
        }
        const float norm = 1.0f / static_cast<float>(mags.size());
        frame.flux_total = total * norm;
        frame.flux_low   = low   * norm;
        frame.flux_mid   = mid   * norm;
        frame.flux_high  = high  * norm;
    }

private:
    std::vector<float> prev_log_;
};

}  // namespace lw::dsp
