#pragma once

#include <memory>
#include <vector>

struct BeatDetectorResult {
    float beat = 0.0f;            // [0,1]
    float tempo_bpm = 0.0f;       // BPM estimate
    float confidence = 0.0f;      // [0,1]
    float beat_phase = 0.0f;      // [0,1]
    bool tempo_detected = false;  // has stable tempo
};

class IBeatDetector {
public:
    virtual ~IBeatDetector() = default;
    virtual void Start() {}
    virtual void Stop() {}

    // Process one frame worth of analysis inputs
    // magnitudes: half-FFT magnitudes; flux_avg: spectral flux avg; flux_low_avg: low-band flux avg; dt: seconds
    virtual void Process(const std::vector<float>& magnitudes,
                         float flux_avg,
                         float flux_low_avg,
                         float dt) = 0;

    virtual BeatDetectorResult GetResult() const = 0;

    static std::unique_ptr<IBeatDetector> Create(int algorithm);
};
