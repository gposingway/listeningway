// ---------------------------------------------
// ProcessAudioSource — per-process loopback (ADR-0009)
//
// Captures *only* the host process's audio output (and its child processes)
// using ActivateAudioInterfaceAsync + AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS.
// Available on Windows 10 build 20348 (22H2) and Windows 11.
//
// The killer simplification: as a ReShade addon we run inside the game,
// so TargetProcessId = GetCurrentProcessId(). No PID resolution, no
// launcher reparenting, no anti-cheat probing.
//
// Event-driven (LOOPBACK | EVENTCALLBACK works for process loopback,
// unlike system loopback). Hardcoded format (engine resamples to fit) —
// GetMixFormat returns E_NOTIMPL on the virtual device.
// ---------------------------------------------
#pragma once

#include <atomic>
#include <thread>

#include "i_audio_source.h"

namespace lw::source {

class ProcessAudioSource final : public IAudioSource {
public:
    ProcessAudioSource();
    ~ProcessAudioSource() override;

    Info info() const override;
    bool available() const override;

    std::optional<Capabilities> open() override;
    bool start(FrameSink sink) override;
    void stop() override;
    bool restart_requested() const override;

private:
    void capture_loop(FrameSink sink);

    std::atomic<bool> running_      {false};
    std::atomic<bool> need_restart_ {false};
    std::thread       thread_;

    Capabilities capabilities_ {};
};

}  // namespace lw::source
