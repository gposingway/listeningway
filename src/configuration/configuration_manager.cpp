#include "configuration_manager.h"
#include "logging.h"
#include <algorithm>
#include <set>
#include "audio/analysis/audio_analysis.h"
#include "audio/capture/audio_capture.h"
#include <thread>
#include <mutex>

extern AudioAnalyzer g_audio_analyzer;

namespace Listeningway {

// Static configuration instance
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
    // Use the robust restart method after loading
    RestartAudioSystems();
    return loaded;
}

void ConfigurationManager::ResetToDefaults() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config.ResetToDefaults();
        // Always set provider code to the default after resetting
        m_config.audio.captureProviderCode = GetDefaultProviderCode();
        ValidateProvider();
        // Set analysisEnabled based on the default provider's activates_capture
        auto provider_code = m_config.audio.captureProviderCode;
        auto available = EnumerateAvailableProviders();
        bool activates_capture = false;
        for (const auto& code : available) {
            if (code == provider_code) {
                if (g_audio_capture_manager) {
                    for (const auto& info : g_audio_capture_manager->GetAvailableProviderInfos()) {
                        if (info.code == provider_code) {
                            activates_capture = info.activates_capture;
                            break;
                        }
                    }
                }
                break;
            }
        }
        m_config.audio.analysisEnabled = activates_capture;
    }
    // Use the robust restart method instead of ApplyConfigToLiveSystems
    RestartAudioSystems();
}

void ConfigurationManager::EnsureValidProvider() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ValidateProvider();
}

std::vector<std::string> ConfigurationManager::EnumerateAvailableProviders() const {
    std::vector<std::string> codes;
    if (g_audio_capture_manager) {
        for (const auto& info : g_audio_capture_manager->GetAvailableProviderInfos()) {
            codes.push_back(info.code);
        }
    }
    return codes;
}

std::string ConfigurationManager::GetDefaultProviderCode() const {
    // Prefer the provider flagged as is_default by AudioCaptureManager
    if (g_audio_capture_manager) {
        for (const auto& info : g_audio_capture_manager->GetAvailableProviderInfos()) {
            if (info.is_default) return info.code;
        }
    }
    // Fallback: first non-off enumerated provider
    auto available = EnumerateAvailableProviders();
    for (const auto& code : available) {
        if (code != "off") return code;
    }
    return !available.empty() ? available[0] : "off";
}

void ConfigurationManager::ValidateProvider() {
    auto available = EnumerateAvailableProviders();
    auto& code = m_config.audio.captureProviderCode;
    // If code is empty or not in available list, select default
    if (code.empty() || std::find(available.begin(), available.end(), code) == available.end()) {
        // Select the provider with is_default flag
        if (g_audio_capture_manager) {
            for (const auto& info : g_audio_capture_manager->GetAvailableProviderInfos()) {
                if (info.is_default) { code = info.code; return; }
            }
        }
        // Fallback to first non-off
        for (const auto& c : available) { if (c != "off") { code = c; return; } }
    }
}

void ConfigurationManager::ApplyConfigToLiveSystems() {
    LOG_DEBUG("[ConfigurationManager] Applying configuration to live systems...");
    
    try {
        // Apply beat detection algorithm configuration
        LOG_DEBUG("[ConfigurationManager] Setting beat detection algorithm: " + std::to_string(m_config.beat.algorithm));
        g_audio_analyzer.SetBeatDetectionAlgorithm(m_config.beat.algorithm);
        
        // Delegate audio system management to AudioCaptureManager
        extern std::unique_ptr<AudioCaptureManager> g_audio_capture_manager;
        if (g_audio_capture_manager) {
            if (!g_audio_capture_manager->ApplyConfiguration(m_config)) {
                LOG_ERROR("[ConfigurationManager] Failed to apply configuration to audio system");
            }
        } else {
            LOG_WARNING("[ConfigurationManager] AudioCaptureManager not available, cannot apply audio configuration");
        }
        
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
        // Create a local copy of the configuration while holding the lock
        Configuration config_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Validate provider before restarting
            ValidateProvider();
            // Copy the configuration for use outside the lock
            config_copy = m_config;
        }
        
        // Apply beat detection algorithm (this doesn't need the lock)
        g_audio_analyzer.SetBeatDetectionAlgorithm(config_copy.beat.algorithm);
        
        // Delegate audio system management to AudioCaptureManager
        // This is done outside the lock to prevent deadlock
        extern std::unique_ptr<AudioCaptureManager> g_audio_capture_manager;
        if (g_audio_capture_manager) {
            if (!g_audio_capture_manager->RestartAudioSystem(config_copy)) {
                LOG_ERROR("[ConfigurationManager] Failed to restart audio system");
            }
        } else {
            LOG_WARNING("[ConfigurationManager] AudioCaptureManager not available, cannot restart audio system");
        }
        
        LOG_DEBUG("[ConfigurationManager] Audio systems restart completed successfully");
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
    // Attempt to load config at startup
    bool loaded = m_config.Load();
    if (!loaded) {
        LOG_WARNING("[ConfigurationManager] No config file found, using defaults.");
        m_config.ResetToDefaults();
    }
    // Ensure provider is valid and select default if needed
    ValidateProvider();
}

void ConfigurationManager::SetAnalysisEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.audio.analysisEnabled = enabled;
}

} // namespace Listeningway