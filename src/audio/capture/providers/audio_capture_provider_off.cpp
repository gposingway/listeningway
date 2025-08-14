// Implementation of dummy provider for 'None (Audio Analysis Off)'
// Renamed for clarity: audio_capture_provider_off.cpp
// ---------------------------------------------
// Dummy (Off) Audio Provider Implementation
// Represents the 'None' or 'Off' selection in the provider dropdown
// ---------------------------------------------
#include "audio/capture/providers/audio_capture_provider_off.h"
#include "audio/capture/providers/audio_capture_provider.h"
#include "../../core/thread_safety_manager.h"
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>

bool AudioCaptureProviderOff::IsAvailable() const { return true; }
bool AudioCaptureProviderOff::Initialize() { return true; }
void AudioCaptureProviderOff::Uninitialize() {}
bool AudioCaptureProviderOff::StartCapture(const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data) {
    // Keep the thread running but provide dummy/zero data
    running = true;
    thread = std::thread([&, config]() {
        while (running.load()) {
            // Provide zero/dummy audio data
            {
                LOCK_AUDIO_DATA();
                data.volume = 0.0f;
                std::fill(data.freq_bands.begin(), data.freq_bands.end(), 0.0f);
                data.beat = 0.0f;
                data.tempo_bpm = 0.0f;
                data.tempo_confidence = 0.0f;
                data.beat_phase = 0.0f;
                data.tempo_detected = false;
                data.volume_left = 0.0f;
                data.volume_right = 0.0f;
                data.audio_pan = 0.0f;
                data.audio_format = 0.0f;
            }
            // Sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    return true;
}
void AudioCaptureProviderOff::StopCapture(std::atomic_bool& running, std::thread& thread) {
    running = false;
    if (thread.joinable()) thread.join();
}
AudioProviderInfo AudioCaptureProviderOff::GetProviderInfo() const {
    return AudioProviderInfo{
        "off", // code as string
        "None (Audio Analysis Off)", // name
        false, // is_default
        0, // order
        false // activates_capture
    };
}
AudioCaptureProviderType AudioCaptureProviderOff::GetProviderType() const { return AudioCaptureProviderType::SYSTEM_AUDIO; } // or a new OFF type
std::string AudioCaptureProviderOff::GetProviderName() const { return "None (Audio Analysis Off)"; }
bool AudioCaptureProviderOff::ShouldRestart() { return false; }
void AudioCaptureProviderOff::ResetRestartFlags() {}

// Factory function for registration
extern "C" IAudioCaptureProvider* CreateAudioCaptureProviderOff() {
    return new AudioCaptureProviderOff();
}
