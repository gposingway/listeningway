#include "overlay_consumer.h"

#include "overlay.h"
#include "../audio/pipeline/audio_system.h"

namespace lw::overlay {

OverlayConsumer* OverlayConsumer::s_instance = nullptr;

bool OverlayConsumer::start(AudioSystem& system, HMODULE addon_module) {
    if (registered_) return true;

    system_ = &system;
    addon_ = addon_module;
    s_instance = this;
    reshade::register_overlay(nullptr, &OverlayConsumer::on_overlay);
    registered_ = true;
    return true;
}

void OverlayConsumer::stop() {
    if (!registered_) return;
    reshade::unregister_overlay(nullptr, &OverlayConsumer::on_overlay);
    registered_ = false;
    s_instance = nullptr;
    system_ = nullptr;
    addon_ = nullptr;
}

std::string OverlayConsumer::status_line() const {
    return registered_ ? "rendering" : "inactive";
}

void OverlayConsumer::on_overlay(reshade::api::effect_runtime* runtime) {
    auto* self = s_instance;
    if (!self || !self->system_ || !self->store_ || !self->registry_) return;
    draw_overlay(runtime, *self->system_, *self->store_, *self->registry_, self->addon_);
}

}  // namespace lw::overlay
