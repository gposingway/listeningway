#include "consumer_registry.h"

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

}  // namespace lw::output
