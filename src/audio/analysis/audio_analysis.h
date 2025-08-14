#pragma once
#include <vector>
#include <cstddef>
#include <memory>
#include <mutex>
#include <kiss_fft.h>
#include "tempo_worker.h"
#include "constants.h"
#include "beat_detector.h"
#include "../../configuration/configuration_manager.h"

// Audio analysis results for one frame
struct AudioAnalysisData {
    float volume = 0.0f;                // Normalized RMS volume [0,1]
    std::vector<float> freq_bands;      // Normalized frequency bands (with equalizer)
    std::vector<float> raw_freq_bands;  // Raw frequency bands (without equalizer)
    float beat = 0.0f;                 // Beat detection value [0,1]
    
    // Beat detection info
    float tempo_bpm = 0.0f;            // Detected tempo in BPM
    float tempo_confidence = 0.0f;     // Confidence in tempo estimate [0,1]
    float beat_phase = 0.0f;           // Current phase in beat cycle [0,1]
    bool tempo_detected = false;       // Whether tempo has been detected

    // Internal analysis state (not for API consumers)
    std::vector<float> _prev_magnitudes; // Previous FFT magnitudes (for spectral flux)
    float _flux_avg = 0.0f;             // Moving average of spectral flux
    float _flux_low_avg = 0.0f;         // Moving average of low-frequency spectral flux    
    
    // Stereo analysis
    float volume_left = 0.0f;         // Left channel volume
    float volume_right = 0.0f;        // Right channel volume
    float audio_pan = 0.0f;           // Pan value [-1, +1]
    float audio_format = 0.0f;        // Audio format (0=none, 1=mono, 2=stereo, 6=5.1, 8=7.1)

    AudioAnalysisData(size_t bands = 8) : freq_bands(bands, 0.0f), raw_freq_bands(bands, 0.0f) {}
};

/**
 * @brief Analyzer for audio data, using beat detection algorithms
 */
class AudioAnalyzer {
public:
    /**
     * @brief Constructor
     */
    AudioAnalyzer();
    
    /**
     * @brief Destructor
     */
    ~AudioAnalyzer();
    
    /**
     * @brief Set the beat detection algorithm to use
     * @param algorithm Algorithm index (0=SimpleEnergy, 1=SpectralFluxAuto)
     */
    void SetBeatDetectionAlgorithm(int algorithm);
    
    /**
     * @brief Get the current beat detection algorithm
     * @return Current algorithm index
     */
    int GetBeatDetectionAlgorithm() const;
    
    /**
     * @brief Start audio analysis
     */
    void Start();
    
    /**
     * @brief Stop audio analysis
     */
    void Stop();
    
    /**
     * @brief Analyze a buffer of audio samples and update analysis data.
     * @param data Pointer to interleaved float samples.
     * @param numFrames Number of frames (samples per channel).
     * @param numChannels Number of channels (e.g. 2 for stereo).
     * @param out Analysis results (updated in-place).
     */
    void AnalyzeAudioBuffer(const float* data, size_t numFrames, size_t numChannels, AudioAnalysisData& out);

private:
    mutable std::mutex mutex_;
    std::unique_ptr<IBeatDetector> beat_detector_;
    int current_algorithm_ = 0;
    bool is_running_ = false;

    // Background tempo estimation worker
    lw::audio::analysis::TempoWorker tempo_worker_;

    // (Buffers are managed in thread-local storage in the analysis routine)
};

// Global instance of the audio analyzer (accessible to all modules)
extern AudioAnalyzer g_audio_analyzer;

// Standalone function to analyze audio buffers using the static config
void AnalyzeAudioBuffer(const float* data, size_t numFrames, size_t numChannels, AudioAnalysisData& out);
