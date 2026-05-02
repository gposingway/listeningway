// ---------------------------------------------
// WasapiLoopbackSource — system audio capture (ADR-0007 Day 2)
//
// Captures the system default render endpoint as a loopback stream and
// pushes interleaved float samples to the FrameSink. Implements the
// corrections from research-notes.md §2:
//   - polling timer (NOT event mode — broken for loopback)
//   - AUTOCONVERTPCM + SRC_DEFAULT_QUALITY pinning float stereo 48 kHz
//   - MMCSS L"Audio" (not Pro Audio)
//   - signal-only IMMNotificationClient + worker rebuild on device change
//   - request 0 buffer period; poll at half device default period
// ---------------------------------------------
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "i_audio_source.h"

// Forward declarations to keep <mmdeviceapi.h> out of the public header.
struct IMMDeviceEnumerator;
struct IMMNotificationClient;

namespace lw::source {

class WasapiLoopbackSource final : public IAudioSource {
public:
    WasapiLoopbackSource();
    ~WasapiLoopbackSource() override;

    Info info() const override;
    bool available() const override;

    std::optional<Capabilities> open() override;
    bool start(FrameSink sink) override;
    void stop() override;
    bool restart_requested() const override;

private:
    void capture_loop(FrameSink sink);

    // Notification client posts to this flag from any thread.
    std::atomic<bool> device_change_pending_ {false};

    std::atomic<bool> running_ {false};
    std::thread thread_;

    // Lifetime: created by open(), destroyed by stop()/dtor.
    IMMDeviceEnumerator*    device_enumerator_   = nullptr;
    IMMNotificationClient*  notification_client_ = nullptr;

    // Capabilities reported by open(); set during init, read after.
    Capabilities capabilities_ {};
    std::mutex   capabilities_mutex_;
};

}  // namespace lw::source
