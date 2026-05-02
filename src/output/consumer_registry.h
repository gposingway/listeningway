// ---------------------------------------------
// ConsumerRegistry — owns IOutputConsumer instances; coordinates lifecycle
// and reacts to settings toggles (ADR-0010).
// ---------------------------------------------
#pragma once

#include <memory>
#include <vector>

#include <windows.h>

#include "i_output_consumer.h"

namespace lw {
class AudioSystem;
namespace config { struct Settings; class Store; }
}

namespace lw::output {

class ConsumerRegistry {
public:
    /// Take ownership of the consumer. Order of insertion is honored at
    /// start_all() time, which matters for the overlay's status panel
    /// presentation.
    void add(std::unique_ptr<IOutputConsumer> consumer);

    /// Start every registered consumer that should currently be running:
    /// internal consumers unconditionally; toggleable consumers per
    /// `is_enabled(settings)`.
    void start_all(AudioSystem& system, HMODULE addon_module,
                   const config::Settings& settings);

    /// Stop every registered consumer. Safe to call multiple times.
    void stop_all();

    /// React to live settings change: toggleable consumers transition
    /// start↔stop to match their new `is_enabled(settings)` state.
    /// Internal consumers are left untouched.
    void on_settings_changed(AudioSystem& system, HMODULE addon_module,
                             const config::Settings& settings);

    /// Read access for overlay status display.
    const std::vector<std::unique_ptr<IOutputConsumer>>& consumers() const {
        return consumers_;
    }

    /// Look up a consumer by id() string. Returns nullptr if not found.
    IOutputConsumer* find_by_id(std::string_view id) const;

    /// Once-per-frame maintenance: poll consumers for self-disarm
    /// requests. If any consumer's worker has decided it cannot continue
    /// (initial connect failure, socket creation failure), mutate the
    /// matching settings flag to false via `store.mutate()`, then run
    /// `on_settings_changed()` so the consumer's stop() runs and the
    /// registry's `active_` mirrors reality.
    void poll_self_disarm(AudioSystem& system, HMODULE addon_module,
                           config::Store& store);

private:
    std::vector<std::unique_ptr<IOutputConsumer>> consumers_;
    std::vector<bool> active_;  ///< parallel to consumers_; tracks start state
};

}  // namespace lw::output
