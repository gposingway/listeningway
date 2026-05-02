// ---------------------------------------------
// Listeningway v2 — addon entry point
//
// Day 0 stub: registers as a no-op ReShade addon. Subsequent days populate
// this with the AudioSystem lifecycle (Source -> Ring -> Pipeline ->
// Snapshot -> Consumers) per ADR-0002.
// ---------------------------------------------
#define ImTextureID ImU64
#include <reshade.hpp>
#include <windows.h>

extern "C" __declspec(dllexport) const char* NAME = "Listeningway";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Real-time audio analysis exposed to ReShade shaders as uniforms. v2.0.0-beta (greenfield).";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            if (!reshade::register_addon(hModule)) {
                return FALSE;
            }
            break;
        case DLL_PROCESS_DETACH:
            reshade::unregister_addon(hModule);
            break;
        default:
            break;
    }
    return TRUE;
}
