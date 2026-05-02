// ---------------------------------------------
// Listeningway v2 — addon entry point.
// Owns: config Store, AudioSystem (which owns sources, ring, pipeline,
// snapshot, DSP thread), ConsumerRegistry (which owns the output consumers).
// ---------------------------------------------
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include <windows.h>

#include <filesystem>
#include <memory>

#include "audio/pipeline/audio_system.h"
#include "audio/source/off_source.h"
#include "audio/source/process_audio_source.h"
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
#include "output/consumer_registry.h"
#include "output/osc_consumer.h"
#include "output/uniform_publisher_consumer.h"
#include "overlay/overlay_consumer.h"

extern "C" __declspec(dllexport) const char* NAME = "Listeningway";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Real-time audio analysis exposed to ReShade shaders as uniforms. v2.0.0-beta.";

namespace {

std::unique_ptr<lw::config::Store>             g_store;
std::unique_ptr<lw::AudioSystem>               g_system;
std::unique_ptr<lw::output::ConsumerRegistry>  g_consumers;
HMODULE g_module_handle = nullptr;

std::filesystem::path settings_path_next_to_dll() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(g_module_handle, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path() / "Listeningway.json";
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
    g_store = std::make_unique<lw::config::Store>(settings_path_next_to_dll());
    g_store->load();

    g_system = std::make_unique<lw::AudioSystem>(g_store.get());
    g_system->register_source(std::make_unique<lw::source::WasapiLoopbackSource>());
    g_system->register_source(std::make_unique<lw::source::ProcessAudioSource>());
    g_system->register_source(std::make_unique<lw::source::OffSource>());
    compose_pipeline(g_system->pipeline());

    if (!g_system->pipeline().validate().empty()) {
        return false;
    }
    if (!g_system->start()) {
        return false;
    }

    // Build the consumer registry. Internal consumers first (always-on);
    // toggleable network consumers will be appended in subsequent phases.
    g_consumers = std::make_unique<lw::output::ConsumerRegistry>();
    g_consumers->add(std::make_unique<lw::output::UniformPublisherConsumer>());
    g_consumers->add(std::make_unique<lw::overlay::OverlayConsumer>(*g_store, *g_consumers));
    g_consumers->add(std::make_unique<lw::output::OscConsumer>(*g_store));

    g_consumers->start_all(*g_system, g_module_handle, g_store->snapshot());
    return true;
}

void shutdown() {
    if (g_consumers) {
        g_consumers->stop_all();
        g_consumers.reset();
    }
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
            break;
        case DLL_PROCESS_DETACH:
            shutdown();
            reshade::unregister_addon(hModule);
            break;
        default:
            break;
    }
    return TRUE;
}
