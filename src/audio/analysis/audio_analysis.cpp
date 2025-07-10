#include "audio_analysis.h"
#include "../../utils/logging.h"
#include "../../configuration/configuration_manager.h"
#include "../../core/audio_format_utils.h"
#include "../../core/constants.h"
using Listeningway::ConfigurationManager;
#include <algorithm>
#include <numeric>
#include <cmath>
#include <mutex>
#include <kiss_fft.h>
#include <memory>

// Global instance of AudioAnalyzer
AudioAnalyzer g_audio_analyzer;

AudioAnalyzer::AudioAnalyzer() = default;
AudioAnalyzer::~AudioAnalyzer() = default;

void AudioAnalyzer::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_running_ = true;
    total_time_ = 0.0f;
    beat_intensity_ = 0.0f;
    bass_threshold_ = 0.0f;
    mid_threshold_ = 0.0f;
}

void AudioAnalyzer::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_running_ = false;
}

void AudioAnalyzer::InitializeFrequencyMapping(size_t channel_count, size_t fft_size, float sample_rate) {
    channel_map_.clear();
    channel_map_.resize(channel_count);

    const float nyquist = sample_rate * 0.5f;
    const size_t half_fft_size = fft_size / 2;

    // Get current configuration
    const auto config = Listeningway::ConfigurationManager::Snapshot();
    
    // Use configured frequency range for musical visualization
    const float min_freq = config.frequency.minFreq;
    const float max_freq = config.frequency.maxFreq;
    
    // Calculate FFT bins corresponding to our frequency range
    const size_t min_bin = static_cast<size_t>(min_freq * half_fft_size / nyquist);
    const size_t max_bin = static_cast<size_t>(max_freq * half_fft_size / nyquist);
    const size_t usable_bins = max_bin - min_bin;

    // Clamp channel count if needed
    if (channel_count > usable_bins) {
        channel_count = usable_bins;
        channel_map_.resize(channel_count);
    }

    // Distribute the frequency range across all bands
    size_t bins_per_channel = usable_bins / channel_count;
    size_t remainder_bins = usable_bins % channel_count;
    size_t current_bin = min_bin;

    for (size_t i = 0; i < channel_count; ++i) {
        size_t bins_for_this_channel = bins_per_channel;
        if (i < remainder_bins) {
            bins_for_this_channel += 1;
        }
        
        channel_map_[i].fft_bins.clear();
        for (size_t j = 0; j < bins_for_this_channel && current_bin <= max_bin; ++j) {
            channel_map_[i].fft_bins.push_back(current_bin);
            current_bin++;
        }
        
        if (!channel_map_[i].fft_bins.empty()) {
            size_t first_bin = channel_map_[i].fft_bins.front();
            size_t last_bin = channel_map_[i].fft_bins.back();
            channel_map_[i].min_freq = static_cast<float>(first_bin) * nyquist / half_fft_size;
            channel_map_[i].max_freq = static_cast<float>(last_bin + 1) * nyquist / half_fft_size;
        } else {
            channel_map_[i].min_freq = 0.0f;
            channel_map_[i].max_freq = 0.0f;
        }
    }

    mapping_initialized_ = true;
}

float AudioAnalyzer::ProcessBeatDetection(const std::vector<float>& freq_bands, float dt, const Listeningway::Configuration& config) {
    total_time_ += dt;
    
    if (freq_bands.size() < 8) return 0.0f;
    
    // Calculate different frequency range energies for artistic beat detection
    size_t bands_per_group = freq_bands.size() / 8;
    float sub_bass = 0.0f, bass = 0.0f, low_mid = 0.0f, mid = 0.0f;
    
    // Sub-bass and bass (channels 0-1/4)
    for (size_t i = 0; i < bands_per_group && i < freq_bands.size(); ++i) {
        sub_bass += freq_bands[i];
    }
    sub_bass /= bands_per_group;
    
    for (size_t i = bands_per_group; i < 2 * bands_per_group && i < freq_bands.size(); ++i) {
        bass += freq_bands[i];
    }
    bass /= bands_per_group;
    
    // Low-mid and mid (channels 1/4-1/2)
    for (size_t i = 2 * bands_per_group; i < 3 * bands_per_group && i < freq_bands.size(); ++i) {
        low_mid += freq_bands[i];
    }
    low_mid /= bands_per_group;
    
    for (size_t i = 3 * bands_per_group; i < 4 * bands_per_group && i < freq_bands.size(); ++i) {
        mid += freq_bands[i];
    }
    mid /= bands_per_group;
    
    // Adaptive thresholds with artistic emphasis
    bass_threshold_ = bass_threshold_ * 0.98f + (sub_bass + bass) * 0.02f;
    mid_threshold_ = mid_threshold_ * 0.98f + (low_mid + mid) * 0.02f;
    
    // Beat detection based on user's style preference
    bool is_beat = false;
    float beat_strength = 0.0f;
    
    if (config.beat.beat_style == "electronic") {
        // Emphasize sharp kick drums and electronic elements
        float kick_energy = (sub_bass + bass) * config.beat.kick_punch;
        if (kick_energy > bass_threshold_ * (1.8f) && total_time_ - last_beat_time_ > 0.15f) {
            beat_strength = kick_energy * config.beat.beat_boost;
            is_beat = true;
        }
    } else if (config.beat.beat_style == "acoustic") {
        // Broader response for natural instruments
        float combined_energy = (sub_bass + bass + low_mid * 0.5f);
        if (combined_energy > (bass_threshold_ + mid_threshold_) * 0.7f && total_time_ - last_beat_time_ > 0.2f) {
            beat_strength = combined_energy * config.beat.beat_boost * 0.8f;
            is_beat = true;
        }
    } else if (config.beat.beat_style == "vocal") {
        // Emphasize vocal dynamics and breathing patterns
        float vocal_energy = mid * config.beat.snare_snap;
        if (vocal_energy > mid_threshold_ * 1.3f || 
            (bass > bass_threshold_ * 1.5f && total_time_ - last_beat_time_ > 0.25f)) {
            beat_strength = std::max(vocal_energy, bass) * config.beat.beat_boost;
            is_beat = true;
        }
    } else { // "mixed" mode
        // Balanced detection for varied content
        float bass_beat = (sub_bass + bass) * config.beat.kick_punch;
        float mid_beat = (low_mid + mid) * config.beat.snare_snap;
        
        if ((bass_beat > bass_threshold_ * 1.6f || mid_beat > mid_threshold_ * 1.4f) &&
            total_time_ - last_beat_time_ > 0.18f) {
            beat_strength = std::max(bass_beat, mid_beat) * config.beat.beat_boost;
            is_beat = true;
        }
    }
    
    if (is_beat) {
        last_beat_time_ = total_time_;
        beat_intensity_ = std::min(1.0f, beat_strength);
    }
    
    // Artistic decay curve for visual appeal
    float decay_rate = 1.0f / config.beat.beat_hold;
    beat_intensity_ = std::max(0.0f, beat_intensity_ - decay_rate * dt);
    
    return beat_intensity_;
}

void AudioAnalyzer::AnalyzeAudioBuffer(const float* data, size_t numFrames, size_t numChannels, AudioAnalysisData& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    const auto config = Listeningway::ConfigurationManager::Snapshot();
    
    // Early exit if analysis is disabled
    if (!config.audio.analysisEnabled) {
        out.volume = 0.0f;
        std::fill(out.freq_bands.begin(), out.freq_bands.end(), 0.0f);
        std::fill(out.raw_freq_bands.begin(), out.raw_freq_bands.end(), 0.0f);
        out.beat = 0.0f;
        return;
    }
    
    // Resize frequency bands if configuration changed
    if (out.freq_bands.size() != config.frequency.bands) {
        out.ResizeBands(config.frequency.bands);
        mapping_initialized_ = false; // Force remapping
    }
    
    // Check if frequency range has changed
    if (config.frequency.minFreq != last_min_freq_ || config.frequency.maxFreq != last_max_freq_) {
        last_min_freq_ = config.frequency.minFreq;
        last_max_freq_ = config.frequency.maxFreq;
        mapping_initialized_ = false; // Force remapping with new frequency range
    }
    
    // Initialize frequency mapping if needed
    if (!mapping_initialized_) {
        InitializeFrequencyMapping(config.frequency.bands, config.frequency.fftSize, config.sample_rate);
    }
    
    // Store previous frame for smooth transitions
    out._prev_freq_bands = out.freq_bands;
    
    // 1. Artistic volume calculation with enhanced dynamics
    float sum_squares = 0.0f;
    for (size_t i = 0; i < numFrames * numChannels; i++) {
        sum_squares += data[i] * data[i];
    }
    float raw_volume = std::sqrt(sum_squares / (numFrames * numChannels));
    
    // Apply artistic curve for better visual dynamics (slight compression + boost)
    out.volume = std::pow(raw_volume, 0.7f) * config.frequency.volume_amplifier;
    out.volume = std::min(1.0f, out.volume);
    
    // 2. Enhanced stereo analysis for creative effects
    if (numChannels >= 2) {
        float left_energy = 0.0f, right_energy = 0.0f;
        float correlation = 0.0f;
        
        for (size_t i = 0; i < numFrames; i++) {
            float l = data[i * numChannels];
            float r = data[i * numChannels + 1];
            
            left_energy += l * l;
            right_energy += r * r;
            correlation += l * r;
        }
        
        out.volume_left = std::sqrt(left_energy / numFrames);
        out.volume_right = std::sqrt(right_energy / numFrames);
        
        // Artistic pan with enhanced sensitivity
        float total_energy = out.volume_left + out.volume_right;
        out.audio_pan = (total_energy > 0.001f) ? 
                       (out.volume_right - out.volume_left) / total_energy * 1.5f : 0.0f;
        out.audio_pan = std::clamp(out.audio_pan, -1.0f, 1.0f);
    } else {
        out.volume_left = out.volume_right = out.volume;
        out.audio_pan = 0.0f;
    }
    
    // 3. High-resolution frequency analysis for perfect coverage
    const size_t fft_size = std::max(static_cast<size_t>(4096), config.frequency.fftSize); // High resolution for gap-free analysis
    const size_t half_fft_size = fft_size / 2;
    
    // Setup FFT
    kiss_fft_cfg fft_cfg = kiss_fft_alloc(fft_size, 0, nullptr, nullptr);
    if (!fft_cfg) {
        LOG_ERROR("Failed to allocate FFT configuration");
        return;
    }
    
    std::vector<kiss_fft_cpx> fft_in(fft_size);
    std::vector<kiss_fft_cpx> fft_out(fft_size);
    
    // High-resolution audio buffer accumulation
    // Initialize accumulation buffer if needed
    if (accumulated_audio_.size() != fft_size) {
        accumulated_audio_.resize(fft_size, 0.0f);
        accumulation_pos_ = 0;
    }
    
    // Convert current frame to mono and add to accumulation buffer
    size_t samples_to_copy = std::min(numFrames, fft_size);
    for (size_t i = 0; i < samples_to_copy; ++i) {
        float sample = 0.0f;
        for (size_t ch = 0; ch < numChannels; ++ch) {
            sample += data[i * numChannels + ch];
        }
        accumulated_audio_[accumulation_pos_] = sample / numChannels;
        accumulation_pos_ = (accumulation_pos_ + 1) % fft_size; // Circular buffer
    }
    
    // High-resolution FFT with accumulated data for maximum frequency detail
    for (size_t i = 0; i < fft_size; ++i) {
        // Read from circular buffer starting from current position
        size_t read_pos = (accumulation_pos_ + i) % fft_size;
        fft_in[i].r = accumulated_audio_[read_pos];
        fft_in[i].i = 0.0f;
    }
    
    kiss_fft(fft_cfg, fft_in.data(), fft_out.data());
    
    // Calculate magnitude with artistic enhancement
    std::vector<float> magnitudes(half_fft_size);
    for (size_t i = 0; i < half_fft_size; ++i) {
        float magnitude = std::sqrt(fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i);
        magnitudes[i] = std::pow(magnitude, 0.8f);  // Slight compression for visual appeal
    }
    
    // Direct 1:1 FFT bin-to-band mapping - RAW CAPTURE FIRST
    for (size_t ch = 0; ch < config.frequency.bands; ++ch) {
        if (ch >= channel_map_.size() || channel_map_[ch].fft_bins.empty()) {
            out.raw_freq_bands[ch] = 0.0f;
            continue;
        }
        float raw_energy = 0.0f;
        for (size_t bin : channel_map_[ch].fft_bins) {
            if (bin < magnitudes.size()) {
                raw_energy += magnitudes[bin];
            }
        }
        // Avoid division by zero
        out.raw_freq_bands[ch] = (channel_map_[ch].fft_bins.size() > 0) ?
                                 raw_energy / channel_map_[ch].fft_bins.size() * 0.01f : 0.0f;
        out.raw_freq_bands[ch] = std::clamp(out.raw_freq_bands[ch], 0.0f, 1.0f);
    }
    
    // THEN apply artistic processing to create processed bands
    for (size_t ch = 0; ch < config.frequency.bands; ++ch) {
        // Start with raw data
        float raw_level = out.raw_freq_bands[ch];
        
        // Calculate bell curve modifiers for linear frequency distribution
        float total_bands = static_cast<float>(config.frequency.bands);
        float ch_ratio = static_cast<float>(ch) / total_bands;
        
        // Bass punch: bell curve centered at first 15% of bands (low frequencies)
        float bass_center = 0.075f; // Center early in the spectrum
        float bass_width = 0.15f;   // Narrow width for bass focus
        float bass_distance = std::abs(ch_ratio - bass_center) / bass_width;
        float bass_modifier = std::exp(-bass_distance * bass_distance) * (config.frequency.bass_punch - 1.0f) + 1.0f;
        
        // Vocal focus: bell curve centered at 20-60% of bands (mid frequencies)  
        float vocal_center = 0.4f;  // Center in mid-frequency range
        float vocal_width = 0.25f;  // Wider for vocal range
        float vocal_distance = std::abs(ch_ratio - vocal_center) / vocal_width;
        float vocal_modifier = std::exp(-vocal_distance * vocal_distance) * (config.frequency.vocal_focus - 1.0f) + 1.0f;
        
        // High detail: bell curve centered at last 25% of bands (high frequencies)
        float high_center = 0.85f;  // Center in high-frequency range
        float high_width = 0.2f;    // Moderate width for highs
        float high_distance = std::abs(ch_ratio - high_center) / high_width;
        float high_modifier = std::exp(-high_distance * high_distance) * (config.frequency.high_detail - 1.0f) + 1.0f;
        
        // Combine all modifiers (when all are 1.0, result is raw_level * 1.0 * 1.0 * 1.0 = raw_level)
        float enhanced = raw_level * bass_modifier * vocal_modifier * high_modifier;
        float target_level = std::clamp(enhanced, 0.0f, 1.0f);
        
        // Artistic attack/decay for visual appeal
        if (target_level > out._prev_freq_bands[ch]) {
            // Fast attack for dramatic impact
            out.freq_bands[ch] = out._prev_freq_bands[ch] + 
                               (target_level - out._prev_freq_bands[ch]) * config.frequency.quick_response * 0.5f;
        } else {
            // Smooth decay for visual flow
            out.freq_bands[ch] = out._prev_freq_bands[ch] * config.frequency.smooth_fade;
        }
        
        // Apply band amplifier for final boost control
        out.freq_bands[ch] *= config.frequency.band_amplifier;
        out.freq_bands[ch] = std::clamp(out.freq_bands[ch], 0.0f, 1.0f);
    }
    
    kiss_fft_free(fft_cfg);
    
    // 4. Creative beat detection
    float dt = static_cast<float>(numFrames) / config.sample_rate;
    out.beat = ProcessBeatDetection(out.freq_bands, dt, config);
    
    // Simple tempo estimation based on beat intervals
    if (out.beat > 0.5f && total_time_ - last_beat_time_ < 2.0f && total_time_ - last_beat_time_ > 0.1f) {
        out.tempo_bpm = 60.0f / (total_time_ - last_beat_time_);
        out.tempo_confidence = std::min(1.0f, out.beat);
        out.tempo_detected = true;
    } else {
        out.tempo_bpm = 0.0f;
        out.tempo_confidence = 0.0f;
        out.tempo_detected = false;
    }
    
    out.beat_phase = std::fmod(total_time_ * (out.tempo_bpm / 60.0f), 1.0f);
    
    // Set audio format info
    out.audio_format = static_cast<float>(numChannels);
}

// Standalone function using the global analyzer
void AnalyzeAudioBuffer(const float* data, size_t numFrames, size_t numChannels, AudioAnalysisData& out) {
    g_audio_analyzer.AnalyzeAudioBuffer(data, numFrames, numChannels, out);
}
