// ---------------------------------------------
// Read-only facade for the audio capture subsystem. Routes through the
// AudioPipeline-owned AudioCaptureManager so callers don't need to know
// about either the pipeline or the manager directly.
// ---------------------------------------------
#include "audio_capture.h"
#include "audio/audio_pipeline.h"

namespace {

AudioCaptureManager* manager() {
    return Listeningway::g_pipeline ? Listeningway::g_pipeline->CaptureManager() : nullptr;
}

} // namespace

std::vector<AudioProviderInfo> GetAvailableAudioCaptureProviders() {
    if (auto* m = manager()) return m->GetAvailableProviderInfos();
    return {};
}

std::string GetAudioCaptureProviderName(const std::string& providerCode) {
    if (auto* m = manager()) {
        for (const auto& info : m->GetAvailableProviderInfos()) {
            if (info.code == providerCode) return info.name;
        }
    }
    return "Unknown";
}
