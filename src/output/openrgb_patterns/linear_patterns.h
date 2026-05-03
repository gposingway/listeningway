// Linear-strip pattern dispatcher (ADR-0014).
//
// One-dimensional zones (RAM, case strips, fan rings, motherboard
// accents, ARGB headers). Eight patterns ordered active → soothing.
//
// Output is written into the caller-provided span, sized to the
// zone's leds_count.
#pragma once

#include <span>

#include "pattern_common.h"

#include "../../config/settings.h"  // for OpenRgbConfig::LinearPattern

namespace lw { struct AudioSnapshot; }

namespace lw::output::patterns {

void render_linear(
    config::OpenRgbConfig::LinearPattern pattern,
    const AudioSnapshot& snap,
    std::span<ColorRgb>  out,
    PatternState&        state,
    float                brightness,
    float                dt_sec) noexcept;

}  // namespace lw::output::patterns
