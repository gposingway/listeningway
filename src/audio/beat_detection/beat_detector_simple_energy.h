#pragma once
#include "beat_detector.h"
#include "settings.h"
#include <mutex>
#include <vector>
#include <chrono>

/**
 * @brief Simple energy-based beat detector.
 *
 * Detects beats by identifying spikes in low-frequency spectral flux above an adaptive threshold.
 * This is the original Listeningway algorithm, tuned for strong, regular beats (e.g., EDM, pop).
 */
class BeatDetectorSimpleEnergy : public IBeatDetector {
public:
    BeatDetectorSimpleEnergy();
    ~BeatDetectorSimpleEnergy() override;

    /// Start the beat detector.
    void Start() override;
    /// Stop the beat detector.
    void Stop() override;
    /**
     * @brief Process audio data for beat detection.
     * @param magnitudes FFT magnitudes
     * @param flux Spectral flux value
     * @param flux_low Low-frequency band-limited flux
     * @param dt Time delta since last frame
     */
    void Process(const std::vector<float>& magnitudes, float flux, float flux_low, float dt) override;
    /// Get the current beat detection result.
    BeatDetectorResult GetResult() const override;

private:
    mutable std::mutex mutex_;
    BeatDetectorResult result_;
    bool is_running_ = false;

    // Beat tracking state
    float last_beat_time_ = 0.0f;
    float total_time_ = 0.0f;
    float beat_value_ = 0.0f;
    std::chrono::steady_clock::time_point last_beat_timestamp_;

    // Adaptive threshold and decay
    float flux_threshold_ = 0.0f;
    float beat_falloff_ = 0.0f;
};