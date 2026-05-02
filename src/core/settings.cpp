#include "settings.h"
#include "audio/analysis/audio_analysis.h"
#include "logging.h"
#include "configuration/configuration_manager.h"
#include <windows.h>
#include <string>

using Listeningway::ConfigurationManager;

namespace {

std::string GetDllDirectory() {
    char dllPath[MAX_PATH] = {};
    HMODULE hModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetDllDirectory, &hModule);
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    std::string path(dllPath);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) path = path.substr(0, pos + 1);
    return path;
}

} // namespace

std::string GetSettingsPath() {
    return GetDllDirectory() + "Listeningway.json";
}

std::string GetLogFilePath() {
    return GetDllDirectory() + "listeningway.log";
}

bool GetAudioAnalysisEnabled() {
    return ConfigurationManager::Snapshot().audio.analysisEnabled;
}

void SetAudioAnalysisEnabled(bool enabled) {
    auto& cm = ConfigurationManager::Instance();
    cm.SetAnalysisEnabled(enabled);
    cm.Save();

    if (enabled) {
        const auto config = ConfigurationManager::Snapshot();
        g_audio_analyzer.SetBeatDetectionAlgorithm(config.beat.algorithm);
        g_audio_analyzer.Start();
        LOG_DEBUG("[Settings] Audio analyzer started with algorithm: " +
                  std::to_string(config.beat.algorithm));
    } else {
        g_audio_analyzer.Stop();
        LOG_DEBUG("[Settings] Audio analyzer stopped");
    }
}

bool GetDebugEnabled() {
    return ConfigurationManager::Snapshot().debug.debugEnabled;
}

void SetDebugEnabled(bool enabled) {
    auto& cm = ConfigurationManager::Instance();
    cm.GetConfig().debug.debugEnabled = enabled;
    cm.Save();
    SetLogDebugEnabled(enabled);
}

void LoadAllTunables() {
    ConfigurationManager::Instance().Load();
    SetLogDebugEnabled(ConfigurationManager::Snapshot().debug.debugEnabled);
    LOG_DEBUG("[Settings] Loaded all tunables");
}

void SaveAllTunables() {
    if (GetAudioAnalysisEnabled()) {
        const auto config = ConfigurationManager::Snapshot();
        g_audio_analyzer.SetBeatDetectionAlgorithm(config.beat.algorithm);
    }
    ConfigurationManager::Instance().Save();
    LOG_DEBUG("[Settings] Saved all tunables");
}

void ResetAllTunablesToDefaults() {
    ConfigurationManager::Instance().ResetToDefaults();
    SetLogDebugEnabled(ConfigurationManager::Snapshot().debug.debugEnabled);
    if (GetAudioAnalysisEnabled()) {
        const auto config = ConfigurationManager::Snapshot();
        g_audio_analyzer.SetBeatDetectionAlgorithm(config.beat.algorithm);
    }
    LOG_DEBUG("[Settings] Reset all tunables to defaults");
}
