#pragma once
#include <vector>
#include <cstddef>
#include <kiss_fft.h>

namespace lw { namespace audio { namespace analysis {

class FFTEngine {
public:
    FFTEngine();
    ~FFTEngine();

    void Configure(size_t fft_size);
    const std::vector<float>& HannWindow(size_t fft_size);

private:
    size_t configured_size_ = 0;
    std::vector<float> hann_;
};

}}} // namespace lw::audio::analysis
