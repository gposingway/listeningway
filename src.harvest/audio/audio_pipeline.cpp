// ---------------------------------------------
// AudioPipeline implementation
// ---------------------------------------------
#include "audio_pipeline.h"

#include "../utils/logging.h"
#include "../core/constants.h"

namespace Listeningway {

std::unique_ptr<AudioPipeline> g_pipeline;

AudioPipeline::AudioPipeline(AudioAnalyzer& analyzer) : analyzer_(analyzer) {}

AudioPipeline::~AudioPipeline() {
    Stop();
}

bool AudioPipeline::StartCaptureLocked(const Listeningway::Configuration& config) {
    if (!capture_manager_) return false;
    return capture_manager_->StartCapture(config, thread_running_, thread_, data_, data_mutex_);
}

void AudioPipeline::StopCaptureLocked() {
    if (capture_manager_) {
        capture_manager_->StopCapture(thread_running_, thread_);
    } else {
        thread_running_ = false;
        if (thread_.joinable()) thread_.join();
    }
}

bool AudioPipeline::Start() {
    if (!capture_manager_) {
        capture_manager_ = std::make_unique<AudioCaptureManager>();
        capture_manager_->Initialize();

        const auto& code = ConfigurationManager::Config().audio.captureProviderCode;
        if (!code.empty()) {
            capture_manager_->SetPreferredProviderByCode(code);
        }
    }

    const auto config = ConfigurationManager::Snapshot();
    analyzer_.SetBeatDetectionAlgorithm(config.beat.algorithm);
    analyzer_.Start();
    LOG_DEBUG("[Pipeline] Audio analyzer started (algorithm=" +
              std::to_string(config.beat.algorithm) + ")");

    if (!StartCaptureLocked(config)) {
        LOG_ERROR("[Pipeline] StartCapture failed");
        return false;
    }
    LOG_DEBUG("[Pipeline] Capture thread started");
    last_audio_update_ = std::chrono::steady_clock::now();
    last_volume_ = 0.0f;
    return true;
}

void AudioPipeline::Stop() {
    StopCaptureLocked();
    analyzer_.Stop();
    if (capture_manager_) {
        capture_manager_->Uninitialize();
        capture_manager_.reset();
    }
    LOG_DEBUG("[Pipeline] Stopped");
}

bool AudioPipeline::Restart() {
    LOG_DEBUG("[Pipeline] Restart requested");
    const auto config = ConfigurationManager::Snapshot();
    StopCaptureLocked();
    analyzer_.Stop();
    analyzer_.SetBeatDetectionAlgorithm(config.beat.algorithm);
    analyzer_.Start();
    return StartCaptureLocked(config);
}

bool AudioPipeline::SwitchProvider(int providerType) {
    {
        std::lock_guard<std::mutex> lock(switch_mutex_);
        if (switching_) {
            LOG_DEBUG("[Pipeline] SwitchProvider: already switching, ignoring");
            return false;
        }
        switching_ = true;
    }
    LOG_DEBUG("[Pipeline] SwitchProvider: type=" + std::to_string(providerType));

    bool ok = true;
    try {
        // -1 (or any negative) means "Off": persist and tear down.
        if (providerType < 0) {
            {
                auto& cfg = ConfigurationManager::Instance().GetConfig();
                cfg.audio.captureProviderCode = "off";
                ConfigurationManager::Instance().SetAnalysisEnabled(false);
                ConfigurationManager::Instance().Save();
            }
            if (thread_running_.load()) {
                StopCaptureLocked();
            }
            const auto snap = ConfigurationManager::Snapshot();
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                data_ = AudioAnalysisData(snap.frequency.bands);
            }
        } else {
            const bool was_running = thread_running_.load();
            {
                auto& cfg = ConfigurationManager::Instance().GetConfig();
                cfg.audio.captureProviderCode = (providerType == 0 ? std::string("system") : std::string("game"));
                ConfigurationManager::Instance().SetAnalysisEnabled(true);
                ConfigurationManager::Instance().Save();
            }
            const auto config = ConfigurationManager::Snapshot();
            if (capture_manager_) {
                ok = capture_manager_->SwitchProviderAndRestart(
                    static_cast<AudioCaptureProviderType>(providerType),
                    config, thread_running_, thread_, data_, data_mutex_);
            } else {
                ok = false;
            }
            if (ok && !was_running && !thread_running_.load()) {
                LOG_DEBUG("[Pipeline] Cold-starting analyzer/capture after switch from Off");
                analyzer_.Start();
                ok = StartCaptureLocked(config);
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Pipeline] SwitchProvider exception: ") + ex.what());
        ok = false;
    } catch (...) {
        LOG_ERROR("[Pipeline] SwitchProvider unknown exception");
        ok = false;
    }

    {
        std::lock_guard<std::mutex> lock(switch_mutex_);
        switching_ = false;
    }
    return ok;
}

bool AudioPipeline::SwitchProviderByCode(const std::string& code) {
    if (code == "off") return SwitchProvider(-1);
    if (code == "system") return SwitchProvider(0);
    if (code == "game" || code == "process") return SwitchProvider(1);

    {
        std::lock_guard<std::mutex> lock(switch_mutex_);
        if (switching_) return false;
        switching_ = true;
    }
    bool ok = false;
    if (capture_manager_) {
        const auto config = ConfigurationManager::Snapshot();
        ok = capture_manager_->SwitchProviderByCodeAndRestart(
            code, config, thread_running_, thread_, data_, data_mutex_);
    }
    {
        std::lock_guard<std::mutex> lock(switch_mutex_);
        switching_ = false;
    }
    return ok;
}

void AudioPipeline::MaybeRestartIfStale() {
    float current_volume;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        current_volume = data_.volume;
    }
    auto now = std::chrono::steady_clock::now();
    if (current_volume != last_volume_) {
        last_audio_update_ = now;
        last_volume_ = current_volume;
        return;
    }
    auto stale_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_audio_update_).count();
    if (stale_ms > static_cast<int>(DEFAULT_CAPTURE_STALE_TIMEOUT * 1000.0f)) {
        LOG_DEBUG("[Pipeline] Audio capture stale, attempting restart");
        if (capture_manager_) {
            const auto config = ConfigurationManager::Snapshot();
            capture_manager_->CheckAndRestartCapture(
                config, thread_running_, thread_, data_, data_mutex_);
        }
        last_audio_update_ = now; // prevent rapid restarts
    }
}

AudioAnalysisData AudioPipeline::Snapshot() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return data_;
}

} // namespace Listeningway
