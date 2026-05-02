// Match overlay include pattern for ImGui/ReShade integration
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include "beat_settings_panel.h"
#include "audio/analysis/beat_profiles.h"
#include "audio/analysis/audio_analysis.h"
#include "core/constants.h"
#include "utils/logging.h"

using Listeningway::ConfigurationManager;

static int g_selected_profile = static_cast<int>(BeatProfile::GeneralMusic);

bool DrawBeatSettingsPanel(const AudioAnalysisData& data, ConfigurationManager& cfgMgr) {
    bool changed = false;
    auto& config = cfgMgr.GetConfig();

    // Sync selected profile from config on first run per session
    static bool initialized = false;
    if (!initialized) {
        BeatProfile loaded;
        if (FromString(config.beat.profile, loaded)) {
            g_selected_profile = static_cast<int>(loaded);
        }
        initialized = true;
    }

    ImGui::Text("Beat Settings");

    // Profile dropdown
    const char* profile_names[] = { "General Music", "Electronic / EDM", "Acoustic / Organic", "Custom" };
    if (ImGui::Combo("Profile", &g_selected_profile, profile_names, IM_ARRAYSIZE(profile_names))) {
        BeatProfile profile = static_cast<BeatProfile>(g_selected_profile);
        if (profile != BeatProfile::Custom) {
            ApplyBeatProfile(profile, config);
            // Ensure analyzer uses the profile's algorithm
            extern AudioAnalyzer g_audio_analyzer;
            g_audio_analyzer.SetBeatDetectionAlgorithm(config.beat.algorithm);
        }
        // Persist profile name
        config.beat.profile = ToString(profile);
        changed = true;
    }

    // Reset button to reapply current profile defaults (when a non-Custom profile is selected)
    if (static_cast<BeatProfile>(g_selected_profile) != BeatProfile::Custom) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to profile defaults")) {
            BeatProfile profile = static_cast<BeatProfile>(g_selected_profile);
            ApplyBeatProfile(profile, config);
            // Re-sync analyzer in case algorithm is part of the profile
            extern AudioAnalyzer g_audio_analyzer;
            g_audio_analyzer.SetBeatDetectionAlgorithm(config.beat.algorithm);
            // Persist profile name (idempotent)
            config.beat.profile = ToString(profile);
            changed = true;
        }
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Reapply the preset values for the selected profile, discarding custom tweaks.");
        }
    }

    // Algorithm selection (visible in Custom and for transparency)
    const char* algorithms[] = { "Simple Energy (Original)", "Spectral Flux + Autocorrelation (Advanced)" };
    int algorithm = config.beat.algorithm;
    if (ImGui::Combo("Algorithm", &algorithm, algorithms, IM_ARRAYSIZE(algorithms))) {
        config.beat.algorithm = algorithm;
        extern AudioAnalyzer g_audio_analyzer;
        g_audio_analyzer.SetBeatDetectionAlgorithm(algorithm);
        // If user changed algorithm, mark as Custom implicitly
        g_selected_profile = static_cast<int>(BeatProfile::Custom);
        config.beat.profile = ToString(BeatProfile::Custom);
        changed = true;
    }

    // Custom controls
    if (config.beat.algorithm == static_cast<int>(BeatDetectionAlgorithm::SpectralFluxAuto)) {
        if (ImGui::CollapsingHeader("Advanced Algorithm Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            float spectral_flux_threshold = config.beat.spectralFluxThreshold;
            if (ImGui::SliderFloat("Spectral Flux Threshold", &spectral_flux_threshold, 0.01f, 0.2f, "%.3f")) {
                config.beat.spectralFluxThreshold = spectral_flux_threshold; changed = true;
                g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
            }
            float tempo_change_threshold = config.beat.tempoChangeThreshold;
            if (ImGui::SliderFloat("Tempo Change Threshold", &tempo_change_threshold, 0.1f, 0.5f, "%.2f")) {
                config.beat.tempoChangeThreshold = tempo_change_threshold; changed = true;
                g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
            }
            float beat_induction_window = config.beat.beatInductionWindow;
            if (ImGui::SliderFloat("Beat Induction Window", &beat_induction_window, 0.05f, 0.2f, "%.2f")) {
                config.beat.beatInductionWindow = beat_induction_window; changed = true;
                g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
            }
            float octave_error_weight = config.beat.octaveErrorWeight;
            if (ImGui::SliderFloat("Octave Error Weight", &octave_error_weight, 0.5f, 0.9f, "%.2f")) {
                config.beat.octaveErrorWeight = octave_error_weight; changed = true;
                g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
            }
        }
        // Decay for SpectralFluxAuto
        float spectral_flux_decay_multiplier = config.beat.spectralFluxDecayMultiplier;
        if (ImGui::SliderFloat("Decay Multiplier", &spectral_flux_decay_multiplier, 0.5f, 5.0f, "%.2f")) {
            config.beat.spectralFluxDecayMultiplier = spectral_flux_decay_multiplier; changed = true;
            g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
        }
    } else {
        // SimpleEnergy decay settings
        ImGui::Text("Beat Decay Settings");
        float beat_falloff_default = config.beat.falloffDefault;
        if (ImGui::SliderFloat("Default Falloff Rate", &beat_falloff_default, 0.5f, 5.0f, "%.2f")) {
            config.beat.falloffDefault = beat_falloff_default; changed = true;
            g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
        }
        if (ImGui::CollapsingHeader("Adaptive Falloff Settings")) {
            float beat_time_scale = config.beat.timeScale;
            if (ImGui::SliderFloat("Time Scale", &beat_time_scale, 1e-10f, 1e-8f, "%.2e")) { config.beat.timeScale = beat_time_scale; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom); }
            float beat_time_initial = config.beat.timeInitial;
            if (ImGui::SliderFloat("Initial Time", &beat_time_initial, 0.1f, 1.0f, "%.2f")) { config.beat.timeInitial = beat_time_initial; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom); }
            float beat_time_min = config.beat.timeMin;
            if (ImGui::SliderFloat("Min Time", &beat_time_min, 0.01f, 0.5f, "%.2f")) { config.beat.timeMin = beat_time_min; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom); }
            float beat_time_divisor = config.beat.timeDivisor;
            if (ImGui::SliderFloat("Time Divisor", &beat_time_divisor, 0.01f, 1.0f, "%.2f")) { config.beat.timeDivisor = beat_time_divisor; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom); }
        }
    }

    // Band-limited Beat Detection Settings (common)
    ImGui::Separator();
    ImGui::Text("Band-Limited Beat Detection");
    float beat_min_freq = config.beat.minFreq;
    if (ImGui::SliderFloat("Beat Min Freq (Hz)", &beat_min_freq, OVERLAY_BEAT_MIN_FREQ_MIN, OVERLAY_BEAT_MIN_FREQ_MAX, "%.1f")) {
        config.beat.minFreq = beat_min_freq; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
    }
    float beat_max_freq = config.beat.maxFreq;
    if (ImGui::SliderFloat("Beat Max Freq (Hz)", &beat_max_freq, OVERLAY_BEAT_MAX_FREQ_MIN, OVERLAY_BEAT_MAX_FREQ_MAX, "%.1f")) {
        config.beat.maxFreq = beat_max_freq; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
    }
    float flux_low_alpha = config.beat.fluxLowAlpha;
    if (ImGui::SliderFloat("Low Flux Smoothing", &flux_low_alpha, OVERLAY_FLUX_SMOOTH_MIN, OVERLAY_FLUX_SMOOTH_MAX, "%.3f")) {
        config.beat.fluxLowAlpha = flux_low_alpha; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
    }
    float flux_low_threshold_multiplier = config.beat.fluxLowThresholdMultiplier;
    if (ImGui::SliderFloat("Low Flux Threshold", &flux_low_threshold_multiplier, OVERLAY_FLUX_THRESH_MIN, OVERLAY_FLUX_THRESH_MAX, "%.2f")) {
        config.beat.fluxLowThresholdMultiplier = flux_low_threshold_multiplier; changed = true; g_selected_profile = static_cast<int>(BeatProfile::Custom); config.beat.profile = ToString(BeatProfile::Custom);
    }

    if (changed) {
        config.Validate();
    }

    return changed;
}
