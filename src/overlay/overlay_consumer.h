// ---------------------------------------------
// OverlayConsumer — wraps draw_overlay() under the IOutputConsumer
// interface (ADR-0010). Always-on internal consumer: registers / unregisters
// the ReShade overlay callback in start()/stop().
// ---------------------------------------------
#pragma once

#include <reshade.hpp>

#include "../output/i_output_consumer.h"

namespace lw::output { class ConsumerRegistry; }

namespace lw {
namespace config { class Store; }
}

namespace lw::overlay {

class OverlayConsumer final : public output::IOutputConsumer {
public:
    /// Construct with the live Store (for reading and mutating settings) and
    /// the consumer registry (for displaying network-consumer status rows).
    OverlayConsumer(config::Store& store, output::ConsumerRegistry& registry)
        : store_(&store), registry_(&registry) {}

    std::string_view id() const override { return "overlay"; }
    std::string_view display_name() const override { return "ImGui Overlay"; }
    bool is_user_configurable() const override { return false; }

    bool start(AudioSystem& system, HMODULE addon_module) override;
    void stop() override;

    std::string status_line() const override;

private:
    config::Store*           store_      = nullptr;
    output::ConsumerRegistry* registry_  = nullptr;
    AudioSystem*             system_     = nullptr;
    HMODULE                  addon_      = nullptr;
    bool                     registered_ = false;

    static OverlayConsumer* s_instance;
    static void on_overlay(reshade::api::effect_runtime* runtime);
};

}  // namespace lw::overlay
