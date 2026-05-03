// odf_csd — complex spectral difference, half-wave rectified (CSD-HWR)
// onset detection function.
//
// Per FFT bin:
//   phase_dev   = phase[k] - 2·prev_phase[k] + prev_phase2[k]
//   mag_diff    = |X[k]| - |X_prev[k]|
//   if mag_diff > 0:                          // half-wave rectification
//       csd_k   = √(|X[k]|² + |X_prev[k]|² - 2·|X[k]|·|X_prev[k]|·cos(phase_dev))
//       sum    += csd_k
// returns sum
//
// The phase-prediction term `phase[k] - 2·prev_phase[k] + prev_phase2[k]`
// linearly extrapolates the previous two phase observations and treats the
// deviation from that prediction as evidence of an onset. Adding the
// magnitude difference (and discarding negative-difference bins) makes the
// detector specific to *onsets* rather than offsets, which is what we want
// for visualisation triggers.
//
// References:
//   - Duxbury, Bello, Davies, Sandler. "Complex Domain Onset Detection for
//     Musical Signals." Proc. DAFx-03, London, 2003. (Original CSD.)
//   - Bello, Daudet, Abdallah, Duxbury, Davies, Sandler. "A Tutorial on
//     Onset Detection in Music Signals." IEEE TSAP 13(5), 2005, §IV-D.
//   - Stark thesis 2011, §3.2.4.
//
// Implementation cross-checked against Adam Stark's BTrack
// (`OnsetDetectionFunction.cpp:520-564`, GPLv3) for algorithm fidelity.
// No source code copied — algorithm derived from the published papers
// above, which describe the technique in full.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace lw::dsp::beat {

class OdfCsd {
public:
    /// Process one FFT frame's magnitudes and phases (both half-spectrum,
    /// equal length). Returns the raw CSD-HWR ODF value for this hop.
    /// Internal state persists across calls; call reset() on stream
    /// changes (sample rate, FFT size, source switch).
    float process(std::span<const float> magnitudes,
                  std::span<const float> phases);

    void reset();

private:
    void ensure_size(size_t n);

    std::vector<float> prev_mag_;
    std::vector<float> prev_phase_;
    std::vector<float> prev_phase2_;
    bool warmed_up_ = false;  ///< first two frames are warm-up; ODF == 0
    int  frames_seen_ = 0;
};

}  // namespace lw::dsp::beat
