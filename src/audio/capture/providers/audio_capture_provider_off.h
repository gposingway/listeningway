// Implementation of dummy provider for 'None (Audio Analysis Off)'
// Renamed for clarity: audio_capture_provider_off.h/cpp

#pragma once
#include "audio/capture/providers/audio_capture_provider.h"
#include <string>

// Dummy provider for 'None (Audio Analysis Off)'
class AudioCaptureProviderOff : public IAudioCaptureProvider {
public:
    bool IsAvailable() const override;
    bool Initialize() override;
    void Uninitialize() override;
    bool StartCapture(const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData&, std::mutex&) override;
    void StopCapture(std::atomic_bool& running, std::thread& thread) override;
    AudioProviderInfo GetProviderInfo() const override;
    AudioCaptureProviderType GetProviderType() const override;
    std::string GetProviderName() const override;
    bool ShouldRestart() override;
    void ResetRestartFlags() override;
};

extern "C" IAudioCaptureProvider* CreateAudioCaptureProviderOff();
