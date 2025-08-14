#pragma once
#include "beat_detector.h"

class BeatDetectorSimpleEnergy : public IBeatDetector {
public:
    void Process(const std::vector<float>& magnitudes, float flux_avg, float flux_low_avg, float dt) override;
    BeatDetectorResult GetResult() const override { return result_; }
private:
    BeatDetectorResult result_{};
    float decay_ = 2.0f; // seconds^-1
};
