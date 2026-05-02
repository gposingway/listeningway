// SpectralCentroidStage — magnitude-weighted center frequency, normalized
// to Nyquist (output ∈ [0, 1]). Useful for "brightness" / color-temp shaders.
// Reads:  Magnitudes, Format
// Writes: SpectralCentroid
#pragma once

#include <algorithm>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class SpectralCentroidStage final : public IDspStage {
public:
    std::string_view name() const override { return "spectral_centroid"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Magnitudes, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::SpectralCentroid};
        return w;
    }

    void process(AnalysisFrame& frame, const config::Settings&) override {
        if (!frame.magnitudes || frame.format.sample_rate == 0) {
            frame.spectral_centroid = 0.0f;
            return;
        }
        const auto mags = *frame.magnitudes;
        const float nyquist = frame.format.sample_rate * 0.5f;
        const float bin_hz = nyquist / static_cast<float>(mags.size());
        constexpr float kEps = 1e-10f;

        double weighted = 0.0;
        double total    = 0.0;
        for (size_t k = 0; k < mags.size(); ++k) {
            const float f = static_cast<float>(k) * bin_hz;
            weighted += static_cast<double>(f) * mags[k];
            total    += mags[k];
        }
        const float centroid_hz = (total > kEps)
            ? static_cast<float>(weighted / total) : 0.0f;
        frame.spectral_centroid = std::clamp(centroid_hz / nyquist, 0.0f, 1.0f);
    }
};

}  // namespace lw::dsp
