// ---------------------------------------------
// UniformPublisherConsumer — wraps publish_uniforms() under the
// IOutputConsumer interface (ADR-0010). Always-on internal consumer: it
// registers a ReShade reshade_begin_effects event in start() and
// unregisters it in stop(). Snapshot is acquired lazily from AudioSystem
// inside the event callback (render-thread cadence).
// ---------------------------------------------
#pragma once

#include <chrono>

#include <reshade.hpp>

#include "i_output_consumer.h"

namespace lw::output {

class UniformPublisherConsumer final : public IOutputConsumer {
public:
    std::string_view id() const override { return "uniform_publisher"; }
    std::string_view display_name() const override { return "ReShade Uniforms"; }
    bool is_user_configurable() const override { return false; }

    bool start(AudioSystem& system, HMODULE addon_module) override;
    void stop() override;

    std::string status_line() const override;

private:
    AudioSystem* system_ = nullptr;
    std::chrono::steady_clock::time_point start_time_{};
    bool registered_ = false;

    // The render-thread callback bridges back to instance state through this
    // singleton pointer. Only one UniformPublisherConsumer is ever active at
    // a time, so this is safe; it's null when the consumer is stopped.
    static UniformPublisherConsumer* s_instance;
    static void on_begin_effects(reshade::api::effect_runtime* runtime,
                                  reshade::api::command_list*,
                                  reshade::api::resource_view,
                                  reshade::api::resource_view);
};

}  // namespace lw::output
