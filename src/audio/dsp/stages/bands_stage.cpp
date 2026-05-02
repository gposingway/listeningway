#include "bands_stage.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lw::dsp {

namespace {

float mel_from_hz(float hz) {
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}
float hz_from_mel(float mel) {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

}  // namespace

void BandsStage::reset() {
    cache_ = {};
}

void BandsStage::rebuild_cache(int band_count, int fft_half, float sample_rate,
                               const config::FrequencyConfig& fc) {
    cache_.band_count   = band_count;
    cache_.fft_half     = fft_half;
    cache_.scale        = static_cast<int>(fc.band_scale);
    cache_.min_freq     = std::clamp(fc.min_freq, 10.0f, sample_rate * 0.5f - 1.0f);
    cache_.max_freq     = std::clamp(fc.max_freq, cache_.min_freq + 1.0f, sample_rate * 0.5f);
    cache_.sample_rate  = sample_rate;

    cache_.edges.assign(band_count + 1, 0.0f);
    if (fc.band_scale == config::FrequencyConfig::BandScale::Linear) {
        const float w = (cache_.max_freq - cache_.min_freq) / band_count;
        for (int b = 0; b <= band_count; ++b) {
            cache_.edges[b] = cache_.min_freq + b * w;
        }
    } else if (fc.band_scale == config::FrequencyConfig::BandScale::Log) {
        const float lo = std::log10(cache_.min_freq);
        const float hi = std::log10(cache_.max_freq);
        for (int b = 0; b <= band_count; ++b) {
            const float t = static_cast<float>(b) / band_count;
            cache_.edges[b] = std::pow(10.0f, lo + t * (hi - lo));
        }
    } else {  // Mel (Slaney)
        const float lo = mel_from_hz(cache_.min_freq);
        const float hi = mel_from_hz(cache_.max_freq);
        for (int b = 0; b <= band_count; ++b) {
            const float t = static_cast<float>(b) / band_count;
            cache_.edges[b] = hz_from_mel(lo + t * (hi - lo));
        }
    }

    // Map FFT bins to bands.
    cache_.bins.assign(band_count, {});
    const float bin_width_hz = sample_rate * 0.5f / static_cast<float>(fft_half);
    for (int k = 1; k < fft_half; ++k) {
        const float f = static_cast<float>(k) * bin_width_hz;
        if (f < cache_.edges.front()) continue;
        for (int b = 0; b < band_count; ++b) {
            if (f >= cache_.edges[b] && f < cache_.edges[b + 1]) {
                cache_.bins[b].push_back(k);
                break;
            }
        }
    }
    // Each band gets at least one bin (closest to center) so empty edges
    // (e.g. very narrow bass band on a small FFT) still produce a value.
    for (int b = 0; b < band_count; ++b) {
        if (cache_.bins[b].empty()) {
            const float center = std::sqrt(cache_.edges[b] * cache_.edges[b + 1]);
            int closest = 1;
            float best = std::numeric_limits<float>::max();
            for (int k = 1; k < fft_half; ++k) {
                const float d = std::abs(static_cast<float>(k) * bin_width_hz - center);
                if (d < best) { best = d; closest = k; }
            }
            cache_.bins[b].push_back(closest);
        }
    }
}

void BandsStage::downsample(const std::vector<float>& src, std::span<float> dst) {
    if (dst.empty() || src.empty()) {
        std::fill(dst.begin(), dst.end(), 0.0f);
        return;
    }
    const float ratio = static_cast<float>(src.size()) / static_cast<float>(dst.size());
    for (size_t i = 0; i < dst.size(); ++i) {
        const size_t lo = static_cast<size_t>(i * ratio);
        const size_t hi = std::min(src.size(), static_cast<size_t>((i + 1) * ratio));
        float sum = 0.0f;
        size_t n = 0;
        for (size_t k = lo; k < hi; ++k) { sum += src[k]; ++n; }
        dst[i] = (n > 0) ? sum / static_cast<float>(n) : 0.0f;
    }
}

void BandsStage::process(AnalysisFrame& frame, const config::Settings& cfg) {
    if (!frame.magnitudes) {
        std::fill(frame.bands_16.begin(), frame.bands_16.end(), 0.0f);
        std::fill(frame.bands_32.begin(), frame.bands_32.end(), 0.0f);
        return;
    }
    const auto mags = *frame.magnitudes;
    const int fft_half = static_cast<int>(mags.size());
    if (fft_half <= 0) return;

    const int band_count = std::max(1, std::min(cfg.frequency.band_count,
                                                static_cast<int>(kMaxBands)));
    const float sr = static_cast<float>(frame.format.sample_rate);
    if (sr <= 0.0f) return;

    if (cache_.band_count != band_count
        || cache_.fft_half != fft_half
        || cache_.scale != static_cast<int>(cfg.frequency.band_scale)
        || cache_.min_freq != cfg.frequency.min_freq
        || cache_.max_freq != cfg.frequency.max_freq
        || cache_.sample_rate != sr) {
        rebuild_cache(band_count, fft_half, sr, cfg.frequency);
    }

    frame.raw_bands.assign(band_count, 0.0f);
    for (int b = 0; b < band_count; ++b) {
        float sum = 0.0f;
        for (int k : cache_.bins[b]) sum += mags[k];
        const float avg = (cache_.bins[b].empty()) ? 0.0f
            : sum / static_cast<float>(cache_.bins[b].size());
        frame.raw_bands[b] = std::min(1.0f, avg * cfg.frequency.band_norm);
    }
    frame.band_count = static_cast<size_t>(band_count);

    downsample(frame.raw_bands, std::span<float>(frame.bands_16.data(), 16));
    downsample(frame.raw_bands, std::span<float>(frame.bands_32.data(), 32));
}

}  // namespace lw::dsp
