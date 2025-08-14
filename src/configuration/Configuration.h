#pragma once

#include <array>
#include <string>
#include "core/constants.h"

namespace Listeningway {

/**
 * @brief Modern configuration structure with direct property access
 * 
 * This structure contains all application settings organized into logical groups.
 * Properties can be accessed directly without getter/setter methods.
 * Use Save()/Load() methods for persistence.
 */
struct Configuration {
    // Audio Analysis Settings
    struct Audio {
        bool analysisEnabled = false;
        std::string captureProviderCode = "off"; // provider code string, e.g. "system", "process", "off"
        // int captureProvider = -1;  // (legacy, remove after migration)
        float panSmoothing = 0.1f;
        float panOffset = 0.0f; // User panning adjustment, range [-1, +1], default 0
    bool simdEnabled = true; // Enable SIMD optimizations (SSE/AVX) when available
    } audio;

    // Beat Detection Settings
    struct BeatDetection {
        int algorithm = DEFAULT_BEAT_DETECTION_ALGORITHM; 
    // Persisted beat profile name: "general", "edm", "acoustic", "custom"
    std::string profile = "general";
        float falloffDefault = DEFAULT_BEAT_FALLOFF_DEFAULT;
        float timeScale = DEFAULT_BEAT_TIME_SCALE;
        float timeInitial = DEFAULT_BEAT_TIME_INITIAL;
        float timeMin = DEFAULT_BEAT_TIME_MIN;
        float timeDivisor = DEFAULT_BEAT_TIME_DIVISOR;
        float spectralFluxThreshold = DEFAULT_SPECTRAL_FLUX_THRESHOLD;
        float spectralFluxDecayMultiplier = DEFAULT_SPECTRAL_FLUX_DECAY_MULTIPLIER;
        float tempoChangeThreshold = DEFAULT_TEMPO_CHANGE_THRESHOLD;
        float beatInductionWindow = DEFAULT_BEAT_INDUCTION_WINDOW;
        float octaveErrorWeight = DEFAULT_OCTAVE_ERROR_WEIGHT;
        float minFreq = DEFAULT_BEAT_MIN_FREQ;
        float maxFreq = DEFAULT_BEAT_MAX_FREQ;
        float fluxLowAlpha = DEFAULT_FLUX_LOW_ALPHA;
        float fluxLowThresholdMultiplier = DEFAULT_FLUX_LOW_THRESHOLD_MULTIPLIER;
        float fluxMin = DEFAULT_BEAT_FLUX_MIN;
    } beat;

    // Frequency Band Settings
    struct FrequencyBands {
        bool logScaleEnabled = DEFAULT_BAND_LOG_SCALE;
        float logStrength = DEFAULT_BAND_LOG_STRENGTH;
        float minFreq = DEFAULT_BAND_MIN_FREQ;
        float maxFreq = DEFAULT_BAND_MAX_FREQ;
        std::array<float, 5> equalizerBands = { DEFAULT_EQUALIZER_BAND1, DEFAULT_EQUALIZER_BAND2, DEFAULT_EQUALIZER_BAND3, DEFAULT_EQUALIZER_BAND4, DEFAULT_EQUALIZER_BAND5 };
        float equalizerWidth = DEFAULT_EQUALIZER_WIDTH;
    // Legacy global amplifier (kept for backward compatibility on load/save)
    float amplifier = DEFAULT_AMPLIFIER;
    // New: separate amplifiers for UI/uniform boosting (do not affect analysis)
    float amplifierVolume = DEFAULT_AMPLIFIER;
    float amplifierBands = DEFAULT_AMPLIFIER;
    float amplifierDirection = DEFAULT_AMPLIFIER;
        size_t bands = DEFAULT_NUM_BANDS;
        size_t fftSize = DEFAULT_FFT_SIZE;
        float bandNorm = DEFAULT_BAND_NORM;
    } frequency;

    // Audio sample rate (Hz)
    float sample_rate = 48000.0f;

    // Debug and Logging Settings
    struct Debug {
        bool debugEnabled = false;
        bool overlayEnabled = false;
    } debug;

    // Methods for persistence
    bool Save() const;
    bool Load();
    void ResetToDefaults();
    bool Validate();

    // Returns the full path to the default config file (same dir as INI/log)
    static std::string GetDefaultConfigPath();

private:
    bool SaveToJson(const std::string& filepath) const;
    bool LoadFromJson(const std::string& filepath);
};

}  // namespace Listeningway
