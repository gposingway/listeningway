#include "tempo_worker.h"
#include <algorithm>
#include <cmath>

namespace lw { namespace audio { namespace analysis {

TempoWorker::TempoWorker() : queue_(4096), running_(false) {
    window_.reserve(4096);
    beat_times_.reserve(64);
}

TempoWorker::~TempoWorker() {
    Stop();
}

void TempoWorker::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    thread_ = std::thread(&TempoWorker::Run, this);
}

void TempoWorker::Stop() {
    if (running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();
    }
}

void TempoWorker::Submit(float flux_low, float dt) {
    // Best effort push, drop if full
    queue_.push(Sample{flux_low, dt});
}

TempoResult TempoWorker::GetResult() const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    return result_;
}

void TempoWorker::Run() {
    // Simple threshold-based peak picking with moving average baseline
    float threshold_k = 1.5f; // sensitivity
    float min_bpm = 60.0f, max_bpm = 200.0f;
    float min_period = 60.0f / max_bpm; // seconds
    float max_period = 60.0f / min_bpm; // seconds

    float time_since_last_peak = max_period;

    while (running_.load()) {
        Sample s;
        if (!queue_.pop(s)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        time_accum_ += s.dt;
        time_since_last_peak += s.dt;

        // Update moving average baseline
        avg_flux_ = (1.0f - ema_alpha_) * avg_flux_ + ema_alpha_ * s.flux;
        float excess = s.flux - avg_flux_;
        float dynamic_thresh = threshold_k * std::max(0.0f, avg_flux_);

        bool is_peak = (excess > dynamic_thresh) && (time_since_last_peak >= min_period);
        if (is_peak) {
            beat_times_.push_back(time_accum_);
            if (beat_times_.size() > 16) beat_times_.erase(beat_times_.begin());
            time_since_last_peak = 0.0f;
        }

        // Update tempo estimate every ~200ms
        static float update_accum = 0.0f;
        update_accum += s.dt;
        if (update_accum >= 0.2f) {
            update_accum = 0.0f;
            float bpm = 0.0f; float conf = 0.0f;
            if (beat_times_.size() >= 4) {
                // Compute average interval between recent peaks
                std::vector<float> intervals;
                intervals.reserve(beat_times_.size());
                for (size_t i = 1; i < beat_times_.size(); ++i) {
                    intervals.push_back(beat_times_[i] - beat_times_[i-1]);
                }
                // Robust estimate: median interval
                std::nth_element(intervals.begin(), intervals.begin() + intervals.size()/2, intervals.end());
                float median = intervals[intervals.size()/2];
                median = std::clamp(median, min_period, max_period);
                bpm = 60.0f / median;

                // Confidence: consistency of intervals (low variance => high confidence)
                float mean = 0.0f; for (float v : intervals) mean += v; mean /= intervals.size();
                float var = 0.0f; for (float v : intervals) { float d = v - mean; var += d*d; } var /= intervals.size();
                float stdev = std::sqrt(var);
                conf = std::clamp(1.0f - (stdev / std::max(0.001f, mean)), 0.0f, 1.0f);
            }

            std::lock_guard<std::mutex> lock(result_mutex_);
            if (bpm > 0.0f) {
                result_.bpm = bpm;
                result_.confidence = 0.5f * result_.confidence + 0.5f * conf; // smooth
                result_.detected = true;
                // Phase estimation left to beat detector for now
            } else {
                // Decay confidence when no beats detected
                result_.confidence *= 0.95f;
                if (result_.confidence < 0.05f) result_.detected = false;
            }
        }
    }
}

}}} // namespace lw::audio::analysis
