#include "beat_detector.h"
#include <memory>
#include "beat_detector_simple_energy.h"
#include "beat_detector_spectral_flux_auto.h"

std::unique_ptr<IBeatDetector> IBeatDetector::Create(int algorithm) {
    if (algorithm == 0) {
        return std::make_unique<BeatDetectorSimpleEnergy>();
    } else {
        return std::make_unique<BeatDetectorSpectralFluxAuto>();
    }
}
