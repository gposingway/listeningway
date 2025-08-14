#include "beat_detector_simple_energy.h"
#include <algorithm>

void BeatDetectorSimpleEnergy::Process(const std::vector<float>& magnitudes, float flux_avg, float flux_low_avg, float dt) {
    // Use low-band flux as a proxy for bass beats
    float target = std::clamp(flux_low_avg * 5.0f, 0.0f, 1.0f);
    // Simple attack/decay
    if (target > result_.beat) {
        result_.beat += (target - result_.beat) * 0.6f; // fast attack
    } else {
        result_.beat = std::max(0.0f, result_.beat - decay_ * dt); // decay over time
    }
    // No robust tempo detection in simple mode
    result_.tempo_bpm = 0.0f;
    result_.confidence = result_.beat;
    result_.tempo_detected = false;
    result_.beat_phase = 0.0f;
}
