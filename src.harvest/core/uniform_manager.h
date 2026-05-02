#pragma once
#include <vector>
#include <string>
#include <reshade.hpp>

// Manages ReShade uniform updates for Listeningway audio data
class UniformManager {
public:
    // Updates all Listeningway_* uniforms with audio and time data
    void update_uniforms(reshade::api::effect_runtime* runtime, float volume, const std::vector<float>& freq_bands, float beat,
                        float time_seconds, float phase_60hz, float phase_120hz, float total_phases_60hz, float total_phases_120hz,
                        float volume_left = 0.0f, float volume_right = 0.0f, float audio_pan = 0.0f, float audio_format = 0.0f,
                        const float* dir8 = nullptr, uint32_t dir8_count = 0);
};
