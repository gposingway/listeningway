#pragma once
#include "audio/capture/providers/audio_capture_provider.h"
#include "audio/analysis/audio_analysis.h"
#include "configuration/configuration_manager.h"
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <mmdeviceapi.h>

/**
 * @brief Manages audio capture providers and provider selection.
 *
 * Handles initialization, provider switching, and capture thread management.
 */
class AudioCaptureManager {
private:
    std::vector<std::unique_ptr<IAudioCaptureProvider>> providers_;
    IAudioCaptureProvider* current_provider_;
    AudioCaptureProviderType preferred_provider_type_;
    bool initialized_;

public:
    AudioCaptureManager();
    ~AudioCaptureManager();

    bool Initialize();
    void Uninitialize();
    std::vector<AudioCaptureProviderType> GetAvailableProviders() const;
    std::string GetProviderName(AudioCaptureProviderType type) const;
    bool SetPreferredProvider(AudioCaptureProviderType type);
    bool SetPreferredProviderByCode(const std::string& providerCode);
    AudioCaptureProviderType GetPreferredProvider() const { return preferred_provider_type_; }
    AudioCaptureProviderType GetCurrentProvider() const;
    bool StartCapture(const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data, std::mutex& data_mutex);
    void StopCapture(std::atomic_bool& running, std::thread& thread);
    void CheckAndRestartCapture(const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data, std::mutex& data_mutex);
    bool SwitchProviderAndRestart(AudioCaptureProviderType type, const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data, std::mutex& data_mutex);
    bool SwitchProviderByCodeAndRestart(const std::string& providerCode, const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data, std::mutex& data_mutex);
    std::vector<AudioProviderInfo> GetAvailableProviderInfos() const;
    bool ApplyConfiguration(const Listeningway::Configuration& config);

private:
    void RegisterProviders();
    IAudioCaptureProvider* FindProvider(AudioCaptureProviderType type) const;
    IAudioCaptureProvider* SelectBestProvider();
};
