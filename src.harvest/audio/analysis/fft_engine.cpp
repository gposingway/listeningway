#include "fft_engine.h"
#include <cmath>

namespace lw { namespace audio { namespace analysis {

FFTEngine::FFTEngine() {}
FFTEngine::~FFTEngine() {}

void FFTEngine::Configure(size_t fft_size) {
    if (configured_size_ == fft_size) return;
    configured_size_ = fft_size;
    hann_.resize(fft_size);
    if (fft_size > 1) {
        for (size_t i = 0; i < fft_size; ++i) {
            hann_[i] = 0.5f * (1.0f - std::cos(2.0f * 3.1415926535f * static_cast<float>(i) / static_cast<float>(fft_size - 1)));
        }
    }
}

const std::vector<float>& FFTEngine::HannWindow(size_t fft_size) {
    Configure(fft_size);
    return hann_;
}

}}} // namespace lw::audio::analysis
