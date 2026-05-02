// ---------------------------------------------
// StageProfile — per-stage timing carried in AudioSnapshot.
//
// Snapshot must remain trivially copyable for the seqlock memcpy contract,
// so the stage name is a fixed char buffer (truncated). Pipeline records
// EMA-smoothed wall-clock per stage and the DSP thread copies these into
// the published snapshot.
// ---------------------------------------------
#pragma once

#include <array>
#include <cstdint>

namespace lw::dsp {

struct StageTiming {
    char  name[24]{};   ///< null-terminated, truncated if longer
    float micros = 0.0f; ///< EMA-smoothed
};

constexpr size_t kMaxStages = 16;
using StageTimings = std::array<StageTiming, kMaxStages>;

}  // namespace lw::dsp
