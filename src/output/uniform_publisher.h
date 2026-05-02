// ---------------------------------------------
// UniformPublisher — write AudioSnapshot fields to ReShade shader uniforms.
// Runs on the render thread (ReShade reshade_begin_effects event).
// ---------------------------------------------
#pragma once

#include <reshade.hpp>

#include "../audio/snapshot/audio_snapshot.h"

namespace lw::output {

/// Walks all uniform variables on `runtime`, looks up their `source`
/// annotation, and writes the matching field from `snap`. Wall-clock derived
/// time/phase values are computed from `start_time` and `now`.
void publish_uniforms(reshade::api::effect_runtime* runtime,
                      const AudioSnapshot& snap,
                      std::chrono::steady_clock::time_point start_time,
                      std::chrono::steady_clock::time_point now);

}  // namespace lw::output
