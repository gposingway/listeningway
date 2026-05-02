// Simple single-producer single-consumer lock-free ring buffer
#pragma once
#include <vector>
#include <atomic>
#include <cstddef>

namespace lw { namespace audio { namespace analysis {

template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity)
        : capacity_(RoundUpToPow2(capacity)), mask_(capacity_ - 1), buffer_(capacity_), head_(0), tail_(0) {}

    bool push(const T& v) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false; // full
        buffer_[head] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false; // empty
        out = buffer_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    bool empty() const { return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire); }
    bool full() const { return ((head_.load(std::memory_order_acquire) + 1) & mask_) == tail_.load(std::memory_order_acquire); }
    size_t capacity() const { return capacity_; }

private:
    static size_t RoundUpToPow2(size_t v) {
        if (v < 2) return 2;
        v--;
        v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= (sizeof(size_t) == 8 ? (v >> 32) : 0);
        return v + 1;
    }

    const size_t capacity_;
    const size_t mask_;
    std::vector<T> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

}}} // namespace lw::audio::analysis
