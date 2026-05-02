#pragma once
#include <string>
#include <vector>

#include "audio/capture/providers/audio_capture_provider.h"

// Read-only facade for the audio capture subsystem. The lifecycle (start/stop,
// provider switching, restart) is owned by Listeningway::AudioPipeline; these
// helpers simply expose provider metadata to UI / configuration code.

/// Returns metadata for every registered, available capture provider.
std::vector<AudioProviderInfo> GetAvailableAudioCaptureProviders();

/// Returns the human-readable name of the provider with the given code,
/// or "Unknown" if no such provider is registered.
std::string GetAudioCaptureProviderName(const std::string& providerCode);
