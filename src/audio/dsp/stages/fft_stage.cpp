#include "fft_stage.h"

#include <kiss_fft.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace lw::dsp {

FftStage::FftStage() = default;

FftStage::~FftStage() {
    if (fft_cfg_) {
        std::free(fft_cfg_);
        fft_cfg_ = nullptr;
    }
}

void FftStage::reset() {
    std::fill(magnitudes_.begin(), magnitudes_.end(), 0.0f);
}

void FftStage::configure(int fft_size) {
    if (fft_size == fft_size_) return;
    if (fft_cfg_) {
        std::free(fft_cfg_);
        fft_cfg_ = nullptr;
    }
    fft_size_ = fft_size;
    if (fft_size_ <= 0) return;

    fft_cfg_ = kiss_fft_alloc(fft_size_, /*inverse*/ 0, nullptr, nullptr);
    in_.assign(fft_size_, kiss_fft_cpx{0.f, 0.f});
    out_.assign(fft_size_, kiss_fft_cpx{0.f, 0.f});

    hann_.resize(fft_size_);
    for (int n = 0; n < fft_size_; ++n) {
        hann_[n] = 0.5f * (1.0f - std::cos(
            (2.0f * std::numbers::pi_v<float> * n) / (fft_size_ - 1)));
    }

    magnitudes_.assign(fft_size_ / 2, 0.0f);
}

void FftStage::process(AnalysisFrame& frame, const config::Settings& cfg) {
    const int target_size = std::max(64, cfg.frequency.fft_size);
    configure(target_size);
    if (!fft_cfg_) return;

    const size_t channels = std::max<uint16_t>(frame.format.channels, 1);
    const size_t frames_in = frame.samples.size() / channels;
    const size_t frames_used = std::min<size_t>(frames_in, static_cast<size_t>(fft_size_));

    // Down-mix to mono and apply Hann window. Pad with zeros to fft_size_.
    for (size_t i = 0; i < frames_used; ++i) {
        float s = 0.0f;
        for (size_t c = 0; c < channels; ++c) {
            s += frame.samples[i * channels + c];
        }
        s /= static_cast<float>(channels);
        in_[i].r = s * hann_[i];
        in_[i].i = 0.0f;
    }
    for (size_t i = frames_used; i < static_cast<size_t>(fft_size_); ++i) {
        in_[i].r = 0.0f;
        in_[i].i = 0.0f;
    }

    kiss_fft(fft_cfg_, in_.data(), out_.data());

    const size_t half = static_cast<size_t>(fft_size_) / 2;
    for (size_t k = 0; k < half; ++k) {
        const float r = out_[k].r;
        const float im = out_[k].i;
        magnitudes_[k] = std::sqrt(r * r + im * im);
    }

    frame.magnitudes = std::span<const float>(magnitudes_.data(), magnitudes_.size());
}

}  // namespace lw::dsp
