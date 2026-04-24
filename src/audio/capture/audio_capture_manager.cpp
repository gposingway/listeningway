// ---------------------------------------------
// Audio Capture Manager Implementation
// Manages audio capture providers and handles provider selection
// ---------------------------------------------
#include "audio_capture_manager.h"
#include "audio/capture/providers/audio_capture_provider_system.h"
#include "audio/capture/providers/audio_capture_provider_off.h"
#include "../utils/logging.h"
#include "../utils/debug_notes.h"
#include "../core/thread_safety_manager.h"
#include <algorithm>
#include <climits>

AudioCaptureManager::AudioCaptureManager() 
    : preferred_provider_type_(AudioCaptureProviderType::SYSTEM_AUDIO),
      current_provider_(nullptr),
      initialized_(false) {
}

AudioCaptureManager::~AudioCaptureManager() {
    Uninitialize();
}

bool AudioCaptureManager::Initialize() {
    if (initialized_) {
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Initialize: already initialized");
        }
        return true;
    }

    LOG_DEBUG("[AudioCaptureManager] Initializing audio capture manager");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("ACM.Initialize: begin");
    }
    
    RegisterProviders();
    
    // Initialize all providers
    for (auto& provider : providers_) {
        if (!provider->Initialize()) {
            LOG_WARNING("[AudioCaptureManager] Failed to initialize provider: " + provider->GetProviderName());
        }
    }
    // Choose preferred provider based on is_default flag
    for (auto& provider : providers_) {
        if (provider->IsAvailable() && provider->GetProviderInfo().is_default) {
            preferred_provider_type_ = provider->GetProviderType();
            // Sync config to the default provider code
            auto& cfg = Listeningway::ConfigurationManager::Instance().GetConfig();
            cfg.audio.captureProviderCode = provider->GetProviderInfo().code;
            Listeningway::ConfigurationManager::Instance().Save();
            LOG_DEBUG("[AudioCaptureManager] Preferred provider set to is_default: " + provider->GetProviderName());
            break;
        }
    }
    // Select the initial provider
    current_provider_ = SelectBestProvider();
    if (!current_provider_) {
        LOG_ERROR("[AudioCaptureManager] No available audio capture providers");
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Initialize: no providers available");
        }
        return false;
    }
    
    LOG_INFO("[AudioCaptureManager] Initialized with provider: " + current_provider_->GetProviderName());
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add(std::string("ACM.Initialized with ") + current_provider_->GetProviderInfo().code);
    }
    initialized_ = true;
    return true;
}

void AudioCaptureManager::Uninitialize() {
    if (!initialized_) {
        return;
    }
    LOG_DEBUG("[AudioCaptureManager] Uninitializing audio capture manager");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("ACM.Uninitialize");
    }
    
    current_provider_ = nullptr;
    
    // Uninitialize all providers
    for (auto& provider : providers_) {
        provider->Uninitialize();
    }
    
    providers_.clear();
    initialized_ = false;
}

void AudioCaptureManager::RegisterProviders() {
    // Register system audio provider (always available)
    providers_.push_back(std::make_unique<AudioCaptureProviderSystem>());
    
    // Register off audio provider (dummy provider)
    providers_.push_back(std::make_unique<AudioCaptureProviderOff>());
    
    LOG_DEBUG("[AudioCaptureManager] Registered " + std::to_string(providers_.size()) + " audio capture providers");
}

std::vector<AudioCaptureProviderType> AudioCaptureManager::GetAvailableProviders() const {
    std::vector<AudioCaptureProviderType> available;
    
    for (const auto& provider : providers_) {
        if (provider->IsAvailable()) {
            available.push_back(provider->GetProviderType());
        }
    }
    
    return available;
}

std::vector<AudioProviderInfo> AudioCaptureManager::GetAvailableProviderInfos() const {
    std::vector<AudioProviderInfo> infos;
    for (const auto& provider : providers_) {
        if (provider->IsAvailable()) {
            infos.push_back(provider->GetProviderInfo());
        }
    }
    
    // Sort providers by their order field
    std::sort(infos.begin(), infos.end(), [](const AudioProviderInfo& a, const AudioProviderInfo& b) {
        return a.order < b.order;
    });
    
    return infos;
}

std::string AudioCaptureManager::GetProviderName(AudioCaptureProviderType type) const {
    IAudioCaptureProvider* provider = FindProvider(type);
    if (provider) {
        return provider->GetProviderName();
    }
    return "Unknown Provider";
}

bool AudioCaptureManager::SetPreferredProvider(AudioCaptureProviderType type) {
    IAudioCaptureProvider* provider = FindProvider(type);
    if (!provider || !provider->IsAvailable()) {
        LOG_WARNING("[AudioCaptureManager] Preferred provider not available: " + GetProviderName(type));
        return false;
    }
    
    preferred_provider_type_ = type;
    // Update live configuration: provider code and analysisEnabled
    auto& cfg = Listeningway::ConfigurationManager::Instance().GetConfig();
    cfg.audio.captureProviderCode = provider->GetProviderInfo().code;
    Listeningway::ConfigurationManager::Instance().SetAnalysisEnabled(provider->GetProviderInfo().activates_capture);
    Listeningway::ConfigurationManager::Instance().Save();
    LOG_INFO("[AudioCaptureManager] Set preferred provider to: " + provider->GetProviderName());
    
    // If we're currently using a different provider, switch to the preferred one
    if (!current_provider_ || current_provider_->GetProviderType() != type) {
        // Stop current capture if running
        if (current_provider_) {
            LOG_INFO("[AudioCaptureManager] Stopping capture for old provider: " + current_provider_->GetProviderName());
            // We need to stop the thread. We'll require a reference to the running/thread/data_mutex/data from the caller.
            // Instead, let's add a new method to handle this at a higher level.
        }
        current_provider_ = provider;
        LOG_INFO("[AudioCaptureManager] Switched to preferred provider: " + current_provider_->GetProviderName());
    }
    
    return true;
}

bool AudioCaptureManager::SetPreferredProviderByCode(const std::string& providerCode) {
    // Find provider by code
    for (const auto& provider : providers_) {
        if (provider->GetProviderInfo().code == providerCode) {
            return SetPreferredProvider(provider->GetProviderType());
        }
    }
    
    LOG_WARNING("[AudioCaptureManager] Provider with code '" + providerCode + "' not found");
    return false;
}

// New method: Switch provider and restart capture thread if running
bool AudioCaptureManager::SwitchProviderAndRestart(AudioCaptureProviderType type, const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data) {
    bool was_running = running.load();
    if (was_running) {
        StopCapture(running, thread);
    }
    bool set_ok = SetPreferredProvider(type);
    if (!set_ok) return false;
    if (was_running) {
        return StartCapture(config, running, thread, data);
    }
    return true;
}

// New method: Switch provider by code and restart capture thread if running
bool AudioCaptureManager::SwitchProviderByCodeAndRestart(const std::string& providerCode, const Listeningway::Configuration& config, std::atomic_bool& running, std::thread& thread, AudioAnalysisData& data) {
    // Handle "off" code specially - stop capture and don't switch to any provider
    if (providerCode == "off") {
        if (running.load()) {
            StopCapture(running, thread);
        }
        LOG_DEBUG("[AudioCaptureManager] Switched to 'off' - audio analysis disabled");
        return true;
    }
    
    // Find provider by code
    IAudioCaptureProvider* target_provider = nullptr;
    for (const auto& provider : providers_) {
        if (provider->IsAvailable() && provider->GetProviderInfo().code == providerCode) {
            target_provider = provider.get();
            break;
        }
    }
    
    if (!target_provider) {
        LOG_ERROR("[AudioCaptureManager] Provider with code '" + providerCode + "' not found or not available");
        return false;
    }
    
    // Use the existing method with the provider type
    bool was_running = running.load();
    bool ok = SwitchProviderAndRestart(target_provider->GetProviderType(), config, running, thread, data);
    if (ok && !was_running && !running.load() && target_provider->GetProviderInfo().activates_capture) {
        // If we were previously Off (no running thread), start analyzer and capture now
        extern AudioAnalyzer g_audio_analyzer;
        LOG_DEBUG("[AudioCaptureManager] Starting analyzer and capture after switching from Off by code");
        g_audio_analyzer.Start();
        ok = StartCapture(config, running, thread, data);
    }
    return ok;
}

AudioCaptureProviderType AudioCaptureManager::GetCurrentProvider() const {
    if (current_provider_) {
        return current_provider_->GetProviderType();
    }
    return AudioCaptureProviderType::SYSTEM_AUDIO; // Default fallback
}

IAudioCaptureProvider* AudioCaptureManager::FindProvider(AudioCaptureProviderType type) const {
    for (const auto& provider : providers_) {
        if (provider->GetProviderType() == type) {
            return provider.get();
        }
    }
    return nullptr;
}

IAudioCaptureProvider* AudioCaptureManager::SelectBestProvider() {
    // First, try the preferred provider if available
    IAudioCaptureProvider* preferred = FindProvider(preferred_provider_type_);
    if (preferred && preferred->IsAvailable()) {
        LOG_DEBUG("[AudioCaptureManager] Selected preferred provider: " + preferred->GetProviderName());
        return preferred;
    }
    
    // If no preferred provider, look for a provider marked as default
    for (const auto& provider : providers_) {
        if (provider->IsAvailable() && provider->GetProviderInfo().is_default) {
            LOG_DEBUG("[AudioCaptureManager] Selected default provider: " + provider->GetProviderName());
            return provider.get();
        }
    }
    
    // Fall back to any available provider, prioritizing by order
    IAudioCaptureProvider* best_provider = nullptr;
    int best_order = INT_MAX;
    
    for (const auto& provider : providers_) {
        if (provider->IsAvailable()) {
            const auto& info = provider->GetProviderInfo();
            if (info.order < best_order) {
                best_provider = provider.get();
                best_order = info.order;
            }
        }
    }
    
    if (best_provider) {
        LOG_DEBUG("[AudioCaptureManager] Selected fallback provider: " + best_provider->GetProviderName());
        return best_provider;
    }
    
    LOG_ERROR("[AudioCaptureManager] No available providers found");
    return nullptr;
}

bool AudioCaptureManager::StartCapture(const Listeningway::Configuration& config, 
                                      std::atomic_bool& running, 
                                      std::thread& thread, 
                                      AudioAnalysisData& data) {
    if (!initialized_ || !current_provider_) {
        LOG_ERROR("[AudioCaptureManager] Cannot start capture - not initialized or no provider");
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.StartCapture: not initialized or no provider");
        }
        return false;
    }
    
    LOG_DEBUG("[AudioCaptureManager] Starting capture with provider: " + current_provider_->GetProviderName());
    bool ok = current_provider_->StartCapture(config, running, thread, data);
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add(std::string("ACM.StartCapture -> ") + (ok ? "OK" : "FAIL"));
    }
    return ok;
}

void AudioCaptureManager::StopCapture(std::atomic_bool& running, std::thread& thread) {
    if (current_provider_) {
        LOG_DEBUG("[AudioCaptureManager] Stopping capture");
        current_provider_->StopCapture(running, thread);
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.StopCapture");
        }
    }
}

void AudioCaptureManager::CheckAndRestartCapture(const Listeningway::Configuration& config, 
                                                 std::atomic_bool& running, 
                                                 std::thread& thread, 
                                                 AudioAnalysisData& data) {
    if (!current_provider_) {
        return;
    }
    
    // Check if current provider needs restart
    if (current_provider_->ShouldRestart()) {
        LOG_DEBUG("[AudioCaptureManager] Provider requesting restart");
        StopCapture(running, thread);
        current_provider_->ResetRestartFlags();
        StartCapture(config, running, thread, data);
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Check: provider restart");
        }
        return;
    }
    
    // Check if we should switch to a different provider
    IAudioCaptureProvider* best_provider = SelectBestProvider();
    if (best_provider && best_provider != current_provider_) {
        LOG_INFO("[AudioCaptureManager] Switching to better provider: " + best_provider->GetProviderName());
        StopCapture(running, thread);
        current_provider_ = best_provider;
        StartCapture(config, running, thread, data);
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add(std::string("ACM.Check: switched -> ") + best_provider->GetProviderInfo().code);
        }
    }
}

// Implementation of the new audio system lifecycle management methods
bool AudioCaptureManager::RestartAudioSystem(const Listeningway::Configuration& config) {
    LOG_DEBUG("[AudioCaptureManager] Restarting audio system with new configuration");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("ACM.RestartAudioSystem");
    }
      // Get access to global audio system variables
    extern std::atomic_bool g_audio_thread_running;
    extern std::thread g_audio_thread;
    extern AudioAnalysisData g_audio_data;
    extern AudioAnalyzer g_audio_analyzer;
    
    try {
        // Stop the current audio system
        bool was_running = g_audio_thread_running.load();
        if (was_running) {
            LOG_DEBUG("[AudioCaptureManager] Stopping current audio capture");
            StopCapture(g_audio_thread_running, g_audio_thread);
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                DebugNotes::Add("ACM.Restart: stopped capture");
            }
        }
        
        // Stop the analyzer
        g_audio_analyzer.Stop();
        
        // Wait a moment for clean shutdown
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Start the analyzer with new config
        LOG_DEBUG("[AudioCaptureManager] Starting audio analyzer");
        g_audio_analyzer.Start();
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Restart: analyzer started");
        }
        
        // Restart capture if it was running
        if (was_running) {
            LOG_DEBUG("[AudioCaptureManager] Restarting audio capture");
            if (!StartCapture(config, g_audio_thread_running, g_audio_thread, g_audio_data)) {
                LOG_ERROR("[AudioCaptureManager] Failed to restart audio capture");
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("ACM.Restart: StartCapture FAIL");
                }
                return false;
            }
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                DebugNotes::Add("ACM.Restart: StartCapture OK");
            }
        }
        
        LOG_DEBUG("[AudioCaptureManager] Audio system restart completed successfully");
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("[AudioCaptureManager] Exception during audio system restart: " + std::string(ex.what()));
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Restart: exception");
        }
        return false;
    } catch (...) {
        LOG_ERROR("[AudioCaptureManager] Unknown exception during audio system restart");
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Restart: unknown exception");
        }
        return false;
    }
}

void AudioCaptureManager::StopAudioSystem() {
    LOG_DEBUG("[AudioCaptureManager] Stopping audio system");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("ACM.StopAudioSystem");
    }
    
    // Get access to global audio system variables
    extern std::atomic_bool g_audio_thread_running;
    extern std::thread g_audio_thread;
    extern AudioAnalyzer g_audio_analyzer;
    
    try {
        // Stop capture if running
    if (g_audio_thread_running.load()) {
            LOG_DEBUG("[AudioCaptureManager] Stopping audio capture");
            StopCapture(g_audio_thread_running, g_audio_thread);
        }
        
        // Stop the analyzer
        LOG_DEBUG("[AudioCaptureManager] Stopping audio analyzer");
        g_audio_analyzer.Stop();
        
        LOG_DEBUG("[AudioCaptureManager] Audio system stopped successfully");
    } catch (const std::exception& ex) {
        LOG_ERROR("[AudioCaptureManager] Exception during audio system stop: " + std::string(ex.what()));
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Stop: exception");
        }
    } catch (...) {
        LOG_ERROR("[AudioCaptureManager] Unknown exception during audio system stop");
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add("ACM.Stop: unknown exception");
        }
    }
}

bool AudioCaptureManager::ApplyConfiguration(const Listeningway::Configuration& config) {
    LOG_DEBUG("[AudioCaptureManager] Applying new configuration to audio system");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("ACM.ApplyConfiguration");
    }
    extern std::atomic_bool g_audio_thread_running;
    extern std::thread g_audio_thread;
    extern AudioAnalysisData g_audio_data;
    extern AudioAnalyzer g_audio_analyzer;

    static bool last_activates_capture = true; // Track previous provider state
    bool activates_capture = false;
    // Find the provider info for the requested provider code
    IAudioCaptureProvider* target_provider = nullptr;
    for (const auto& provider : providers_) {
        if (provider->IsAvailable() && provider->GetProviderInfo().code == config.audio.captureProviderCode) {
            target_provider = provider.get();
            break;
        }
    }
    if (target_provider) {
        activates_capture = target_provider->GetProviderInfo().activates_capture;
    }
    // If switching from capture to none, stop and clear data
    if (last_activates_capture && !activates_capture) {
    if (g_audio_thread_running.load()) {
            LOG_DEBUG("[AudioCaptureManager] Switching to non-capturing provider, stopping audio system");
            StopAudioSystem();
        }
        // Clear analysis data so UI doesn't show stale values
        g_audio_data = AudioAnalysisData(config.frequency.bands);
        Listeningway::ConfigurationManager::Instance().SetAnalysisEnabled(false);
        last_activates_capture = false;
        return true;
    }
    // If switching from none to capture, start system
    if (!last_activates_capture && activates_capture) {
    LOG_DEBUG("[AudioCaptureManager] Switching to capturing provider, starting audio system");
    Listeningway::ConfigurationManager::Instance().SetAnalysisEnabled(true);
    g_audio_analyzer.Start();
        if (!StartCapture(config, g_audio_thread_running, g_audio_thread, g_audio_data)) {
            LOG_ERROR("[AudioCaptureManager] Failed to start audio capture");
            last_activates_capture = false;
            return false;
        }
        last_activates_capture = true;
        return true;
    }
    // If audio analysis is disabled in config, stop the system
    bool was_running = g_audio_thread_running.load();
    if (!config.audio.analysisEnabled) {
    if (was_running) {
            LOG_DEBUG("[AudioCaptureManager] Audio analysis disabled in config, stopping system");
            StopAudioSystem();
        }
        last_activates_capture = activates_capture;
        return true;
    }
    // If audio was not running but should be enabled, start it
    if (!was_running && config.audio.analysisEnabled) {
    LOG_DEBUG("[AudioCaptureManager] Audio analysis enabled in config, starting system");
        g_audio_analyzer.Start();
        if (!StartCapture(config, g_audio_thread_running, g_audio_thread, g_audio_data)) {
            LOG_ERROR("[AudioCaptureManager] Failed to start audio capture");
            last_activates_capture = activates_capture;
            return false;
        }
        last_activates_capture = activates_capture;
        return true;
    }
    // If already running, restart to apply new settings
    if (was_running) {
        LOG_DEBUG("[AudioCaptureManager] Restarting audio system to apply configuration changes");
        last_activates_capture = activates_capture;
        bool ok = RestartAudioSystem(config);
        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
            DebugNotes::Add(std::string("ACM.Apply: restart -> ") + (ok ? "OK" : "FAIL"));
        }
        return ok;
    }
    LOG_DEBUG("[AudioCaptureManager] Configuration applied successfully");
    last_activates_capture = activates_capture;
    return true;
}
