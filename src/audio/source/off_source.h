#pragma once

#include "i_audio_source.h"

namespace lw::source {

/// "Audio analysis disabled" source. Reports availability, accepts start/stop,
/// never invokes the sink. AudioSystem wraps this to drive the snapshot to all
/// zeros while the user has the dropdown set to Off.
class OffSource final : public IAudioSource {
public:
    Info info() const override {
        return {
            .code = "off",
            .display = "None (Audio Analysis Off)",
            .is_default = false,
            .order = 100,
            .activates_capture = false,
        };
    }
    bool available() const override { return true; }

    std::optional<Capabilities> open() override {
        return Capabilities{};  // valid-but-empty: no capture, no format
    }
    bool start(FrameSink) override { return true; }
    void stop() override {}
};

}  // namespace lw::source
