// ---------------------------------------------
// @file uniform_manager.cpp
// @brief Uniform management implementation for Listeningway ReShade addon
// ---------------------------------------------
#include "uniform_manager.h"
#include <string_view>
#include <algorithm>
#include "constants.h" // DEFAULT_NUM_BANDS

void UniformManager::update_uniforms(reshade::api::effect_runtime* runtime, float volume, const std::vector<float>& freq_bands, float beat,
    float time_seconds, float phase_60hz, float phase_120hz, float total_phases_60hz, float total_phases_120hz,
    float volume_left, float volume_right, float audio_pan, float audio_format,
    const float* dir8, uint32_t dir8_count) {
    // Clamp the effective band count to DEFAULT_NUM_BANDS — which is also the
    // compile-time size of the shader-side Listeningway_FreqBands[] array
    // (build.bat substitutes this constant into ListeningwayUniforms.fxh). If
    // a user misconfigures num_bands above this, we truncate rather than
    // overflow the shader uniform.
    const size_t effective_bands = std::min(freq_bands.size(), DEFAULT_NUM_BANDS);
    // Only update uniforms with the correct annotation (source = ...)
    runtime->enumerate_uniform_variables(nullptr, [&](reshade::api::effect_runtime*, reshade::api::effect_uniform_variable var_handle) {
        char source[64] = "";
        if (runtime->get_annotation_string_from_uniform_variable(var_handle, "source", source)) {
            if (strcmp(source, "listeningway_volume") == 0) {
                runtime->set_uniform_value_float(var_handle, &volume, 1);
            } else if (strcmp(source, "listeningway_freqbands") == 0) {
                if (effective_bands > 0) {
                    runtime->set_uniform_value_float(var_handle, freq_bands.data(), static_cast<uint32_t>(effective_bands));
                }
            } else if (strcmp(source, "listeningway_numbands") == 0) {
                // Expose the configured band count so shaders can size their
                // traversal against the live value instead of guessing against
                // the fallback array length. Written as a float for uniform
                // type compatibility; shaders cast to int where needed.
                float nb = static_cast<float>(effective_bands);
                runtime->set_uniform_value_float(var_handle, &nb, 1);
            } else if (strcmp(source, "listeningway_beat") == 0) {
                runtime->set_uniform_value_float(var_handle, &beat, 1);
            } else if (strcmp(source, "listeningway_timeseconds") == 0) {
                runtime->set_uniform_value_float(var_handle, &time_seconds, 1);
            } else if (strcmp(source, "listeningway_timephase60hz") == 0) {
                runtime->set_uniform_value_float(var_handle, &phase_60hz, 1);
            } else if (strcmp(source, "listeningway_timephase120hz") == 0) {
                runtime->set_uniform_value_float(var_handle, &phase_120hz, 1);
            } else if (strcmp(source, "listeningway_totalphases60hz") == 0) {
                runtime->set_uniform_value_float(var_handle, &total_phases_60hz, 1);
            } else if (strcmp(source, "listeningway_totalphases120hz") == 0) {
                runtime->set_uniform_value_float(var_handle, &total_phases_120hz, 1);
            } else if (strcmp(source, "listeningway_volumeleft") == 0) {
                runtime->set_uniform_value_float(var_handle, &volume_left, 1); 
            } else if (strcmp(source, "listeningway_volumeright") == 0) {
                runtime->set_uniform_value_float(var_handle, &volume_right, 1);
            } else if (strcmp(source, "listeningway_audiopan") == 0) {
                runtime->set_uniform_value_float(var_handle, &audio_pan, 1);
            } else if (strcmp(source, "listeningway_audioformat") == 0) {
                runtime->set_uniform_value_float(var_handle, &audio_format, 1);
            } else if (strcmp(source, "listeningway_direction8") == 0) {
                if (dir8 && dir8_count >= 8) {
                    runtime->set_uniform_value_float(var_handle, dir8, 8);
                } else {
                    float zeros[8] = {0,0,0,0,0,0,0,0};
                    runtime->set_uniform_value_float(var_handle, zeros, 8);
                }
            } else if (strcmp(source, "listeningway_front") == 0) {
                float v = (dir8 && dir8_count >= 1) ? dir8[0] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_front_right") == 0) {
                float v = (dir8 && dir8_count >= 2) ? dir8[1] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_right") == 0) {
                float v = (dir8 && dir8_count >= 3) ? dir8[2] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_back_right") == 0) {
                float v = (dir8 && dir8_count >= 4) ? dir8[3] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_back") == 0) {
                float v = (dir8 && dir8_count >= 5) ? dir8[4] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_back_left") == 0) {
                float v = (dir8 && dir8_count >= 6) ? dir8[5] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_left") == 0) {
                float v = (dir8 && dir8_count >= 7) ? dir8[6] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            } else if (strcmp(source, "listeningway_front_left") == 0) {
                float v = (dir8 && dir8_count >= 8) ? dir8[7] : 0.0f;
                runtime->set_uniform_value_float(var_handle, &v, 1);
            }
        }
    });
}
