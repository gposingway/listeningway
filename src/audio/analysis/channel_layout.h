#pragma once
#include <atomic>

namespace lw { namespace audio { namespace analysis {

enum class Layout71 { Unknown = 0, Classic = 1, Surround = 2 };

// Global shared indicator for 7.1 layout detected at capture start.
// Classic: FL,FR,FC,LFE,BL,BR,SL,SR (rear first)
// Surround: FL,FR,FC,LFE,SL,SR,BL,BR (side first)
inline std::atomic<int>& Layout71State() {
    static std::atomic<int> state{static_cast<int>(Layout71::Unknown)};
    return state;
}

inline void SetLayout71(Layout71 l) { Layout71State().store(static_cast<int>(l), std::memory_order_relaxed); }
inline Layout71 GetLayout71() { return static_cast<Layout71>(Layout71State().load(std::memory_order_relaxed)); }

}}}
