// ---------------------------------------------
// IOutputConsumer — adapter for output destinations (ADR-0010).
//
// Lifecycle-only interface. Internal always-on consumers (UniformPublisher,
// Overlay) and toggleable network consumers (OSC, OpenRGB, future SSE) all
// live behind this contract. Snapshot acquisition is each consumer's own
// choice — event-driven for ReShade-bound, worker-thread for network. There
// is intentionally no shared `consume()` method; that heterogeneity is a
// property to preserve, not paper over.
// ---------------------------------------------
#pragma once

#include <string>
#include <string_view>

#include <windows.h>

namespace lw {
class AudioSystem;
namespace config { struct Settings; }
}

namespace lw::output {

class IOutputConsumer {
public:
    virtual ~IOutputConsumer() = default;

    /// Stable identifier for settings persistence and logging.
    virtual std::string_view id() const = 0;

    /// Human-readable name for the overlay UI.
    virtual std::string_view display_name() const = 0;

    /// True if user can toggle on/off via the overlay; false for always-on
    /// internal consumers.
    virtual bool is_user_configurable() const = 0;

    /// For toggleable consumers: read the user's current enable preference
    /// from settings (typically `settings.network.<id>.enabled`). Always-on
    /// consumers return true unconditionally and don't override.
    virtual bool is_enabled(const config::Settings&) const { return true; }

    /// Activate the consumer. Internal consumers register ReShade event
    /// hooks here; network consumers spin up sockets and worker threads.
    /// Returns false on failure; the orchestrator surfaces the error via
    /// status_line(). Calling start() on an already-started consumer is a
    /// no-op that returns true.
    virtual bool start(AudioSystem& system, HMODULE addon_module) = 0;

    /// Deactivate. Tears down sockets, threads, and event registrations.
    /// Idempotent.
    virtual void stop() = 0;

    /// Optional one-line status for the overlay. Empty string = no row.
    /// Network consumers should return useful runtime info: destination,
    /// rate, packet count, last error.
    virtual std::string status_line() const { return {}; }

    /// Optional "send a single ping" hook for the overlay's test button.
    /// Returns true if a test message went out, false if the consumer
    /// doesn't support it or the test failed. Default: not supported.
    virtual bool send_test_packet() { return false; }

    /// True if the consumer's worker thread has decided it cannot continue
    /// (e.g., initial connect failed, socket creation failed). Polled by
    /// the registry every frame; if true, the registry mutates the
    /// matching settings flag to false and stops the consumer so the UI
    /// reflects reality. Default: never self-disarms.
    virtual bool wants_self_disarm() const { return false; }

    /// Apply the self-disarm to the settings struct: clear "this
    /// consumer's enable flag." Called by the registry when
    /// wants_self_disarm() returned true. Default: no-op.
    virtual void disarm_in_settings(config::Settings&) const {}
};

}  // namespace lw::output
