// ---------------------------------------------
// IAudioSource — adapter for audio frame producers (ADR-0003)
//
// Multiple implementations: WasapiLoopbackSource, OffSource, FileSource,
// SignalGeneratorSource, ProcessAudioSource (opt-in), TeeSource (decorator).
// All are owned by AudioSystem; lifecycle is managed there.
// ---------------------------------------------
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>

#include "source_format.h"

namespace lw::source {

/// Metadata used by the UI dropdown, state machine, and configuration code.
struct Info {
    std::string code;       ///< stable identifier persisted in Settings
    std::string display;    ///< human-readable for UI
    bool is_default = false;
    int order = 0;          ///< UI sort order
    bool activates_capture = true;  ///< false for OffSource
};

/// The adapter interface. Sources push frames to a sink callback; the sink is
/// supplied by AudioSystem and forwards into the FrameRing.
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    virtual Info info() const = 0;
    virtual bool available() const = 0;

    /// Acquire device handles; populate Capabilities. nullopt on failure.
    /// Caller invokes start() afterwards if the result is non-null.
    virtual std::optional<Capabilities> open() = 0;

    /// Pushed-frames callback signature. The first argument is interleaved
    /// float samples. The Format is the *current* capture format (a source
    /// may push frames in a different format than what `open()` reported if
    /// e.g. the device renegotiates mid-stream — though for v1 most sources
    /// hold a single format for the lifetime of a start/stop cycle).
    using FrameSink = std::function<void(std::span<const float>, Format)>;

    /// Begin pushing frames to `sink`. Returns false on failure.
    virtual bool start(FrameSink sink) = 0;

    /// Stop pushing frames. Joins any internal threads. Idempotent.
    virtual void stop() = 0;

    /// Optional restart hint: source may signal that an external event
    /// (device change, file end-of-stream) means the AudioSystem should
    /// stop()/open()/start() us again. Polled from the worker thread.
    virtual bool restart_requested() const { return false; }
};

}  // namespace lw::source
