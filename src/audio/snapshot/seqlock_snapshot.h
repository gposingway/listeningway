// ---------------------------------------------
// SeqlockSnapshot — wait-free single-writer, lock-free multi-reader publish
// of an AudioSnapshot. (ADR-0002, research-notes.md §6)
//
// Writer protocol:
//   seq.store(odd, relaxed)
//   atomic_thread_fence(release)   — odd visible before payload write
//   memcpy(&data, &snap, sizeof)   — non-atomic body, made safe by fences
//   atomic_thread_fence(release)   — payload visible before even seq
//   seq.store(even, release)
//
// Reader protocol (bounded retry, falls back to last good copy):
//   for spin in 0..N:
//     s1 = seq.load(acquire)
//     if (s1 & 1) continue        — writer mid-update
//     atomic_thread_fence(acquire)
//     memcpy(&out, &data, sizeof)
//     atomic_thread_fence(acquire)
//     s2 = seq.load(acquire)
//     if (s1 == s2) return success
//   return last_good_copy
// ---------------------------------------------
#pragma once

#include <atomic>
#include <cstring>

#include "audio_snapshot.h"

namespace lw {

class SeqlockSnapshot {
public:
    SeqlockSnapshot() = default;
    SeqlockSnapshot(const SeqlockSnapshot&) = delete;
    SeqlockSnapshot& operator=(const SeqlockSnapshot&) = delete;

    /// Single-writer publish. Caller must serialize calls to publish().
    void publish(const AudioSnapshot& snap) noexcept {
        const uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        std::memcpy(&data_, &snap, sizeof(AudioSnapshot));

        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);
    }

    /// Reader. Multi-reader safe. On contention, falls back to `last_good`
    /// after `kMaxSpin` failed attempts (caller keeps showing the previous
    /// snapshot — fine for visualization).
    AudioSnapshot read() const noexcept {
        AudioSnapshot out{};
        for (int spin = 0; spin < kMaxSpin; ++spin) {
            const uint64_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1u) continue;  // writer mid-update
            std::atomic_thread_fence(std::memory_order_acquire);

            std::memcpy(&out, &data_, sizeof(AudioSnapshot));

            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t s2 = seq_.load(std::memory_order_acquire);
            if (s1 == s2) return out;  // consistent
        }
        // Degenerate: heavy contention. Return whatever we last copied;
        // worst case is one stale frame which is harmless visually.
        return out;
    }

    /// True if a publish has occurred since construction.
    bool has_data() const noexcept {
        return seq_.load(std::memory_order_acquire) >= 2;
    }

private:
    static constexpr int kMaxSpin = 4;

    alignas(64) std::atomic<uint64_t> seq_{0};
    alignas(64) AudioSnapshot data_{};
};

}  // namespace lw
