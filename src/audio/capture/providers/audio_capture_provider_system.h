// Implementation of system-wide audio capture provider (WASAPI loopback)
// Renamed for clarity: audio_capture_provider_system.h/cpp

#pragma once
#include "audio/capture/providers/audio_capture_provider.h"
#include <mmdeviceapi.h>
#include <atomic>

// System-wide audio capture using WASAPI loopback
class AudioCaptureProviderSystem : public IAudioCaptureProvider {
private:
    class DeviceNotificationClient;

    static std::atomic_bool device_change_pending_;
    static IMMDeviceEnumerator* device_enumerator_;
    static DeviceNotificationClient* notification_client_;

public:
    AudioCaptureProviderSystem() = default;
    ~AudioCaptureProviderSystem() override = default;

    AudioProviderInfo GetProviderInfo() const override;

    AudioCaptureProviderType GetProviderType() const override {
        return AudioCaptureProviderType::SYSTEM_AUDIO;
    }

    std::string GetProviderName() const override {
        return "System Audio (WASAPI Loopback)";
    }    bool IsAvailable() const override;
    bool StartCapture(const Listeningway::Configuration& config,
                     std::atomic_bool& running,
                     std::thread& thread,
                     AudioAnalysisData& data,
                     std::mutex& data_mutex) override;

    void StopCapture(std::atomic_bool& running, std::thread& thread) override;

    bool ShouldRestart() override {
        return device_change_pending_.load();
    }

    void ResetRestartFlags() override {
        device_change_pending_ = false;
    }

    bool Initialize() override;
    void Uninitialize() override;

    // Static method for device change notification
    static void SetDeviceChangePending() {
        device_change_pending_ = true;
    }
};
