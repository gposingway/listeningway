#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "audio/analysis/audio_analysis.h"
#include "audio/audio_pipeline.h"
#include "constants.h"
#include "configuration/configuration_manager.h"
#include "logging.h"
#include "overlay.h"
#include "settings.h"
#include "uniform_manager.h"

using Listeningway::ConfigurationManager;

static std::atomic_bool g_addon_enabled{false};
static UniformManager g_uniform_manager;
static const std::chrono::steady_clock::time_point g_start_time = std::chrono::steady_clock::now();

static void UpdateShaderUniforms(reshade::api::effect_runtime* runtime) {
    if (!Listeningway::g_pipeline) return;

    AudioAnalysisData snap = Listeningway::g_pipeline->Snapshot();
    const auto config = ConfigurationManager::Snapshot();

    const float ampVol = config.frequency.amplifierVolume;
    const float ampBands = config.frequency.amplifierBands;
    const float ampDir = config.frequency.amplifierDirection;

    snap.volume *= ampVol;
    for (auto& v : snap.freq_bands) v *= ampBands;
    snap.volume_left *= ampVol;
    snap.volume_right *= ampVol;
    float dir8_local[8];
    for (int i = 0; i < 8; ++i) dir8_local[i] = snap.direction8[i] * ampDir;

    const auto now = std::chrono::steady_clock::now();
    const float time_seconds = std::chrono::duration<float>(now - g_start_time).count();
    const float phase_60hz = std::fmod(time_seconds * 60.0f, 1.0f);
    const float phase_120hz = std::fmod(time_seconds * 120.0f, 1.0f);
    const float total_phases_60hz = time_seconds * 60.0f;
    const float total_phases_120hz = time_seconds * 120.0f;

    g_uniform_manager.update_uniforms(runtime, snap.volume, snap.freq_bands, snap.beat,
        time_seconds, phase_60hz, phase_120hz, total_phases_60hz, total_phases_120hz,
        snap.volume_left, snap.volume_right, snap.audio_pan, snap.audio_format, dir8_local, 8);
}

static void OnReloadedEffects(reshade::api::effect_runtime*) {
    // Uniform handles are not cached; nothing to do here.
}

static void OverlayCallback(reshade::api::effect_runtime*) {
    try {
        if (Listeningway::g_pipeline) {
            Listeningway::g_pipeline->MaybeRestartIfStale();
            const auto data = Listeningway::g_pipeline->Snapshot();
            DrawListeningwayDebugOverlay(data);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Overlay] Exception: ") + ex.what());
    } catch (...) {
        LOG_ERROR("[Overlay] Unknown exception.");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(lpReserved);
    try {
        switch (ul_reason_for_call) {
            case DLL_PROCESS_ATTACH: {
                LOG_DEBUG("[Addon] DLL_PROCESS_ATTACH");
                ConfigurationManager::Instance(); // load config
                LoadAllTunables();
                DisableThreadLibraryCalls(hModule);
                if (reshade::register_addon(hModule)) {
                    OpenLogFile("listeningway.log");
                    LOG_DEBUG("Addon loaded and log file opened.");
                    reshade::register_overlay(nullptr, &OverlayCallback);
                    reshade::register_event<reshade::addon_event::reshade_begin_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_begin_effects>::decl)UpdateShaderUniforms);
                    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_reloaded_effects>::decl)OnReloadedEffects);

                    Listeningway::g_pipeline = std::make_unique<Listeningway::AudioPipeline>(g_audio_analyzer);
                    Listeningway::g_pipeline->Start();
                    g_addon_enabled = true;
                }
                break;
            }
            case DLL_PROCESS_DETACH: {
                LOG_DEBUG("[Addon] DLL_PROCESS_DETACH");
                if (g_addon_enabled.load()) {
                    reshade::unregister_overlay(nullptr, &OverlayCallback);
                    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_begin_effects>::decl)UpdateShaderUniforms);
                    reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(
                        (reshade::addon_event_traits<reshade::addon_event::reshade_reloaded_effects>::decl)OnReloadedEffects);

                    if (Listeningway::g_pipeline) {
                        Listeningway::g_pipeline->Stop();
                        Listeningway::g_pipeline.reset();
                    }
                    CloseLogFile();
                    g_addon_enabled = false;
                }
                break;
            }
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
 * Real-time audio analysis exposed to ReShade shaders as uniforms. The flow
 * is: Audio Capture (thread, in AudioPipeline) -> Analysis -> Snapshot ->
 * Uniform update / Overlay rendering.
 */

extern "C" bool SwitchAudioProvider(int providerType, int /*timeout_ms*/ = 2000) {
    if (!Listeningway::g_pipeline) return false;
    return Listeningway::g_pipeline->SwitchProvider(providerType);
}
