#pragma once

#include "configuration.h"
#include <mutex>
#include <string>
#include <vector>

namespace Listeningway {

/**
 * @brief Centralized configuration manager (singleton)
 *
 * Owns a single static Configuration instance, provides thread-safe access,
 * and handles all provider logic and persistence.
 */
class ConfigurationManager {
public:
    // Singleton access
    static ConfigurationManager& Instance();

    // Static access to the configuration (always available)
    static const Configuration& Config();
    static const Configuration& ConfigConst();

    // Thread-safe access (for advanced use)
    Configuration& GetConfig();
    const Configuration& GetConfig() const;

    // Returns an immutable copy of the configuration for thread-safe use in background threads
    static Configuration Snapshot();    // Save/load/reset (no parameters, always use default path)
    bool Save();
    bool Load();
    void ResetToDefaults();

    // Applies the current config to all live systems (analyzer, capture, etc.)
    void ApplyConfigToLiveSystems();
    
    // Robustly restart audio analysis systems
    void RestartAudioSystems();

    // Provider logic: enumerate, validate, set default if needed
    void EnsureValidProvider();
    std::vector<std::string> EnumerateAvailableProviders() const;
    std::string GetDefaultProviderCode() const;

    // Thread-safe setter for analysisEnabled
    void SetAnalysisEnabled(bool enabled);

private:
    ConfigurationManager();
    ~ConfigurationManager() = default;
    ConfigurationManager(const ConfigurationManager&) = delete;
    ConfigurationManager& operator=(const ConfigurationManager&) = delete;

    static Configuration m_config;
    mutable std::mutex m_mutex;

    // Helper for provider logic
    void ValidateProvider();
};

} // namespace Listeningway