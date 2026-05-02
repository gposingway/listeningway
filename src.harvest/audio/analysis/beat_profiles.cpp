#include "beat_profiles.h"
#include <algorithm>

using Listeningway::Configuration;

const char* ToString(BeatProfile profile) {
    switch (profile) {
        case BeatProfile::GeneralMusic: return "general";
        case BeatProfile::ElectronicEDM: return "edm";
        case BeatProfile::AcousticOrganic: return "acoustic";
        case BeatProfile::Custom: return "custom";
        default: return "custom";
    }
}

bool FromString(const std::string& s, BeatProfile& out) {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    if (v == "general") { out = BeatProfile::GeneralMusic; return true; }
    if (v == "edm" || v == "electronic") { out = BeatProfile::ElectronicEDM; return true; }
    if (v == "acoustic" || v == "organic") { out = BeatProfile::AcousticOrganic; return true; }
    if (v == "custom") { out = BeatProfile::Custom; return true; }
    return false;
}

void ApplyBeatProfile(BeatProfile profile, Configuration& cfg) {
    if (profile == BeatProfile::Custom) return; // do nothing

    // Reasonable presets based on docs/yagni_analysis.md intent
    switch (profile) {
        case BeatProfile::GeneralMusic: {
            cfg.beat.profile = ToString(profile);
            cfg.beat.algorithm = static_cast<int>(BeatDetectionAlgorithm::SpectralFluxAuto);
            cfg.beat.spectralFluxThreshold = 0.06f;
            cfg.beat.spectralFluxDecayMultiplier = 1.5f;
            cfg.beat.tempoChangeThreshold = 0.25f;
            cfg.beat.beatInductionWindow = 0.12f;
            cfg.beat.octaveErrorWeight = 0.75f;
            cfg.beat.minFreq = 40.0f;
            cfg.beat.maxFreq = 250.0f;
            cfg.beat.fluxLowAlpha = 0.15f;
            cfg.beat.fluxLowThresholdMultiplier = 1.5f;
            break;
        }
        case BeatProfile::ElectronicEDM: {
            cfg.beat.profile = ToString(profile);
            cfg.beat.algorithm = static_cast<int>(BeatDetectionAlgorithm::SimpleEnergy);
            cfg.beat.falloffDefault = 2.5f;
            cfg.beat.timeScale = 2e-9f;
            cfg.beat.timeInitial = 0.25f;
            cfg.beat.timeMin = 0.06f;
            cfg.beat.timeDivisor = 0.2f;
            cfg.beat.minFreq = 30.0f;
            cfg.beat.maxFreq = 180.0f;
            cfg.beat.fluxLowAlpha = 0.1f;
            cfg.beat.fluxLowThresholdMultiplier = 1.2f;
            break;
        }
        case BeatProfile::AcousticOrganic: {
            cfg.beat.profile = ToString(profile);
            cfg.beat.algorithm = static_cast<int>(BeatDetectionAlgorithm::SpectralFluxAuto);
            cfg.beat.spectralFluxThreshold = 0.08f;
            cfg.beat.spectralFluxDecayMultiplier = 1.2f;
            cfg.beat.tempoChangeThreshold = 0.35f;
            cfg.beat.beatInductionWindow = 0.18f;
            cfg.beat.octaveErrorWeight = 0.85f;
            cfg.beat.minFreq = 60.0f;
            cfg.beat.maxFreq = 400.0f;
            cfg.beat.fluxLowAlpha = 0.2f;
            cfg.beat.fluxLowThresholdMultiplier = 1.8f;
            break;
        }
        default: break;
    }
}
