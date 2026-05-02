// ---------------------------------------------
// Listeningway v2 — top-level constants
//
// Day 0 stub. Subsequent days will introduce per-layer constants files
// (config defaults, capture buffer sizes, FFT defaults). This header holds
// only the values that need to be visible to the build system itself —
// specifically, DEFAULT_NUM_BANDS is read by build.bat to size the
// shader-side Listeningway_FreqBands[] array in the generated .fxh.
// ---------------------------------------------
#pragma once

#include <cstddef>

// Compile-time ceiling for the shader-side Listeningway_FreqBands[] array.
// build.bat extracts this value via regex and substitutes it into the
// ListeningwayUniforms.fxh template at build time. The runtime band count
// (Settings::frequency::bands, when v2 settings land) may be lower; values
// above this are clamped at the uniform boundary to prevent shader array
// overrun.
//
// Format contract: this declaration must match the regex
//   constexpr\s+size_t\s+DEFAULT_NUM_BANDS\s*=\s*(\d+)
// in build.bat. Do not change the type spelling, the name, or the form
// without updating build.bat in lockstep.
constexpr size_t DEFAULT_NUM_BANDS = 64;
