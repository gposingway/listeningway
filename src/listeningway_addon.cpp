// ---------------------------------------------
// Listeningway v2 — addon entry point.
//
// CRITICAL: DllMain runs under the Windows loader lock. This file does
// nothing that could deadlock the loader: no thread spawning, no
// WSAStartup, no COM init, no file I/O. All heavy work is deferred to
// the first ReShade frame via the boot scheduler, which calls App::start()
// from the render thread once the loader lock is released.
//
// The contents of "start" live in src/app.cpp. This file knows about
// ReShade event lifecycle and the boot scheduler — nothing else.
// ---------------------------------------------
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include <windows.h>

#include <memory>

#include "app.h"
#include "boot/scheduler.h"

extern "C" __declspec(dllexport) const char* NAME = "Listeningway";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Real-time audio analysis exposed to ReShade shaders as uniforms.";

namespace {

std::unique_ptr<lw::App>             g_app;
std::unique_ptr<lw::boot::Scheduler> g_boot;
HMODULE                              g_module = nullptr;

void on_frame_tick(reshade::api::effect_runtime*,
                    reshade::api::command_list*,
                    reshade::api::resource_view,
                    reshade::api::resource_view) {
    // Boot phase: drive the scheduler one step per frame.
    if (g_boot) {
        g_boot->tick();
        if (g_boot->empty() || g_boot->failed()) {
            g_boot.reset();
        }
        return;
    }
    // Post-boot: routine maintenance (consumer self-disarm polling, etc.).
    if (g_app) g_app->tick_running();
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = hModule;
            DisableThreadLibraryCalls(hModule);
            if (!reshade::register_addon(hModule)) return FALSE;

            // Construct the App but do not start it here — the scheduler
            // runs start() on the render thread once the loader lock is
            // released.
            g_app = std::make_unique<lw::App>(hModule);

            g_boot = std::make_unique<lw::boot::Scheduler>();
            g_boot->enqueue("start", [] {
                return g_app ? g_app->tick_boot()
                             : lw::boot::StepResult::Failed;
            });

            reshade::register_event<reshade::addon_event::reshade_begin_effects>(
                &on_frame_tick);
            break;

        case DLL_PROCESS_DETACH:
            reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(
                &on_frame_tick);
            if (g_app) g_app->stop();   // idempotent; partial-init-safe
            g_app.reset();
            g_boot.reset();
            reshade::unregister_addon(hModule);
            break;

        default:
            break;
    }
    return TRUE;
}
