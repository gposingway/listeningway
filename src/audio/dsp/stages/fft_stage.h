// FftStage — Hann-windowed FFT, magnitudes only.
// Reads:  Samples, Format
// Writes: Magnitudes
//
// Wraps kissfft with cached config + buffers (one allocation at start, none
// per frame). Output is half-spectrum magnitudes in `magnitudes_buf_`,
// exposed via std::span on the AnalysisFrame.
#pragma once

#include <kiss_fft.h>

#include <span>
#include <vector>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class FftStage final : public IDspStage {
public:
    FftStage();
    ~FftStage() override;

    std::string_view name() const override { return "fft"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Samples, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {FieldId::Magnitudes};
        return w;
    }
    void reset() override;
    void process(AnalysisFrame& frame, const config::Settings& cfg) override;

private:
    void configure(int fft_size);

    int                     fft_size_       = 0;
    kiss_fft_state*         fft_cfg_        = nullptr;
    std::vector<kiss_fft_cpx> in_;
    std::vector<kiss_fft_cpx> out_;
    std::vector<float>      hann_;
    std::vector<float>      magnitudes_;
};

}  // namespace lw::dsp
