// ---------------------------------------------
// Listeningway v2 — addon entry point.
// Owns: config Store, AudioSystem (which owns sources, ring, pipeline,
// snapshot, DSP thread). Hooks ReShade for the per-frame uniform publish
// and the overlay draw.
// ---------------------------------------------
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <memory>

#include "audio/pipeline/audio_system.h"
#include "audio/source/off_source.h"
#include "audio/source/wasapi_loopback_source.h"
#include "audio/dsp/stages/volume_stage.h"
#include "audio/dsp/stages/fft_stage.h"
#include "audio/dsp/stages/bands_stage.h"
#include "audio/dsp/stages/log_boost_stage.h"
#include "audio/dsp/stages/equalizer_stage.h"
#include "audio/dsp/stages/band_norm_stage.h"
#include "audio/dsp/stages/spectral_centroid_stage.h"
#include "audio/dsp/stages/flux_stage.h"
#include "audio/dsp/stages/beat_stage.h"
#include "audio/dsp/stages/chronotensity_stage.h"
#include "audio/dsp/stages/pan_stage.h"
#include "audio/dsp/stages/directional_stage.h"
#include "audio/dsp/stages/loudness_stage.h"
#include "config/store.h"
#include "output/uniform_publisher.h"
#include "overlay/overlay.h"

extern "C" __declspec(dllexport) const char* NAME = "Listeningway";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Real-time audio analysis exposed to ReShade shaders as uniforms. v2.0.0-beta.";

namespace {

std::unique_ptr<lw::config::Store> g_store;
std::unique_ptr<lw::AudioSystem>   g_system;
std::chrono::steady_clock::time_point g_start_time;
HMODULE g_module_handle = nullptr;

std::filesystem::path settings_path_next_to_dll() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(g_module_handle, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path() / "Listeningway.json";
}

void on_reshade_begin_effects(reshade::api::effect_runtime* runtime,
                               reshade::api::command_list*,
                               reshade::api::resource_view,
                               reshade::api::resource_view) {
    if (!g_system) return;
    const auto snap = g_system->snapshot();
    lw::output::publish_uniforms(runtime, snap, g_start_time,
                                  std::chrono::steady_clock::now());
}

void on_overlay(reshade::api::effect_runtime* runtime) {
    if (!g_system || !g_store) return;
    lw::draw_overlay(runtime, *g_system, *g_store);
}

void compose_pipeline(lw::dsp::Pipeline& p) {
    using namespace lw::dsp;
    p.add(std::make_unique<VolumeStage>());
    p.add(std::make_unique<FftStage>());
    p.add(std::make_unique<BandsStage>());
    p.add(std::make_unique<LogBoostStage>());
    p.add(std::make_unique<EqualizerStage>());
    p.add(std::make_unique<BandNormStage>());
    p.add(std::make_unique<SpectralCentroidStage>());
    p.add(std::make_unique<FluxStage>());
    p.add(std::make_unique<BeatStage>());
    p.add(std::make_unique<ChronotensityStage>());
    p.add(std::make_unique<PanStage>());
    p.add(std::make_unique<DirectionalStage>());
    p.add(std::make_unique<LoudnessStage>());
}

bool init() {
    g_start_time = std::chrono::steady_clock::now();
    g_store = std::make_unique<lw::config::Store>(settings_path_next_to_dll());
    g_store->load();  // missing file → defaults are kept

    g_system = std::make_unique<lw::AudioSystem>(g_store.get());
    g_system->register_source(std::make_unique<lw::source::WasapiLoopbackSource>());
    g_system->register_source(std::make_unique<lw::source::OffSource>());
    compose_pipeline(g_system->pipeline());

    if (!g_system->pipeline().validate().empty()) {
        // Composition error — refuse to start; addon stays loaded but inert.
        return false;
    }
    return g_system->start();
}

void shutdown() {
    if (g_system) {
        g_system->stop();
        g_system.reset();
    }
    if (g_store) {
        g_store->save();
        g_store.reset();
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module_handle = hModule;
            DisableThreadLibraryCalls(hModule);
            if (!reshade::register_addon(hModule)) return FALSE;
            init();
            reshade::register_event<reshade::addon_event::reshade_begin_effects>(&on_reshade_begin_effects);
            reshade::register_overlay(nullptr, &on_overlay);
            break;
        case DLL_PROCESS_DETACH:
            reshade::unregister_overlay(nullptr, &on_overlay);
            reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(&on_reshade_begin_effects);
            shutdown();
            reshade::unregister_addon(hModule);
            break;
        default:
            break;
    }
    return TRUE;
}
