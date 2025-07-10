#include "Configuration.h"
#include "logging.h"
#include "settings.h"
#include <fstream>
#include <sstream>
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Listeningway {

bool Configuration::Save() const {
    return SaveToJson(GetDefaultConfigPath());
}

bool Configuration::Load() {
    return LoadFromJson(GetDefaultConfigPath());
}

void Configuration::ResetToDefaults() {
    // Reset to default values by reconstructing the object
    *this = Configuration{};
    LOG_DEBUG("[Configuration] Reset all settings to defaults");
}

bool Configuration::Validate() {
    bool isValid = true;
    
    // Validate audio settings
    // audio.captureProvider = std::max(-1, audio.captureProvider); // (legacy, remove after migration)
    audio.panSmoothing = std::clamp(audio.panSmoothing, 0.0f, 1.0f);
    audio.panOffset = std::clamp(audio.panOffset, -1.0f, 1.0f);
    
    // Validate beat detection settings
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
    
    // Validate frequency settings
    frequency.logStrength = std::clamp(frequency.logStrength, 0.2f, 3.0f);
    frequency.minFreq = std::clamp(frequency.minFreq, 10.0f, 500.0f);
    frequency.maxFreq = std::clamp(frequency.maxFreq, 2000.0f, 22050.0f);
    for (auto& band : frequency.equalizerBands) {
        band = std::clamp(band, 0.0f, 4.0f);
    }
    frequency.equalizerWidth = std::clamp(frequency.equalizerWidth, 0.05f, 0.5f);
    frequency.amplifier = std::clamp(frequency.amplifier, 1.0f, 11.0f);
    frequency.band_amplifier = std::clamp(frequency.band_amplifier, 0.1f, 10.0f);
    frequency.volume_amplifier = std::clamp(frequency.volume_amplifier, 0.1f, 10.0f);
    
    // Ensure min < max for frequency ranges
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
    // Use the same directory as the INI/log file
    std::string ini = GetSettingsPath();
    size_t pos = ini.find_last_of("\\/");
    std::string dir = (pos != std::string::npos) ? ini.substr(0, pos + 1) : "";
    if (!dir.empty()) {
        std::filesystem::path dirPath(dir);
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }
    }
    return dir + "Listeningway.json";
}

bool Configuration::SaveToJson(const std::string& filepath) const {
    try {
        LOG_DEBUG("[Configuration] Attempting to save config to: " + filepath);
        std::ofstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR("[Configuration] Failed to open file for writing: " + filepath);
            return false;
        }
        
        // Simple JSON serialization (no external dependencies)
        file << "{\n";
        
        // Audio settings
        file << "  \"audio\": {\n";
        file << "    \"analysisEnabled\": " << (audio.analysisEnabled ? "true" : "false") << ",\n";
        file << "    \"captureProviderCode\": \"" << audio.captureProviderCode << "\",\n";
        file << "    \"panSmoothing\": " << audio.panSmoothing << ",\n";
        file << "    \"panOffset\": " << audio.panOffset << "\n";
        file << "  },\n";
        
        // Beat detection settings
        file << "  \"beat\": {\n";
        file << "    \"algorithm\": " << beat.algorithm << ",\n";
        file << "    \"falloffDefault\": " << beat.falloffDefault << ",\n";
        file << "    \"timeScale\": " << beat.timeScale << ",\n";
        file << "    \"timeInitial\": " << beat.timeInitial << ",\n";
        file << "    \"timeMin\": " << beat.timeMin << ",\n";
        file << "    \"timeDivisor\": " << beat.timeDivisor << ",\n";
        file << "    \"spectralFluxThreshold\": " << beat.spectralFluxThreshold << ",\n";
        file << "    \"spectralFluxDecayMultiplier\": " << beat.spectralFluxDecayMultiplier << ",\n";
        file << "    \"tempoChangeThreshold\": " << beat.tempoChangeThreshold << ",\n";
        file << "    \"beatInductionWindow\": " << beat.beatInductionWindow << ",\n";
        file << "    \"octaveErrorWeight\": " << beat.octaveErrorWeight << ",\n";
        file << "    \"minFreq\": " << beat.minFreq << ",\n";
        file << "    \"maxFreq\": " << beat.maxFreq << ",\n";
        file << "    \"fluxLowAlpha\": " << beat.fluxLowAlpha << ",\n";
        file << "    \"fluxLowThresholdMultiplier\": " << beat.fluxLowThresholdMultiplier << "\n";
        file << "  },\n";
        
        // Frequency settings
        file << "  \"frequency\": {\n";
        file << "    \"logScaleEnabled\": " << (frequency.logScaleEnabled ? "true" : "false") << ",\n";
        file << "    \"logStrength\": " << frequency.logStrength << ",\n";
        file << "    \"minFreq\": " << frequency.minFreq << ",\n";
        file << "    \"maxFreq\": " << frequency.maxFreq << ",\n";
        file << "    \"equalizerBands\": [";
        for (size_t i = 0; i < frequency.equalizerBands.size(); ++i) {
            file << frequency.equalizerBands[i];
            if (i < frequency.equalizerBands.size() - 1) file << ", ";
        }
        file << "],\n";
        file << "    \"equalizerWidth\": " << frequency.equalizerWidth << ",\n";
        file << "    \"amplifier\": " << frequency.amplifier << ",\n";
        file << "    \"bandAmplifier\": " << frequency.band_amplifier << ",\n";
        file << "    \"volumeAmplifier\": " << frequency.volume_amplifier << ",\n";
        // Serialize new members
        file << "    \"bands\": " << frequency.bands << ",\n";
        file << "    \"fftSize\": " << frequency.fftSize << ",\n";
        file << "    \"bandNorm\": " << frequency.bandNorm << "\n";
        file << "  },\n";
        
        // Debug settings
        file << "  \"debug\": {\n";
        file << "    \"debugEnabled\": " << (debug.debugEnabled ? "true" : "false") << ",\n";
        file << "    \"overlayEnabled\": " << (debug.overlayEnabled ? "true" : "false") << "\n";
        file << "  }\n";
        
        file << "}\n";
        
        file.close();
        LOG_DEBUG("[Configuration] Saved configuration to: " + filepath);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("[Configuration] Exception while saving: " + std::string(e.what()));
        return false;
    }
}

bool Configuration::LoadFromJson(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_WARNING("[Configuration] Config file not found, using defaults: " + filepath);
            return false;  // Not an error, will use defaults
        }
        
        // Simple JSON parsing (basic implementation)
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        // Parse key-value pairs (very basic JSON parser)
        auto getValue = [&content](const std::string& key) -> std::string {
            std::string searchKey = "\"" + key + "\":";
            size_t pos = content.find(searchKey);
            if (pos == std::string::npos) return "";
            
            pos += searchKey.length();
            while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
            
            size_t start = pos;
            size_t end = pos;
            
            if (content[pos] == '"') {
                // String value
                start = pos + 1;
                end = content.find('"', start);
            } else if (content[pos] == '[') {
                // Array value
                end = content.find(']', pos) + 1;
            } else {
                // Number or boolean
                while (end < content.length() && content[end] != ',' && content[end] != '\n' && content[end] != '}') {
                    end++;
                }
            }
            
            return content.substr(start, end - start);
        };
        
        // Parse audio settings
        std::string value = getValue("analysisEnabled");
        if (!value.empty()) audio.analysisEnabled = (value == "true");
        
        value = getValue("captureProviderCode");
        if (!value.empty()) audio.captureProviderCode = value.substr(1, value.length() - 2); // remove quotes
        
        value = getValue("panSmoothing");
        if (!value.empty()) audio.panSmoothing = std::stof(value);
        
        value = getValue("panOffset");
        if (!value.empty()) audio.panOffset = std::stof(value);
        
        // Parse beat detection settings
        value = getValue("algorithm");
        if (!value.empty()) beat.algorithm = std::stoi(value);
        
        value = getValue("falloffDefault");
        if (!value.empty()) beat.falloffDefault = std::stof(value);
        
        value = getValue("timeScale");
        if (!value.empty()) beat.timeScale = std::stof(value);
        
        value = getValue("timeInitial");
        if (!value.empty()) beat.timeInitial = std::stof(value);
        
        value = getValue("timeMin");
        if (!value.empty()) beat.timeMin = std::stof(value);
        
        value = getValue("timeDivisor");
        if (!value.empty()) beat.timeDivisor = std::stof(value);
        
        value = getValue("spectralFluxThreshold");
        if (!value.empty()) beat.spectralFluxThreshold = std::stof(value);
        
        value = getValue("spectralFluxDecayMultiplier");
        if (!value.empty()) beat.spectralFluxDecayMultiplier = std::stof(value);
        
        value = getValue("tempoChangeThreshold");
        if (!value.empty()) beat.tempoChangeThreshold = std::stof(value);
        
        value = getValue("beatInductionWindow");
        if (!value.empty()) beat.beatInductionWindow = std::stof(value);
        
        value = getValue("octaveErrorWeight");
        if (!value.empty()) beat.octaveErrorWeight = std::stof(value);
        
        value = getValue("minFreq");
        if (!value.empty()) beat.minFreq = std::stof(value);
        
        value = getValue("maxFreq");
        if (!value.empty()) beat.maxFreq = std::stof(value);
        
        value = getValue("fluxLowAlpha");
        if (!value.empty()) beat.fluxLowAlpha = std::stof(value);
        
        value = getValue("fluxLowThresholdMultiplier");
        if (!value.empty()) beat.fluxLowThresholdMultiplier = std::stof(value);
        
        // Parse frequency settings
        value = getValue("logScaleEnabled");
        if (!value.empty()) frequency.logScaleEnabled = (value == "true");
        
        value = getValue("logStrength");
        if (!value.empty()) frequency.logStrength = std::stof(value);
        
        // Parse equalizer bands array
        value = getValue("equalizerBands");
        if (!value.empty() && value.front() == '[' && value.back() == ']') {
            std::string arrayContent = value.substr(1, value.length() - 2);
            std::istringstream iss(arrayContent);
            std::string bandValue;
            size_t bandIndex = 0;
            while (std::getline(iss, bandValue, ',') && bandIndex < frequency.equalizerBands.size()) {
                // Trim whitespace
                bandValue.erase(0, bandValue.find_first_not_of(" \t"));
                bandValue.erase(bandValue.find_last_not_of(" \t") + 1);
                frequency.equalizerBands[bandIndex] = std::stof(bandValue);
                bandIndex++;
            }
        }
        
        value = getValue("equalizerWidth");
        if (!value.empty()) frequency.equalizerWidth = std::stof(value);
        
        value = getValue("amplifier");
        if (!value.empty()) frequency.amplifier = std::stof(value);
        
        value = getValue("bandAmplifier");
        if (!value.empty()) {
            frequency.band_amplifier = std::stof(value);
        } else {
            frequency.band_amplifier = DEFAULT_AMPLIFIER; // Ensure default if missing
        }
        
        value = getValue("volumeAmplifier");
        if (!value.empty()) {
            frequency.volume_amplifier = std::stof(value);
        } else {
            frequency.volume_amplifier = DEFAULT_AMPLIFIER; // Ensure default if missing
        }
        
        // Parse new members
        value = getValue("bands");
        if (!value.empty()) frequency.bands = static_cast<size_t>(std::stoul(value));
        value = getValue("fftSize");
        if (!value.empty()) frequency.fftSize = static_cast<size_t>(std::stoul(value));
        value = getValue("bandNorm");
        if (!value.empty()) frequency.bandNorm = std::stof(value);
        
        // Parse debug settings
        value = getValue("debugEnabled");
        if (!value.empty()) debug.debugEnabled = (value == "true");
        
        value = getValue("overlayEnabled");
        if (!value.empty()) debug.overlayEnabled = (value == "true");
        
        // Validate loaded values
        Validate();
        
        LOG_DEBUG("[Configuration] Loaded configuration from: " + filepath);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("[Configuration] Exception while loading: " + std::string(e.what()));
        return false;
    }
}

}  // namespace Listeningway
