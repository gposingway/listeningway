// ---------------------------------------------
// Overlay — minimal v2 ImGui status panel.
//
// First-test scope: dropdown for source selection, live readout of the
// snapshot fields (volume / volume_norm / beat / tempo / pan / band
// histogram). Tunables UI (sliders for every Setting<T>) is deferred to
// the post-first-test pass.
// ---------------------------------------------
#pragma once

#include <reshade.hpp>

namespace lw {

class AudioSystem;
namespace config { class Store; }

void draw_overlay(reshade::api::effect_runtime* runtime,
                  AudioSystem& system,
                  config::Store& store);

}  // namespace lw
