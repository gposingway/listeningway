// ---------------------------------------------
// Audio source format and capabilities (ADR-0002, ADR-0003)
// ---------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>

namespace lw::source {

/// Speaker layout flags. Mirrors WASAPI channel masks at the meaningful values
/// without exposing the full Win32 bit soup.
enum class ChannelLayout {
    Unknown,
    Mono,
    Stereo,
    Surround51,        // FL FR FC LFE SL SR (ITU-R BS.775)
    Surround71Side,    // FL FR FC LFE SL SR BL BR (Windows default)
    Surround71Rear,    // FL FR FC LFE BL BR SL SR ("classic" 7.1)
};

/// Wire format of the float frames a source produces.
struct Format {
    uint32_t sample_rate = 0;
    uint16_t channels    = 0;
    ChannelLayout layout = ChannelLayout::Unknown;

    bool valid() const noexcept {
        return sample_rate > 0 && channels > 0;
    }
};

/// What `IAudioSource::open()` reports back. Used by the DSP pipeline and the
/// FFT engine to size their internal buffers without guessing.
struct Capabilities {
    Format format;
    size_t typical_frame_count = 0;   ///< frames per push, typical
    size_t max_frame_count     = 0;   ///< frames per push, worst case
    double reported_latency_ms = 0.0; ///< source-driven end-to-end latency hint
};

}  // namespace lw::source
