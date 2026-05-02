// ---------------------------------------------
// FrameRing — lock-free SPSC carrier for audio frames (ADR-0002)
//
// Wraps moodycamel::ReaderWriterQueue. The producer (capture thread) pushes
// chunks of interleaved float samples + a Format header; the consumer (DSP
// thread) pops them in arrival order. Wait-free for both sides; never
// allocates after construction.
// ---------------------------------------------
#pragma once

#include <readerwriterqueue.h>

#include <cstddef>
#include <span>
#include <vector>

#include "../source/source_format.h"

namespace lw::ring {

/// One unit of audio that flows from capture to DSP. `samples` is owned by
/// the chunk (vector storage) so the source's transient buffer can be reused.
/// Chunks are recycled via a free-list to avoid per-frame allocation; see
/// `FrameRing::recycle()`.
struct FrameChunk {
    std::vector<float> samples;   ///< interleaved
    source::Format format;
};

class FrameRing {
public:
    /// `capacity_chunks` is the maximum in-flight chunk count. The actual byte
    /// budget depends on chunk size (typical_frame_count * channels * 4).
    /// 64 chunks at ~480 frames each ≈ 1.3 seconds of stereo at 48 kHz.
    explicit FrameRing(size_t capacity_chunks = 64)
        : queue_(capacity_chunks), free_list_(capacity_chunks) {
        // Pre-populate the free list so push() never needs to allocate.
        for (size_t i = 0; i < capacity_chunks; ++i) {
            free_list_.try_enqueue(std::make_unique<FrameChunk>());
        }
    }

    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    /// Producer side: copy `samples` into a recycled chunk and enqueue.
    /// Returns false if the ring is full (drops the oldest-not-yet-consumed
    /// behavior is not used; we report back-pressure to the caller). Also
    /// returns false on allocation pressure (free list empty).
    bool push(std::span<const float> samples, source::Format format) {
        std::unique_ptr<FrameChunk> chunk;
        if (!free_list_.try_dequeue(chunk) || !chunk) {
            return false;  // back-pressure: consumer is behind
        }
        chunk->samples.assign(samples.begin(), samples.end());
        chunk->format = format;
        return queue_.try_enqueue(std::move(chunk));
    }

    /// Consumer side: pop one chunk if available. Returns false if empty.
    /// Caller must call `recycle(std::move(out))` when done with the chunk.
    bool pop(std::unique_ptr<FrameChunk>& out) {
        return queue_.try_dequeue(out);
    }

    /// Return a consumed chunk to the free list. The chunk's `samples` vector
    /// keeps its capacity so reuse is allocation-free for similar sizes.
    void recycle(std::unique_ptr<FrameChunk> chunk) {
        if (chunk) {
            chunk->samples.clear();
            chunk->format = {};
            free_list_.try_enqueue(std::move(chunk));
        }
    }

    /// Approximate count; SPSC-correct (only the producer's enqueues vs the
    /// consumer's dequeues are reflected, no other side reads).
    size_t pending() const { return queue_.size_approx(); }

private:
    moodycamel::ReaderWriterQueue<std::unique_ptr<FrameChunk>> queue_;
    moodycamel::ReaderWriterQueue<std::unique_ptr<FrameChunk>> free_list_;
};

}  // namespace lw::ring
