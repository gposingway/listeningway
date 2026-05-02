#include "configuration_manager.h"
#include "logging.h"
#include <algorithm>
#include "audio/analysis/audio_analysis.h"
#include "audio/audio_pipeline.h"
#include "audio/capture/audio_capture_manager.h"
#include <thread>
#include <mutex>

extern AudioAnalyzer g_audio_analyzer;

namespace Listeningway {

namespace {

AudioCaptureManager* CaptureManagerOrNull() {
    return g_pipeline ? g_pipeline->CaptureManager() : nullptr;
}

} // namespace

Configuration ConfigurationManager::m_config = {};

ConfigurationManager& ConfigurationManager::Instance() {
    static ConfigurationManager instance;
    return instance;
}

const Configuration& ConfigurationManager::Config() {
    return Instance().GetConfig();
}

const Configuration& ConfigurationManager::ConfigConst() {
    return Instance().GetConfig();
}

Configuration& ConfigurationManager::GetConfig() {
    return m_config;
}

const Configuration& ConfigurationManager::GetConfig() const {
    return m_config;
}

bool ConfigurationManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.Save();
}

bool ConfigurationManager::Load() {
    bool loaded;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        loaded = m_config.Load();
        ValidateProvider();
        m_config.Validate();
    }
    RestartAudioSystems();
    return loaded;
}

void ConfigurationManager::ResetToDefaults() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.ResetToDefaults();
        m_config.audio.captureProviderCode = GetDefaultProviderCode();
        ValidateProvider();
        bool activates_capture = false;
        if (auto* mgr = CaptureManagerOrNull()) {
            for (const auto& info : mgr->GetAvailableProviderInfos()) {
                if (info.code == m_config.audio.captureProviderCode) {
                    activates_capture = info.activates_capture;
                    break;
                }
            }
        }
        m_config.audio.analysisEnabled = activates_capture;
    }
    RestartAudioSystems();
}

void ConfigurationManager::EnsureValidProvider() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ValidateProvider();
}

std::vector<std::string> ConfigurationManager::EnumerateAvailableProviders() const {
    std::vector<std::string> codes;
    if (auto* mgr = CaptureManagerOrNull()) {
        for (const auto& info : mgr->GetAvailableProviderInfos()) {
            codes.push_back(info.code);
        }
    }
    return codes;
}

std::string ConfigurationManager::GetDefaultProviderCode() const {
    if (auto* mgr = CaptureManagerOrNull()) {
        for (const auto& info : mgr->GetAvailableProviderInfos()) {
            if (info.is_default) return info.code;
        }
    }
    auto available = EnumerateAvailableProviders();
    for (const auto& code : available) {
        if (code != "off") return code;
    }
    return !available.empty() ? available[0] : "off";
}

void ConfigurationManager::ValidateProvider() {
    auto available = EnumerateAvailableProviders();
    auto& code = m_config.audio.captureProviderCode;
    if (code.empty() || std::find(available.begin(), available.end(), code) == available.end()) {
        if (auto* mgr = CaptureManagerOrNull()) {
            for (const auto& info : mgr->GetAvailableProviderInfos()) {
                if (info.is_default) { code = info.code; return; }
            }
        }
        for (const auto& c : available) { if (c != "off") { code = c; return; } }
    }
}

void ConfigurationManager::ApplyConfigToLiveSystems() {
    LOG_DEBUG("[ConfigurationManager] Applying configuration to live systems...");
    try {
        g_audio_analyzer.SetBeatDetectionAlgorithm(m_config.beat.algorithm);
        // The pipeline owns lifecycle; no further action needed unless we want
        // to fully restart, in which case callers should use RestartAudioSystems().
        LOG_DEBUG("[ConfigurationManager] Configuration applied successfully");
    } catch (const std::exception& ex) {
        LOG_ERROR("[ConfigurationManager] Error applying config to live systems: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("[ConfigurationManager] Unknown error applying config to live systems");
    }
}

void ConfigurationManager::RestartAudioSystems() {
    LOG_DEBUG("[ConfigurationManager] Restarting audio systems...");
    try {
        Configuration config_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ValidateProvider();
            config_copy = m_config;
        }
        g_audio_analyzer.SetBeatDetectionAlgorithm(config_copy.beat.algorithm);
        if (g_pipeline) {
            if (!g_pipeline->Restart()) {
                LOG_ERROR("[ConfigurationManager] Pipeline restart failed");
            }
        } else {
            LOG_DEBUG("[ConfigurationManager] Pipeline not available; skipping restart");
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("[ConfigurationManager] Error restarting audio systems: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("[ConfigurationManager] Unknown error restarting audio systems");
    }
}

Configuration ConfigurationManager::Snapshot() {
    std::lock_guard<std::mutex> lock(Instance().m_mutex);
    return m_config;
}

ConfigurationManager::ConfigurationManager() {
    bool loaded = m_config.Load();
    if (!loaded) {
        LOG_WARNING("[ConfigurationManager] No config file found, using defaults.");
        m_config.ResetToDefaults();
    }
    ValidateProvider();
}

void ConfigurationManager::SetAnalysisEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.audio.analysisEnabled = enabled;
}

} // namespace Listeningway
