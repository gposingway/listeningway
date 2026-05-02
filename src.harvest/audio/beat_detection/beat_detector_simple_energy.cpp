#include "beat_detector_simple_energy.h"
#include <algorithm>

void BeatDetectorSimpleEnergy::Process(const std::vector<float>& magnitudes, float flux_avg, float flux_low_avg, float dt) {
    // Use low-band flux as a proxy: if above dynamic threshold, trigger beat
    float threshold = 0.12f; // heuristic
    if (flux_low_avg > threshold) {
        result_.beat = 1.0f; // impulse on detection
    } else {
        result_.beat = std::max(0.0f, result_.beat - decay_ * dt); // decay over time
    }
    // No robust tempo detection in simple mode
    result_.tempo_bpm = 0.0f;
    result_.confidence = result_.beat;
    result_.tempo_detected = false;
    result_.beat_phase = 0.0f;
}
