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
};

}  // namespace lw::output
