#include "beat_detector_spectral_flux_auto.h"
#include <algorithm>
#include <cmath>

void BeatDetectorSpectralFluxAuto::Process(const std::vector<float>& magnitudes, float flux_avg, float flux_low_avg, float dt) {
    time_ += dt;

    // Exponential moving averages for stability
    const float alpha = 0.1f;
    moving_flux_ = (1.0f - alpha) * moving_flux_ + alpha * flux_avg;
    moving_flux_low_ = (1.0f - alpha) * moving_flux_low_ + alpha * flux_low_avg;

    // Dynamic threshold relative to moving average
    float threshold = moving_flux_ * 1.3f;
    float strength = std::max(0.0f, flux_avg - threshold);

    // Peak detection with refractory period (avoid too frequent beats)
    bool is_beat_now = false;
    if (strength > 0.0f && (time_ - last_peak_time_) > 0.15f) {
        is_beat_now = true;
        last_peak_time_ = time_;
    }

    // Beat behavior: impulse at detection (1.0), then decay
    if (is_beat_now) {
        result_.beat = 1.0f;
    } else {
        // Decay over time when no new beat
        result_.beat = std::max(0.0f, result_.beat - 1.2f * dt);
    }

    // Naive tempo estimate based on last interval
    if (is_beat_now) {
        float interval = time_ - last_peak_time_;
        if (interval > 0.2f && interval < 2.0f) {
            result_.tempo_bpm = 60.0f / interval;
            result_.confidence = std::clamp(result_.beat, 0.0f, 1.0f);
            result_.tempo_detected = true;
        } else {
            result_.tempo_detected = false;
            result_.confidence = 0.0f;
        }
    }

    // Phase approximation (wrap every second at tempo)
    result_.beat_phase = 0.0f;
}
