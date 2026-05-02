#include "consumer_registry.h"

#include "../config/settings.h"
#include "../config/store.h"

namespace lw::output {

void ConsumerRegistry::add(std::unique_ptr<IOutputConsumer> consumer) {
    if (consumer) {
        consumers_.push_back(std::move(consumer));
        active_.push_back(false);
    }
}

void ConsumerRegistry::start_all(AudioSystem& system, HMODULE addon_module,
                                  const config::Settings& settings) {
    if (active_.size() != consumers_.size()) {
        active_.assign(consumers_.size(), false);
    }
    for (size_t i = 0; i < consumers_.size(); ++i) {
        if (active_[i]) continue;
        auto& c = *consumers_[i];
        const bool should_run = !c.is_user_configurable() || c.is_enabled(settings);
        if (should_run) {
            active_[i] = c.start(system, addon_module);
        }
    }
}

void ConsumerRegistry::stop_all() {
    // Stop in reverse order so consumers that depend on the others (none
    // currently, but future-proofing) tear down first.
    for (size_t i = consumers_.size(); i-- > 0;) {
        if (active_[i]) {
            consumers_[i]->stop();
            active_[i] = false;
        }
    }
}

void ConsumerRegistry::on_settings_changed(AudioSystem& system, HMODULE addon_module,
                                             const config::Settings& settings) {
    for (size_t i = 0; i < consumers_.size(); ++i) {
        auto& c = *consumers_[i];
        if (!c.is_user_configurable()) continue;  // always-on consumers ignore toggles

        const bool should_run = c.is_enabled(settings);
        if (should_run && !active_[i]) {
            active_[i] = c.start(system, addon_module);
        } else if (!should_run && active_[i]) {
            c.stop();
            active_[i] = false;
        }
    }
}

IOutputConsumer* ConsumerRegistry::find_by_id(std::string_view id) const {
    for (auto& c : consumers_) {
        if (c->id() == id) return c.get();
    }
    return nullptr;
}

void ConsumerRegistry::poll_self_disarm(AudioSystem& system, HMODULE addon_module,
                                          config::Store& store) {
    bool any = false;
    for (auto& c : consumers_) {
        if (c->wants_self_disarm()) {
            store.mutate([&](config::Settings& s) { c->disarm_in_settings(s); });
            any = true;
        }
    }
    if (any) {
        // Apply the new settings: any consumer whose flag was just cleared
        // is stopped here, syncing `active_[]` with worker-thread reality.
        on_settings_changed(system, addon_module, store.snapshot());
    }
}

}  // namespace lw::output
