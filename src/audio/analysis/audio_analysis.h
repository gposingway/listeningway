#pragma once
#include <vector>
#include <cstddef>
#include <memory>
#include <mutex>
#include "../../core/constants.h"
#include "../../configuration/configuration_manager.h"

// Audio analysis results for one frame
struct AudioAnalysisData {
    float volume = 0.0f;                // Normalized RMS volume [0,1]
    std::vector<float> freq_bands;      // Normalized frequency bands (configurable count)
    std::vector<float> raw_freq_bands;  // Raw frequency bands (without equalizer)
    float beat = 0.0f;                 // Beat detection value [0,1]
    
    // Beat detection info
    float tempo_bpm = 0.0f;            // Detected tempo in BPM
    float tempo_confidence = 0.0f;     // Confidence in tempo estimate [0,1]
    float beat_phase = 0.0f;           // Current phase in beat cycle [0,1]
    bool tempo_detected = false;       // Whether tempo has been detected

    // Stereo analysis
    float volume_left = 0.0f;         // Left channel volume
    float volume_right = 0.0f;        // Right channel volume
    float audio_pan = 0.0f;           // Pan value [-1, +1]
    float audio_format = 0.0f;        // Audio format (0=none, 1=mono, 2=stereo, 6=5.1, 8=7.1)
    
    // Internal analysis state (not for API consumers)
    std::vector<float> _prev_freq_bands;     // Previous frame for smooth transitions

    AudioAnalysisData(size_t bands = DEFAULT_NUM_BANDS) : 
        freq_bands(bands, 0.0f), 
        raw_freq_bands(bands, 0.0f),
        _prev_freq_bands(bands, 0.0f) {}
    
    // Convenience method to resize for different band counts
    void ResizeBands(size_t new_band_count) {
        freq_bands.resize(new_band_count, 0.0f);
        raw_freq_bands.resize(new_band_count, 0.0f);
        _prev_freq_bands.resize(new_band_count, 0.0f);
    }
};

/**
 * @brief Simplified analyzer for aesthetic audio visualization
 */
class AudioAnalyzer {
public:
    AudioAnalyzer();
    ~AudioAnalyzer();
    
    void Start();
    void Stop();
    
    /**
     * @brief Set the beat detection algorithm (for compatibility - now uses configuration)
     * @param algorithm Algorithm index (ignored in new system, uses config.beat.beat_style instead)
     */
    void SetBeatDetectionAlgorithm(int algorithm) { /* No-op for compatibility */ }
    
    /**
     * @brief Get the current beat detection algorithm (for compatibility)
     * @return Always returns 0 (new system uses configuration)
     */
    int GetBeatDetectionAlgorithm() const { return 0; }
    
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
    bool is_running_ = false;
    
    // Beat detection state
    float bass_threshold_ = 0.0f;
    float mid_threshold_ = 0.0f;
    float beat_intensity_ = 0.0f;
    float last_beat_time_ = 0.0f;
    float total_time_ = 0.0f;
    
    // Frequency mapping for aesthetic analysis
    struct ChannelMapping {
        float min_freq, max_freq;
        std::vector<size_t> fft_bins;
    };
    std::vector<ChannelMapping> channel_map_;
    bool mapping_initialized_ = false;
    
    // Track frequency configuration to detect changes
    float last_min_freq_ = 0.0f;
    float last_max_freq_ = 0.0f;
    
    // High-resolution FFT accumulation buffer for gap-free frequency analysis
    std::vector<float> accumulated_audio_;
    size_t accumulation_pos_ = 0;
    
    void InitializeFrequencyMapping(size_t channel_count, size_t fft_size, float sample_rate);
    float ProcessBeatDetection(const std::vector<float>& freq_bands, float dt, const Listeningway::Configuration& config);
};

// Global instance of the audio analyzer
extern AudioAnalyzer g_audio_analyzer;

// Standalone function to analyze audio buffers
void AnalyzeAudioBuffer(const float* data, size_t numFrames, size_t numChannels, AudioAnalysisData& out);
