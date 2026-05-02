#include "Configuration.h"
#include "logging.h"
#include "settings.h"
#include <fstream>
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Listeningway {

bool Configuration::Save() const {
    return SaveToJson(GetDefaultConfigPath());
}

bool Configuration::Load() {
    return LoadFromJson(GetDefaultConfigPath());
}

void Configuration::ResetToDefaults() {
    *this = Configuration{};
    LOG_DEBUG("[Configuration] Reset all settings to defaults");
}

bool Configuration::Validate() {
    bool isValid = true;

    audio.panSmoothing = std::clamp(audio.panSmoothing, 0.0f, 1.0f);
    audio.panOffset = std::clamp(audio.panOffset, -1.0f, 1.0f);

    beat.algorithm = std::clamp(beat.algorithm, 0, 1);
    beat.falloffDefault = std::clamp(beat.falloffDefault, 0.1f, 10.0f);
    beat.timeScale = std::clamp(beat.timeScale, 1e-12f, 1e-6f);
    beat.timeInitial = std::clamp(beat.timeInitial, 0.1f, 2.0f);
    beat.timeMin = std::clamp(beat.timeMin, 0.01f, 1.0f);
    beat.timeDivisor = std::clamp(beat.timeDivisor, 0.01f, 1.0f);
    beat.spectralFluxThreshold = std::clamp(beat.spectralFluxThreshold, 0.01f, 0.5f);
    beat.spectralFluxDecayMultiplier = std::clamp(beat.spectralFluxDecayMultiplier, 0.1f, 10.0f);
    beat.tempoChangeThreshold = std::clamp(beat.tempoChangeThreshold, 0.1f, 1.0f);
    beat.beatInductionWindow = std::clamp(beat.beatInductionWindow, 0.05f, 0.5f);
    beat.octaveErrorWeight = std::clamp(beat.octaveErrorWeight, 0.1f, 1.0f);
    beat.minFreq = std::clamp(beat.minFreq, 0.0f, 22050.0f);
    beat.maxFreq = std::clamp(beat.maxFreq, 0.0f, 22050.0f);
    beat.fluxLowAlpha = std::clamp(beat.fluxLowAlpha, 0.01f, 1.0f);
    beat.fluxLowThresholdMultiplier = std::clamp(beat.fluxLowThresholdMultiplier, 0.5f, 5.0f);

    frequency.logStrength = std::clamp(frequency.logStrength, 0.2f, 3.0f);
    frequency.minFreq = std::clamp(frequency.minFreq, 10.0f, 500.0f);
    frequency.maxFreq = std::clamp(frequency.maxFreq, 2000.0f, 22050.0f);
    for (auto& band : frequency.equalizerBands) {
        band = std::clamp(band, 0.0f, 4.0f);
    }
    frequency.equalizerWidth = std::clamp(frequency.equalizerWidth, 0.05f, 0.5f);
    frequency.amplifier = std::clamp(frequency.amplifier, OVERLAY_AMPLIFIER_MIN, OVERLAY_AMPLIFIER_MAX);
    frequency.amplifierVolume = std::clamp(frequency.amplifierVolume, OVERLAY_AMPLIFIER_MIN, OVERLAY_AMPLIFIER_MAX);
    frequency.amplifierBands = std::clamp(frequency.amplifierBands, OVERLAY_AMPLIFIER_MIN, OVERLAY_AMPLIFIER_MAX);
    frequency.amplifierDirection = std::clamp(frequency.amplifierDirection, OVERLAY_AMPLIFIER_MIN, OVERLAY_AMPLIFIER_MAX);

    if (frequency.minFreq >= frequency.maxFreq) {
        frequency.maxFreq = frequency.minFreq + 1000.0f;
        isValid = false;
    }

    if (beat.minFreq >= beat.maxFreq) {
        beat.maxFreq = beat.minFreq + 100.0f;
        isValid = false;
    }

    return isValid;
}

std::string Configuration::GetDefaultConfigPath() {
    std::string path = GetSettingsPath();  // Listeningway.json next to DLL
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        std::filesystem::path dirPath(path.substr(0, pos + 1));
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }
    }
    return path;
}

bool Configuration::SaveToJson(const std::string& filepath) const {
    try {
        LOG_DEBUG("[Configuration] Attempting to save config to: " + filepath);
        json j;

        j["audio"] = {
            {"analysisEnabled", audio.analysisEnabled},
            {"captureProviderCode", audio.captureProviderCode},
            {"panSmoothing", audio.panSmoothing},
            {"panOffset", audio.panOffset},
            {"simdEnabled", audio.simdEnabled},
        };

        j["beat"] = {
            {"algorithm", beat.algorithm},
            {"profile", beat.profile},
            {"falloffDefault", beat.falloffDefault},
            {"timeScale", beat.timeScale},
            {"timeInitial", beat.timeInitial},
            {"timeMin", beat.timeMin},
            {"timeDivisor", beat.timeDivisor},
            {"spectralFluxThreshold", beat.spectralFluxThreshold},
            {"spectralFluxDecayMultiplier", beat.spectralFluxDecayMultiplier},
            {"tempoChangeThreshold", beat.tempoChangeThreshold},
            {"beatInductionWindow", beat.beatInductionWindow},
            {"octaveErrorWeight", beat.octaveErrorWeight},
            {"minFreq", beat.minFreq},
            {"maxFreq", beat.maxFreq},
            {"fluxLowAlpha", beat.fluxLowAlpha},
            {"fluxLowThresholdMultiplier", beat.fluxLowThresholdMultiplier},
            {"fluxMin", beat.fluxMin},
        };

        j["frequency"] = {
            {"logScaleEnabled", frequency.logScaleEnabled},
            {"logStrength", frequency.logStrength},
            {"minFreq", frequency.minFreq},
            {"maxFreq", frequency.maxFreq},
            {"equalizerBands", frequency.equalizerBands},
            {"equalizerWidth", frequency.equalizerWidth},
            {"amplifier", frequency.amplifier},
            {"amplifierVolume", frequency.amplifierVolume},
            {"amplifierBands", frequency.amplifierBands},
            {"amplifierDirection", frequency.amplifierDirection},
            {"bands", frequency.bands},
            {"fftSize", frequency.fftSize},
            {"bandNorm", frequency.bandNorm},
        };

        j["debug"] = {
            {"debugEnabled", debug.debugEnabled},
            {"overlayEnabled", debug.overlayEnabled},
        };

        std::ofstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("[Configuration] Failed to open file for writing: " + filepath);
            return false;
        }
        file << j.dump(2);
        LOG_DEBUG("[Configuration] Saved configuration to: " + filepath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[Configuration] Exception while saving: " + std::string(e.what()));
        return false;
    }
}

namespace {

template <typename T>
void load_if(const json& obj, const char* key, T& out) {
    auto it = obj.find(key);
    if (it != obj.end() && !it->is_null()) {
        try {
            out = it->get<T>();
        } catch (const std::exception&) {
            // leave default value on type mismatch
        }
    }
}

} // namespace

bool Configuration::LoadFromJson(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_WARNING("[Configuration] Config file not found, using defaults: " + filepath);
            return false;
        }

        json j;
        try {
            file >> j;
        } catch (const json::parse_error& e) {
            LOG_ERROR("[Configuration] JSON parse error: " + std::string(e.what()));
            return false;
        }

        if (j.contains("audio") && j["audio"].is_object()) {
            const auto& a = j["audio"];
            load_if(a, "analysisEnabled", audio.analysisEnabled);
            load_if(a, "captureProviderCode", audio.captureProviderCode);
            load_if(a, "panSmoothing", audio.panSmoothing);
            load_if(a, "panOffset", audio.panOffset);
            load_if(a, "simdEnabled", audio.simdEnabled);
        }

        if (j.contains("beat") && j["beat"].is_object()) {
            const auto& b = j["beat"];
            load_if(b, "algorithm", beat.algorithm);
            load_if(b, "profile", beat.profile);
            load_if(b, "falloffDefault", beat.falloffDefault);
            load_if(b, "timeScale", beat.timeScale);
            load_if(b, "timeInitial", beat.timeInitial);
            load_if(b, "timeMin", beat.timeMin);
            load_if(b, "timeDivisor", beat.timeDivisor);
            load_if(b, "spectralFluxThreshold", beat.spectralFluxThreshold);
            load_if(b, "spectralFluxDecayMultiplier", beat.spectralFluxDecayMultiplier);
            load_if(b, "tempoChangeThreshold", beat.tempoChangeThreshold);
            load_if(b, "beatInductionWindow", beat.beatInductionWindow);
            load_if(b, "octaveErrorWeight", beat.octaveErrorWeight);
            load_if(b, "minFreq", beat.minFreq);
            load_if(b, "maxFreq", beat.maxFreq);
            load_if(b, "fluxLowAlpha", beat.fluxLowAlpha);
            load_if(b, "fluxLowThresholdMultiplier", beat.fluxLowThresholdMultiplier);
            load_if(b, "fluxMin", beat.fluxMin);
        }

        if (j.contains("frequency") && j["frequency"].is_object()) {
            const auto& f = j["frequency"];
            load_if(f, "logScaleEnabled", frequency.logScaleEnabled);
            load_if(f, "logStrength", frequency.logStrength);
            load_if(f, "minFreq", frequency.minFreq);
            load_if(f, "maxFreq", frequency.maxFreq);
            if (f.contains("equalizerBands") && f["equalizerBands"].is_array()) {
                const auto& arr = f["equalizerBands"];
                for (size_t i = 0; i < frequency.equalizerBands.size() && i < arr.size(); ++i) {
                    if (arr[i].is_number()) frequency.equalizerBands[i] = arr[i].get<float>();
                }
            }
            load_if(f, "equalizerWidth", frequency.equalizerWidth);
            load_if(f, "amplifier", frequency.amplifier);
            load_if(f, "amplifierVolume", frequency.amplifierVolume);
            load_if(f, "amplifierBands", frequency.amplifierBands);
            load_if(f, "amplifierDirection", frequency.amplifierDirection);
            // Back-compat: if new amplifier fields were missing, mirror legacy amplifier
            const bool hasV = f.contains("amplifierVolume");
            const bool hasB = f.contains("amplifierBands");
            const bool hasD = f.contains("amplifierDirection");
            if (!hasV) frequency.amplifierVolume = frequency.amplifier;
            if (!hasB) frequency.amplifierBands = frequency.amplifier;
            if (!hasD) frequency.amplifierDirection = frequency.amplifier;
            load_if(f, "bands", frequency.bands);
            load_if(f, "fftSize", frequency.fftSize);
            load_if(f, "bandNorm", frequency.bandNorm);
        }

        if (j.contains("debug") && j["debug"].is_object()) {
            const auto& d = j["debug"];
            load_if(d, "debugEnabled", debug.debugEnabled);
            load_if(d, "overlayEnabled", debug.overlayEnabled);
        }

        Validate();
        LOG_DEBUG("[Configuration] Loaded configuration from: " + filepath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[Configuration] Exception while loading: " + std::string(e.what()));
        return false;
    }
}

}  // namespace Listeningway
