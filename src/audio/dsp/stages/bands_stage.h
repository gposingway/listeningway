// BandsStage — bin FFT magnitudes into perceptual frequency bands.
// Reads:  Magnitudes, Format
// Writes: RawBands, Bands16, Bands32
//
// Slaney mel by default (per ADR-0007 / research-notes.md §1). Linear and
// log scales selectable via Settings::frequency::band_scale.
#pragma once

#include <span>
#include <vector>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class BandsStage final : public IDspStage {
public:
    std::string_view name() const override { return "bands"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Magnitudes, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::RawBands, FieldId::Bands16, FieldId::Bands32,
        };
        return w;
    }
    void reset() override;
    void process(AnalysisFrame& frame, const config::Settings& cfg) override;

private:
    struct Cache {
        // Cache key
        int  band_count   = 0;
        int  fft_half     = 0;
        int  scale        = -1;
        float min_freq    = 0.0f;
        float max_freq    = 0.0f;
        float sample_rate = 0.0f;

        // Edges in Hz, length band_count + 1.
        std::vector<float> edges;
        // Per-band bin index lists.
        std::vector<std::vector<int>> bins;
    } cache_;

    void rebuild_cache(int band_count, int fft_half, float sample_rate,
                       const config::FrequencyConfig& fc);
    static void downsample(const std::vector<float>& src, std::span<float> dst);
};

}  // namespace lw::dsp
