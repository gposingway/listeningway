#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include "ring_buffer.h"

namespace lw { namespace audio { namespace analysis {

struct TempoResult {
    float bpm = 0.0f;
    float confidence = 0.0f;
    float phase = 0.0f;
    bool detected = false;
};

// Background worker that estimates tempo from low-frequency spectral flux
class TempoWorker {
public:
    TempoWorker();
    ~TempoWorker();

    void Start();
    void Stop();

    // Submit latest flux sample and its time delta
    void Submit(float flux_low, float dt);

    // Thread-safe snapshot of latest result
    TempoResult GetResult() const;

private:
    struct Sample { float flux; float dt; };

    void Run();

    // Ring buffer for SPSC handoff
    SpscRingBuffer<Sample> queue_;

    // Worker thread
    std::atomic_bool running_;
    std::thread thread_;

    // Result storage
    mutable std::mutex result_mutex_;
    TempoResult result_;

    // Internal state for simple periodicity estimation
    std::vector<float> window_;
    float time_accum_ = 0.0f;
    float avg_flux_ = 0.0f;
    float ema_alpha_ = 0.05f; // smoothing for average

    // Beat peak timestamps (seconds)
    std::vector<float> beat_times_;
};

}}} // namespace lw::audio::analysis
