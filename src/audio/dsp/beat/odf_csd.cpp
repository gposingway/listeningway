#include "odf_csd.h"

#include <algorithm>
#include <cmath>

namespace lw::dsp::beat {

void OdfCsd::reset() {
    std::fill(prev_mag_.begin(),    prev_mag_.end(),    0.0f);
    std::fill(prev_phase_.begin(),  prev_phase_.end(),  0.0f);
    std::fill(prev_phase2_.begin(), prev_phase2_.end(), 0.0f);
    warmed_up_ = false;
    frames_seen_ = 0;
}

void OdfCsd::ensure_size(size_t n) {
    if (prev_mag_.size() == n) return;
    prev_mag_.assign(n, 0.0f);
    prev_phase_.assign(n, 0.0f);
    prev_phase2_.assign(n, 0.0f);
    warmed_up_ = false;
    frames_seen_ = 0;
}

float OdfCsd::process(std::span<const float> magnitudes,
                       std::span<const float> phases) {
    const size_t n = std::min(magnitudes.size(), phases.size());
    if (n == 0) return 0.0f;
    ensure_size(n);

    // The first two frames have no meaningful phase prediction (the
    // extrapolation needs two previous phases); skip them and store
    // values for next time.
    if (!warmed_up_) {
        for (size_t k = 0; k < n; ++k) {
            prev_phase2_[k] = prev_phase_[k];
            prev_phase_[k]  = phases[k];
            prev_mag_[k]    = magnitudes[k];
        }
        if (++frames_seen_ >= 2) warmed_up_ = true;
        return 0.0f;
    }

    float sum = 0.0f;
    for (size_t k = 0; k < n; ++k) {
        const float mag      = magnitudes[k];
        const float prev_mag = prev_mag_[k];
        const float mag_diff = mag - prev_mag;

        // Half-wave rectification: only count bins whose magnitude rose.
        if (mag_diff > 0.0f) {
            const float phase_dev = phases[k] - 2.0f * prev_phase_[k] + prev_phase2_[k];
            const float csd = std::sqrt(
                mag * mag + prev_mag * prev_mag
                - 2.0f * mag * prev_mag * std::cos(phase_dev));
            sum += csd;
        }

        prev_phase2_[k] = prev_phase_[k];
        prev_phase_[k]  = phases[k];
        prev_mag_[k]    = mag;
    }
    return sum;
}

}  // namespace lw::dsp::beat
