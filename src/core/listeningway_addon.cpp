// ---------------------------------------------
// @file listeningway_addon.cpp
// @brief Main entry point and DLL logic for Listeningway ReShade Addon
// ---------------------------------------------
#include <algorithm>
#include <unordered_map>
#include <string_view>
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "audio_capture.h"
#include "audio_analysis.h"
#include "overlay.h"
#include "logging.h"
#include "uniform_manager.h"
#include "constants.h"
#include "settings.h"

static std::atomic_bool g_addon_enabled = false;
static std::atomic_bool g_audio_thread_running = false;
static std::atomic_bool g_audio_analysis_enabled = true; // Toggle for audio analysis
static std::thread g_audio_thread;
static std::mutex g_audio_data_mutex;
static AudioAnalysisData g_audio_data;
static AudioAnalysisConfig g_audio_config(g_settings);
static UniformManager g_uniform_manager;
static std::chrono::steady_clock::time_point g_last_audio_update = std::chrono::steady_clock::now();
static float g_last_volume = 0.0f;
static std::chrono::steady_clock::time_point g_start_time = std::chrono::steady_clock::now();

/**
 * @brief Updates all Listeningway_* uniforms in all loaded effects.
 * @param runtime The ReShade effect runtime.
 */
static void UpdateShaderUniforms(reshade::api::effect_runtime* runtime) {
    float volume_to_set;
    std::vector<float> freq_bands_to_set;
    float beat_to_set;
    {
        std::lock_guard<std::mutex> lock(g_audio_data_mutex);
        volume_to_set = g_audio_data.volume;
        freq_bands_to_set = g_audio_data.freq_bands;
        beat_to_set = g_audio_data.beat;
    }
    // Time/phase calculations
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - g_start_time;
    float time_seconds = elapsed.count();
    float phase_60hz = std::fmod(time_seconds * 60.0f, 1.0f);
    float phase_120hz = std::fmod(time_seconds * 120.0f, 1.0f);
    float total_phases_60hz = time_seconds * 60.0f;
    float total_phases_120hz = time_seconds * 120.0f;
    g_uniform_manager.update_uniforms(runtime, volume_to_set, freq_bands_to_set, beat_to_set,
        time_seconds, phase_60hz, phase_120hz, total_phases_60hz, total_phases_120hz);
}

/**
 * @brief Caches all Listeningway_* uniforms on effect reload.
 * @param runtime The ReShade effect runtime.
 */
static void OnReloadedEffects(reshade::api::effect_runtime* runtime) {
    // No longer needed: uniform handles are not cached
}

/**
 * @brief Checks if new audio values have been captured in the last 3 seconds.
 * If not, attempts to restart the audio capture thread.
 */
static void MaybeRestartAudioCaptureIfStale() {
    float current_volume;
    {
        std::lock_guard<std::mutex> lock(g_audio_data_mutex);
        current_volume = g_audio_data.volume;
    }
    auto now = std::chrono::steady_clock::now();
    if (current_volume != g_last_volume) {
        g_last_audio_update = now;
        g_last_volume = current_volume;
    } else if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_audio_update).count() > static_cast<int>(g_settings.capture_stale_timeout * 1000.0f)) {
        // No new audio for the configured timeout, try to restart
        LOG_DEBUG("[Addon] Audio capture thread stale, attempting restart.");
        CheckAndRestartAudioCapture(g_audio_config, g_audio_thread_running, g_audio_thread, g_audio_data_mutex, g_audio_data);
        LOG_DEBUG("[Addon] Audio capture thread restarted.");
        g_last_audio_update = now; // Prevent rapid restarts
    }
}

/**
 * @brief Overlay callback for ReShade. Draws the debug overlay.
 * @param runtime The ReShade effect runtime.
 */
static void OverlayCallback(reshade::api::effect_runtime*) {
    try {
        MaybeRestartAudioCaptureIfStale();
        DrawListeningwayDebugOverlay(g_audio_data, g_audio_data_mutex);
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Overlay] Exception: ") + ex.what());
    } catch (...) {
        LOG_ERROR("[Overlay] Unknown exception.");
    }
}

/**
 * @brief DLL entry point and addon registration for Listeningway.
 * Handles ReShade addon lifecycle and event registration.
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(lpReserved);
    try {
        switch (ul_reason_for_call) {
            case DLL_PROCESS_ATTACH:
                LOG_DEBUG("[Addon] DLL_PROCESS_ATTACH: Startup sequence initiated.");
                LoadSettings();
                LOG_DEBUG("[Addon] Loaded settings from ini.");
                LoadAllTunables();
                LOG_DEBUG("[Addon] Loaded all tunables from ini.");
                DisableThreadLibraryCalls(hModule);
                InitAudioDeviceNotification();
                LOG_DEBUG("[Addon] Device notification initialized.");
                if (reshade::register_addon(hModule)) {
                    OpenLogFile("listeningway.log");
                    LOG_DEBUG("Addon loaded and log file opened.");
                    reshade::register_overlay(nullptr, &OverlayCallback);
                    reshade::register_event<reshade::addon_event::reshade_begin_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_begin_effects>::decl)UpdateShaderUniforms);
                    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_reloaded_effects>::decl)OnReloadedEffects);
                    
                    // Initialize and start the audio analyzer with the configured algorithm
                    g_audio_analyzer.SetBeatDetectionAlgorithm(g_settings.beat_detection_algorithm);
                    g_audio_analyzer.Start();
                    LOG_DEBUG("[Addon] Audio analyzer started with algorithm: " + 
                             std::to_string(g_settings.beat_detection_algorithm));
                    
                    // Start the audio capture thread
                    StartAudioCaptureThread(g_audio_config, g_audio_thread_running, g_audio_thread, g_audio_data_mutex, g_audio_data);
                    LOG_DEBUG("[Addon] Audio capture thread started.");
                    g_addon_enabled = true;
                }
                break;
            case DLL_PROCESS_DETACH:
                LOG_DEBUG("[Addon] DLL_PROCESS_DETACH: Shutdown sequence initiated.");
                if (g_addon_enabled.load()) {
                    reshade::unregister_overlay(nullptr, &OverlayCallback);
                    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_begin_effects>::decl)UpdateShaderUniforms);
                    reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_reloaded_effects>::decl)OnReloadedEffects);
                    
                    // Stop the audio analyzer
                    g_audio_analyzer.Stop();
                    LOG_DEBUG("[Addon] Audio analyzer stopped.");
                    
                    // Stop the audio capture thread
                    StopAudioCaptureThread(g_audio_thread_running, g_audio_thread);
                    LOG_DEBUG("[Addon] Audio capture thread stopped.");
                    CloseLogFile();
                    g_addon_enabled = false;
                }
                UninitAudioDeviceNotification();
                LOG_DEBUG("[Addon] Device notification uninitialized.");
                break;
        }
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Addon] Exception in DllMain: ") + ex.what());
    } catch (...) {
        LOG_ERROR("[Addon] Unknown exception in DllMain.");
    }
    return TRUE;
}

/**
 * @mainpage Listeningway ReShade Addon
 *
 * @section intro_sec Introduction
 * Listeningway is a modular audio analysis and visualization addon for ReShade. It captures system audio, analyzes it in real time, and exposes the results to shaders via uniforms.
 *
 * @section extend_sec How to Extend
 * - Add new analysis features in audio_analysis.*
 * - Add new uniforms in uniform_manager.* and update logic
 * - Add new overlay elements in overlay.*
 * - Use logging for debugging and diagnostics
 *
 * The flow is: Audio Capture (thread) -> Analysis -> Uniform Update -> Shader/Overlay
 */

