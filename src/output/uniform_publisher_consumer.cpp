#include "uniform_publisher_consumer.h"

#include "uniform_publisher.h"
#include "../audio/pipeline/audio_system.h"

namespace lw::output {

UniformPublisherConsumer* UniformPublisherConsumer::s_instance = nullptr;

bool UniformPublisherConsumer::start(AudioSystem& system, HMODULE) {
    if (registered_) return true;

    system_ = &system;
    start_time_ = std::chrono::steady_clock::now();
    s_instance = this;
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(
        &UniformPublisherConsumer::on_begin_effects);
    registered_ = true;
    return true;
}

void UniformPublisherConsumer::stop() {
    if (!registered_) return;
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(
        &UniformPublisherConsumer::on_begin_effects);
    registered_ = false;
    s_instance = nullptr;
    system_ = nullptr;
}

std::string UniformPublisherConsumer::status_line() const {
    return registered_ ? "publishing to active effects" : "inactive";
}

void UniformPublisherConsumer::on_begin_effects(reshade::api::effect_runtime* runtime,
                                                  reshade::api::command_list*,
                                                  reshade::api::resource_view,
                                                  reshade::api::resource_view) {
    auto* self = s_instance;
    if (!self || !self->system_) return;
    const auto snap = self->system_->snapshot();
    publish_uniforms(runtime, snap, self->start_time_,
                     std::chrono::steady_clock::now());
}

}  // namespace lw::output
