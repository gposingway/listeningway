#pragma once
#include "beat_detector.h"

class BeatDetectorSpectralFluxAuto : public IBeatDetector {
public:
    void Start() override { time_ = 0.0f; last_peak_time_ = -10.0f; }
    void Process(const std::vector<float>& magnitudes, float flux_avg, float flux_low_avg, float dt) override;
    BeatDetectorResult GetResult() const override { return result_; }
private:
    BeatDetectorResult result_{};
    float time_ = 0.0f;
    float last_peak_time_ = -10.0f;
    float moving_flux_ = 0.0f;
    float moving_flux_low_ = 0.0f;
};
