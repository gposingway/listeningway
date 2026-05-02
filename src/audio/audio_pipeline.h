// ---------------------------------------------
// AudioPipeline
// Single owner of the live audio capture/analysis lifecycle.
// Replaces the previous global graph (g_audio_thread_*, g_audio_data,
// g_audio_capture_manager, g_switching_provider) with one class that holds
// the state and the synchronization primitives that protect it.
// ---------------------------------------------
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "audio/analysis/audio_analysis.h"
#include "audio/capture/audio_capture_manager.h"
#include "configuration/configuration_manager.h"

namespace Listeningway {

class AudioPipeline {
public:
    explicit AudioPipeline(AudioAnalyzer& analyzer);
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    /// Initialize capture manager, start analyzer, start capture thread.
    bool Start();

    /// Stop capture thread, stop analyzer, uninitialize capture manager.
    void Stop();

    /// Switch to a provider by integer type (0=System, 1=Process, -1=Off).
    /// Persists the choice in Configuration. Atomic against concurrent calls.
    bool SwitchProvider(int providerType);

    /// Switch to a provider by configuration code ("system", "off", ...).
    bool SwitchProviderByCode(const std::string& code);

    /// Restart capture+analyzer with the current configuration. Used after
    /// config edits that require the audio thread to pick up new values.
    bool Restart();

    /// If the published volume hasn't changed for ~stale_timeout seconds,
    /// transparently restart the capture thread. Called periodically from
    /// the overlay callback.
    void MaybeRestartIfStale();

    /// Returns true while a capture thread is running.
    bool IsRunning() const { return thread_running_.load(); }

    /// Copy of the latest analysis data, taken under the pipeline mutex.
    AudioAnalysisData Snapshot() const;

    /// Direct access to the capture manager (read-only operations like
    /// enumerating providers). May be nullptr before Start().
    AudioCaptureManager* CaptureManager() { return capture_manager_.get(); }
    const AudioCaptureManager* CaptureManager() const { return capture_manager_.get(); }

private:
    bool StartCaptureLocked(const Listeningway::Configuration& config);
    void StopCaptureLocked();

    AudioAnalyzer& analyzer_;
    std::unique_ptr<AudioCaptureManager> capture_manager_;

    mutable std::mutex data_mutex_;
    AudioAnalysisData data_;

    std::atomic_bool thread_running_{false};
    std::thread thread_;

    // Provider-switch serialization
    std::mutex switch_mutex_;
    std::atomic_bool switching_{false};

    // Stale-detection state
    std::chrono::steady_clock::time_point last_audio_update_{std::chrono::steady_clock::now()};
    float last_volume_{0.0f};
};

/// The single AudioPipeline owned by the addon. Set up in DllMain.
extern std::unique_ptr<AudioPipeline> g_pipeline;

} // namespace Listeningway
