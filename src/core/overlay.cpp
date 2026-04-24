// ---------------------------------------------
// Overlay Module Implementation
// Provides a debug ImGui overlay for real-time audio analysis data
// ---------------------------------------------
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include "overlay.h"
#include "audio/analysis/audio_analysis.h"
#include "constants.h"
#include "audio_format_utils.h"
#include "settings.h"
#include "logging.h"
#include "thread_safety_manager.h"
#include "audio/capture/audio_capture.h"
#include "configuration/configuration_manager.h"
#include "beat_settings_panel.h"
#include "utils/debug_notes.h"
using Listeningway::ConfigurationManager;
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm> // For std::clamp

extern std::atomic_bool g_audio_analysis_enabled;
extern bool g_listeningway_debug_enabled;

// External declarations for global variables used in overlay
extern std::atomic_bool g_switching_provider;
extern std::mutex g_provider_switch_mutex;

// Declare SwitchAudioProvider for use in overlay.cpp
extern "C" bool SwitchAudioProvider(int providerType, int timeout_ms = 2000);

// Static reference to avoid repeated Instance() calls - safe since ConfigurationManager is a singleton
static auto& g_configManager = ConfigurationManager::Instance();

// Overlay/ImGui color constants (must be defined after ImGui headers)
constexpr ImU32 OVERLAY_BAR_COLOR_BG = IM_COL32(40, 40, 40, 128);           // Dark background for bars
constexpr ImU32 OVERLAY_BAR_COLOR_OUTLINE = IM_COL32(60, 60, 60, 128);      // Outline for frequency bars
constexpr ImU32 OVERLAY_BAR_COLOR_CENTER_MARKER = IM_COL32(255, 255, 255, 180); // Center marker (white, semi-transparent)

// Helper: Draw toggles (audio analysis, debug logging)
static void DrawToggles() {
    auto& config = g_configManager.GetConfig();
    
    // Audio Provider Selection Dropdown
    std::vector<AudioProviderInfo> available_providers = GetAvailableAudioCaptureProviders();
    std::vector<const char*> provider_names;
    for (const auto& info : available_providers) {
        provider_names.push_back(info.name.c_str());
    }

    // Find current selection index by code
    int display_selection_index = 0;
    std::string current_code = config.audio.captureProviderCode;
    if (!available_providers.empty()) {
        bool found = false;
        for (size_t i = 0; i < available_providers.size(); ++i) {
            if (available_providers[i].code == current_code) {
                display_selection_index = static_cast<int>(i);
                found = true;
                break;
            }
        }
        if (!found) {
            // Fallback to the first available provider
            display_selection_index = 0;
            config.audio.captureProviderCode = available_providers[0].code;
        }
    }

    const char* combo_label = provider_names.empty() ? "(no providers)" : provider_names[display_selection_index];
    if (ImGui::BeginCombo("Audio Analysis", combo_label)) {
        int previous_selection = display_selection_index;
        for (int i = 0; i < provider_names.size(); ++i) {
            const bool is_selected = (display_selection_index == i);
            if (ImGui::Selectable(provider_names[i], is_selected, 0)) {
                if (previous_selection != i) {
                    const auto& selected_info = available_providers[i];
                    config.audio.captureProviderCode = selected_info.code;
                    
                    // Map provider code to provider type for existing SwitchAudioProvider function
                    int provider_type = -1; // Default to "None/Off"
                    if (selected_info.code == "system") {
                        provider_type = 0; // SYSTEM_AUDIO
                    } else if (selected_info.code == "game") {
                        provider_type = 1; // PROCESS_AUDIO
                    }
                    // "off" code stays as -1 for None
                    
                    // Switch the provider using the existing robust function
                    bool switch_ok = SwitchAudioProvider(provider_type, 2000);
                    
                    if (switch_ok) {
                        g_configManager.Save();
                        LOG_DEBUG(std::string("[Overlay] Audio Provider changed to: ") + selected_info.name + 
                                 " (code: " + selected_info.code + ", type: " + std::to_string(provider_type) + ")");
                    } else {
                        LOG_ERROR(std::string("[Overlay] Failed to switch to provider: ") + selected_info.name);
                    }
                }
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Use the global debug flag directly, then synchronize with configManager through SetDebugEnabled
    bool debug_enabled = g_listeningway_debug_enabled;
    if (ImGui::Checkbox("Enable Debug Logging", &debug_enabled)) {
        SetDebugEnabled(debug_enabled);
        LOG_DEBUG(std::string("[Overlay] Debug Logging toggled ") + (debug_enabled ? "ON" : "OFF"));
    }

    // SIMD toggle
    bool simd_enabled = config.audio.simdEnabled;
    if (ImGui::Checkbox("Enable SIMD (SSE/AVX)", &simd_enabled)) {
    config.audio.simdEnabled = simd_enabled;
        ConfigurationManager::Instance().ApplyConfigToLiveSystems();
    g_configManager.Save();
        LOG_DEBUG(std::string("[Overlay] SIMD toggled ") + (simd_enabled ? "ON" : "OFF"));
    }
}

// Helper: unified amplifier slider styling (distinct from regular controls)
// Returns true if the value changed
static bool DrawAmplifierSlider(const char* id, float& value) {
    // Accent color (consistent across all amplifiers) and red "Spinal Tap" override when > 10
    const ImVec4 accent = ImVec4(0.47f, 0.74f, 1.00f, 1.00f); // light teal/blue
    const ImVec4 red    = ImVec4(1.00f, 0.10f, 0.10f, 1.00f);
    float t = 0.0f;
    if (value > 10.0f) {
        t = std::clamp((value - 10.0f) / 1.0f, 0.0f, 1.0f);
    }
    ImVec4 grab = ImVec4(
        accent.x * (1.0f - t) + red.x * t,
        accent.y * (1.0f - t) + red.y * t,
        accent.z * (1.0f - t) + red.z * t,
        accent.w * (1.0f - t) + red.w * t
    );
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, grab);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, grab);
    bool changed = ImGui::SliderFloat(id, &value, OVERLAY_AMPLIFIER_MIN, OVERLAY_AMPLIFIER_MAX, "%.2f");
    ImGui::PopStyleColor(2);
    return changed;
}

// Helper: Draw log file info
static void DrawLogInfo() {
    if (g_listeningway_debug_enabled) {
        ImGui::Text("Log file: ");
        ImGui::SameLine();
        std::string logPath = GetLogFilePath();
        if (ImGui::Selectable(logPath.c_str())) {
            ShellExecuteA(nullptr, "open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::Text("(Click to open log file)");
        // Debug notes copy/paste area
        ImGui::Text("Debug Notes (copy/paste):");
        std::string notes = DebugNotes::GetAll();
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll;
        ImGui::InputTextMultiline("##DebugNotes", (char*)notes.c_str(), notes.size()+1, ImVec2(-1, 120), flags);
        if (ImGui::Button("Copy Notes")) {
            ImGui::SetClipboardText(notes.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Notes")) {
            DebugNotes::Clear();
        }
    }
}

// Helper: Draw website link
static void DrawWebsite() {
    ImGui::Text("Website:");
    ImGui::SameLine();
    if (ImGui::Selectable("https://github.com/gposingway/Listeningway")) {
        ShellExecuteA(nullptr, "open", "https://github.com/gposingway/Listeningway", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
}

// (Removed old standalone DrawVolume/DrawBeat helpers; visuals now live in grouped sections)

// Helper: Draw frequency bands with view mode
static void DrawFrequencyBands(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    float amp = config.frequency.amplifierBands;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Frequency Bands");
    
    size_t band_count = data.freq_bands.size();
    
    // Always use compact visualization with thin horizontal bars stacked vertically
    // Calculate total height for all bars with no spacing
    float barHeight = OVERLAY_BAR_HEIGHT_THIN; // Thin bar height
    float totalHeight = barHeight * band_count;
    
    // Create a child window to contain all the bars - removed scrollbars
    ImGui::BeginChild("FreqBandsCompact", ImVec2(0, totalHeight + 15), true, ImGuiWindowFlags_NoScrollbar);
    
    // Get starting cursor position for drawing bars
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    
    // Draw each frequency band as a thin bar
    for (size_t i = 0; i < band_count; ++i) {
        float value = std::clamp(data.freq_bands[i] * amp, 0.0f, 1.0f);
        // Calculate bar coordinates - each bar directly below the previous one
        ImVec2 barStart(startPos.x, startPos.y + i * barHeight);
        ImVec2 barEnd(startPos.x + value * windowSize.x, barStart.y + barHeight);
        // Draw filled bar
        ImGui::GetWindowDrawList()->AddRectFilled(
            barStart,
            barEnd,
            ImGui::GetColorU32(IM_COL32(25 + 230 * (1.0f - (float)i / band_count), // Red gradient (high to low frequencies)
                                      25 + 230 * ((float)i / band_count),         // Green gradient (low to high frequencies)
                                      230,                                        // Constant blue
                                      255)),                                       // Alpha
            OVERLAY_BAR_ROUNDING  // No rounding
        );
        // Draw background/outline for the full bar area
        ImGui::GetWindowDrawList()->AddRect(
            barStart,
            ImVec2(startPos.x + windowSize.x, barStart.y + barHeight),
            OVERLAY_BAR_COLOR_OUTLINE, // Dark gray
            OVERLAY_BAR_ROUNDING  // No rounding
        );
    }
    ImGui::Dummy(ImVec2(0, totalHeight)); // Reserve space for our custom drawing
    ImGui::EndChild();
    // Bands Boost (below display) with label
    {
        float amplifierBands = config.frequency.amplifierBands;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Bands Boost:");
        ImGui::SameLine();
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        if (DrawAmplifierSlider("##AmplifierBands", amplifierBands)) {
            config.frequency.amplifierBands = amplifierBands;
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Visual multiplier for frequency bands (uniforms too)");
        }
    }
}

// Forward declaration for controls section used below
static void DrawFrequencyBoostSettings();

// Helper: Draw Frequency Bands related controls (mapping + equalizer)
static void DrawFrequencyRelatedControls() {
    auto& config = g_configManager.GetConfig();
    ImGui::Text("Frequency Band Mapping:");
    bool band_log_scale = config.frequency.logScaleEnabled;
    if (ImGui::Checkbox("Logarithmic Bands", &band_log_scale)) {
        config.frequency.logScaleEnabled = band_log_scale;
    }
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Log scale better matches hearing; linear is legacy");
    }
    if (config.frequency.logScaleEnabled) {
        float band_log_strength = config.frequency.logStrength;
        if (ImGui::SliderFloat("##LogStrength", &band_log_strength, OVERLAY_LOG_STRENGTH_MIN, OVERLAY_LOG_STRENGTH_MAX, "%.2f")) {
            config.frequency.logStrength = band_log_strength;
        }
        ImGui::SameLine();
        ImGui::Text("Log Strength");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Controls bass detail in logarithmic scale");
        }
        float band_min_freq = config.frequency.minFreq;
        if (ImGui::SliderFloat("##MinFreq", &band_min_freq, OVERLAY_MIN_FREQ_MIN, OVERLAY_MIN_FREQ_MAX, "%.0f")) {
            config.frequency.minFreq = band_min_freq;
        }
        ImGui::SameLine();
        ImGui::Text("Min Freq (Hz)");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Minimum frequency for frequency bands");
        }
        float band_max_freq = config.frequency.maxFreq;
        if (ImGui::SliderFloat("##MaxFreq", &band_max_freq, OVERLAY_MAX_FREQ_MIN, OVERLAY_MAX_FREQ_MAX, "%.0f")) {
            config.frequency.maxFreq = band_max_freq;
        }
        ImGui::SameLine();
        ImGui::Text("Max Freq (Hz)");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Maximum frequency for frequency bands");
        }
    }
    // Equalizer and width
    DrawFrequencyBoostSettings();
}

// Helper: Draw 8-direction rose diagram for directional intensity
static void DrawDirectionRose(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    ImGui::TextUnformatted("Directional Intensity (Rose)");
    // Size of the rose area
    float size = std::min(ImGui::GetContentRegionAvail().x, 140.0f);
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(start.x + size * 0.6f, start.y + size * 0.6f);
    float radius = size * 0.5f;
    auto* dl = ImGui::GetWindowDrawList();
    // Background circle
    dl->AddCircleFilled(center, radius, IM_COL32(30,30,30,128), 64);
    dl->AddCircle(center, radius, OVERLAY_BAR_COLOR_OUTLINE, 64, 1.0f);
    // Draw 8 petals
    // Order: F, FR, R, BR, B, BL, L, FL corresponding to angles 0,45,90,...,315 degrees
    const float two_pi = 6.2831853f;
    for (int i = 0; i < 8; ++i) {
        float v = std::clamp(data.direction8[i], 0.0f, 1.0f);
        float a0 = (two_pi / 8.0f) * (i - 0.5f);
        float a1 = (two_pi / 8.0f) * (i + 0.5f);
        float r = radius * v;
        ImVec2 p0(center.x + r * cosf(a0), center.y + r * sinf(a0));
        ImVec2 p1(center.x + r * cosf(a1), center.y + r * sinf(a1));
        // Triangle from center to arc segment
        ImU32 col = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        dl->AddTriangleFilled(center, p0, p1, col);
    }
    ImGui::Dummy(ImVec2(size * 1.2f, size * 1.2f));
    // Direction Boost (below display) with label
    float amplifierDir = config.frequency.amplifierDirection;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Direction Boost:");
    ImGui::SameLine();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    if (DrawAmplifierSlider("##AmplifierDirection", amplifierDir)) {
        config.frequency.amplifierDirection = amplifierDir;
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Boosts directional intensity uniforms; visual-only multiplier");
    }
}

// Helper: Draw time/phase info
static void DrawTimePhaseInfo() {
    // Calculate time since start (same as in listeningway_addon.cpp)
    static const auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - start_time;
    float time_seconds = elapsed.count();
    float phase_60hz = std::fmod(time_seconds * 60.0f, 1.0f);
    float phase_120hz = std::fmod(time_seconds * 120.0f, 1.0f);
    float total_phases_60hz = time_seconds * 60.0f;
    float total_phases_120hz = time_seconds * 120.0f;
    ImGui::Text("Time/Phase Uniforms:");
    ImGui::Text("  Seconds: %.3f", time_seconds);
    ImGui::Text("  Phase 60Hz: %.3f", phase_60hz);
    ImGui::Text("  Phase 120Hz: %.3f", phase_120hz);
    ImGui::Text("  Total 60Hz cycles: %.1f", total_phases_60hz);
    ImGui::Text("  Total 120Hz cycles: %.1f", total_phases_120hz);
}

// Helper: Draw Beat Detection Algorithm settings

// Helper: Draw Beat Decay Settings for both algorithms

// Frequency Boost Settings section
static void DrawFrequencyBoostSettings() {
    auto& config = g_configManager.GetConfig();
    
    if (ImGui::CollapsingHeader("Frequency Boost Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("Equalizer");
          // Use the same compact style as Frequency Band Mapping with text on the right
        float equalizer_band1 = config.frequency.equalizerBands[0];
        if (ImGui::SliderFloat("##band1", &equalizer_band1, OVERLAY_EQ_BAND_MIN, OVERLAY_EQ_BAND_MAX, "%.2f")) {
            config.frequency.equalizerBands[0] = equalizer_band1;
        }
        ImGui::SameLine();
        ImGui::Text("Low (Bass)");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Boost for lowest frequency bands (bass)");
        }
          float equalizer_band2 = config.frequency.equalizerBands[1];
        if (ImGui::SliderFloat("##band2", &equalizer_band2, OVERLAY_EQ_BAND_MIN, OVERLAY_EQ_BAND_MAX, "%.2f")) {
            config.frequency.equalizerBands[1] = equalizer_band2;
        }
        ImGui::SameLine();
        ImGui::Text("Low-Mid");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Boost for low-mid frequency bands");
        }
          float equalizer_band3 = config.frequency.equalizerBands[2];
        if (ImGui::SliderFloat("##band3", &equalizer_band3, OVERLAY_EQ_BAND_MIN, OVERLAY_EQ_BAND_MAX, "%.2f")) {
            config.frequency.equalizerBands[2] = equalizer_band3;
        }
        ImGui::SameLine();
        ImGui::Text("Mid");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Boost for mid frequency bands");
        }
          float equalizer_band4 = config.frequency.equalizerBands[3];
        if (ImGui::SliderFloat("##band4", &equalizer_band4, OVERLAY_EQ_BAND_MIN, OVERLAY_EQ_BAND_MAX, "%.2f")) {
            config.frequency.equalizerBands[3] = equalizer_band4;
        }
        ImGui::SameLine();
        ImGui::Text("Mid-High");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Boost for mid-high frequency bands");
        }
          float equalizer_band5 = config.frequency.equalizerBands[4];
        if (ImGui::SliderFloat("##band5", &equalizer_band5, OVERLAY_EQ_BAND_MIN, OVERLAY_EQ_BAND_MAX, "%.2f")) {
            config.frequency.equalizerBands[4] = equalizer_band5;
        }
        ImGui::SameLine();
        ImGui::Text("High (Treble)");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Boost for highest frequency bands (treble)");
        }
        
        ImGui::PopID();
          // Add the equalizer width slider
        float equalizer_width = config.frequency.equalizerWidth;
        if (ImGui::SliderFloat("##EqualizerWidth", &equalizer_width, OVERLAY_EQ_WIDTH_MIN, OVERLAY_EQ_WIDTH_MAX, "%.2f")) {
            config.frequency.equalizerWidth = equalizer_width;
        }
        ImGui::SameLine();
        ImGui::Text("Equalizer Width");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Controls band influence on neighboring frequencies\nLower = narrow bands with less overlap\nHigher = wider bands with more influence");
        }
    }
}

// Helper: Draw spatialization info (left/right volume and pan)
static void DrawSpatialization(const AudioAnalysisData& data) {
    // Align all progress bars to the same X position
    float label_width = ImGui::CalcTextSize("Pan Angle:").x;
    float bar_start_x = ImGui::GetCursorPosX() + label_width + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float bar_width = ImGui::GetContentRegionAvail().x - (bar_start_x - ImGui::GetCursorPosX());

    // Left Volume
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Left:");
    ImGui::SameLine(bar_start_x);
    ImGui::ProgressBar(data.volume_left, ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", data.volume_left);

    // Right Volume
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Right:");
    ImGui::SameLine(bar_start_x);
    ImGui::ProgressBar(data.volume_right, ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", data.volume_right);

    // Pan Angle (legacy, remove or update to new range)
    // ImGui::AlignTextToFramePadding();
    // ImGui::Text("Pan Angle:");
    // ImGui::SameLine(bar_start_x);
    // ImGui::ProgressBar((data.audio_pan + 180.0f) / 360.0f, ImVec2(bar_width, 0.0f));
    // ImGui::SameLine();
    // ImGui::Text("%.1f deg", data.audio_pan);
}

// Helper: Draw all volume, spatialization, and beat info in a single aligned block
static void DrawVolumeSpatializationBeat(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    // Use per-metric amplifiers for visuals
    float amp = config.frequency.amplifierVolume;
    float band_amp = config.frequency.amplifierBands;
    // Find the widest label
    float label_width = ImGui::CalcTextSize("Pan Angle:").x;
    label_width = std::max(label_width, ImGui::CalcTextSize("Volume:").x);
    label_width = std::max(label_width, ImGui::CalcTextSize("Left:").x);
    label_width = std::max(label_width, ImGui::CalcTextSize("Right:").x);
    label_width = std::max(label_width, ImGui::CalcTextSize("Beat:").x);
    label_width = std::max(label_width, ImGui::CalcTextSize("Format:").x);
    label_width = std::max(label_width, ImGui::CalcTextSize("Pan Smooth:").x);
    float bar_start_x = ImGui::GetCursorPosX() + label_width + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float bar_width = ImGui::GetContentRegionAvail().x - (bar_start_x - ImGui::GetCursorPosX());    // Volume (overall)
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Volume:");
    ImGui::SameLine(bar_start_x);
    
    // Get the screen position right after the progress bar for proper alignment
    ImVec2 progress_bar_screen_pos = ImGui::GetCursorScreenPos();
    ImGui::ProgressBar(std::clamp(data.volume * amp, 0.0f, 1.0f), ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", data.volume * amp);    // Compact Left/Right display under the main volume bar
    // Volume Boost (below display) with label
    {
        float amplifierVol = config.frequency.amplifierVolume;
        ImGui::Dummy(ImVec2(0, OVERLAY_BAR_SPACING_SMALL));
        ImGui::PushItemWidth(bar_width);
        // Align with bars
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Volume Boost:");
        ImGui::SameLine(bar_start_x);
        if (DrawAmplifierSlider("##AmplifierVolume", amplifierVol)) {
            config.frequency.amplifierVolume = amplifierVol;
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Boosts overall and L/R volume uniforms; visual-only multiplier");
        }
    }
    const float thin_bar_height = OVERLAY_BAR_HEIGHT_THIN;  // Same height as frequency bands
    const float small_spacing = OVERLAY_BAR_SPACING_SMALL;    // Small gap between left and right bars
    const float half_bar_width = (bar_width - small_spacing) * 0.5f;
    
    // Use the captured progress bar position for perfect alignment
    ImVec2 start_pos = progress_bar_screen_pos;
    start_pos.y += ImGui::GetFrameHeight() + OVERLAY_BAR_SPACING_SMALL;  // Position below the progress bar
    
    // Calculate center point for both bars
    float center_x = start_pos.x + bar_width * 0.5f;
    
    // Draw Left volume bar (grows from center leftward)
    ImVec2 left_bar_bg_min = ImVec2(start_pos.x, start_pos.y);
    ImVec2 left_bar_bg_max = ImVec2(center_x - small_spacing * 0.5f, start_pos.y + thin_bar_height);
    ImVec2 left_bar_fill_min = ImVec2(center_x - small_spacing * 0.5f - std::clamp(data.volume_left * amp, 0.0f, 1.0f) * half_bar_width, start_pos.y);
    ImVec2 left_bar_fill_max = ImVec2(center_x - small_spacing * 0.5f, start_pos.y + thin_bar_height);
      // Draw Left bar background
    ImGui::GetWindowDrawList()->AddRectFilled(
        left_bar_bg_min, left_bar_bg_max,
        OVERLAY_BAR_COLOR_BG,  // Dark background
        OVERLAY_BAR_ROUNDING  // No rounding
    );
    
    // Draw Left bar fill (grows from right edge leftward)
    if (data.volume_left > 0.0f) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            left_bar_fill_min, left_bar_fill_max,
            ImGui::GetColorU32(ImGuiCol_PlotHistogram),  // Match main volume bar color
            OVERLAY_BAR_ROUNDING  // No rounding
        );
    }
    
    // Draw Right volume bar (grows from center rightward)
    ImVec2 right_bar_bg_min = ImVec2(center_x + small_spacing * 0.5f, start_pos.y);
    ImVec2 right_bar_bg_max = ImVec2(start_pos.x + bar_width, start_pos.y + thin_bar_height);
    ImVec2 right_bar_fill_min = ImVec2(center_x + small_spacing * 0.5f, start_pos.y);
    ImVec2 right_bar_fill_max = ImVec2(center_x + small_spacing * 0.5f + std::clamp(data.volume_right * amp, 0.0f, 1.0f) * half_bar_width, start_pos.y + thin_bar_height);
    
    // Draw Right bar background
    ImGui::GetWindowDrawList()->AddRectFilled(
        right_bar_bg_min, right_bar_bg_max,
        OVERLAY_BAR_COLOR_BG,  // Dark background
        OVERLAY_BAR_ROUNDING  // No rounding
    );
    
    // Draw Right bar fill (grows from left edge rightward)
    if (data.volume_right > 0.0f) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            right_bar_fill_min, right_bar_fill_max,
            ImGui::GetColorU32(ImGuiCol_PlotHistogram),  // Match main volume bar color
            OVERLAY_BAR_ROUNDING  // No rounding
        );    }    // Reserve space for the custom drawn left/right bars and move cursor down
    // Use minimal spacing between the left/right bars and the pan bar
    // This should be just enough to visually separate the bars (2-4 pixels)
    ImGui::Dummy(ImVec2(0, OVERLAY_BAR_SPACING_LARGE)); // Small spacing after left/right bars
    
    // Pan bar (without label and without text overlay)
    // Use invisible dummy element for alignment with bars above
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::SameLine(bar_start_x); // Align exactly with the bars above
    
    // Now get cursor position after alignment
    ImVec2 pan_cursor_pos = ImGui::GetCursorScreenPos();
    float pan_clamped = std::clamp(data.audio_pan, -1.0f, 1.0f);
    
    // Draw pan bar background - full width to match other bars
    ImVec2 pan_bar_bg_min = ImVec2(pan_cursor_pos.x, pan_cursor_pos.y);
    ImVec2 pan_bar_bg_max = ImVec2(pan_cursor_pos.x + bar_width, pan_cursor_pos.y + thin_bar_height);
    ImGui::GetWindowDrawList()->AddRectFilled(
        pan_bar_bg_min, pan_bar_bg_max,
        OVERLAY_BAR_COLOR_BG,  // Dark background (same as other bars)
        OVERLAY_BAR_ROUNDING  // No rounding
    );
    
    // Calculate center position
    float pan_center_x = pan_cursor_pos.x + (bar_width * 0.5f);
    
    // Draw pan bar fill based on value
    // For negative values (-1 to 0), extend from center to left
    // For positive values (0 to +1), extend from center to right
    if (pan_clamped < 0.0f) {
        // Extend to the left for negative values
        float width = -pan_clamped * (bar_width * 0.5f); // Scale to half width
        ImVec2 pan_bar_fill_min = ImVec2(pan_center_x - width, pan_cursor_pos.y);
        ImVec2 pan_bar_fill_max = ImVec2(pan_center_x, pan_cursor_pos.y + thin_bar_height);
        
        ImGui::GetWindowDrawList()->AddRectFilled(
            pan_bar_fill_min, pan_bar_fill_max,
            ImGui::GetColorU32(ImGuiCol_PlotHistogram),  // Match main volume bar color
            OVERLAY_BAR_ROUNDING  // No rounding
        );
    } else if (pan_clamped > 0.0f) {
        // Extend to the right for positive values
        float width = pan_clamped * (bar_width * 0.5f); // Scale to half width
        ImVec2 pan_bar_fill_min = ImVec2(pan_center_x, pan_cursor_pos.y);
        ImVec2 pan_bar_fill_max = ImVec2(pan_center_x + width, pan_cursor_pos.y + thin_bar_height);
        
        ImGui::GetWindowDrawList()->AddRectFilled(
            pan_bar_fill_min, pan_bar_fill_max,
            ImGui::GetColorU32(ImGuiCol_PlotHistogram),  // Match main volume bar color
            OVERLAY_BAR_ROUNDING  // No rounding
        );
    }
    
    // Add center marker line
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(pan_center_x, pan_cursor_pos.y),
        ImVec2(pan_center_x, pan_cursor_pos.y + thin_bar_height),
        OVERLAY_BAR_COLOR_CENTER_MARKER,  // White semi-transparent
        OVERLAY_BAR_CENTER_MARKER_THICKNESS
    );
    
    // Reserve space for the pan bar (using Dummy to advance cursor)
    ImGui::Dummy(ImVec2(bar_width, thin_bar_height));
    
    // Add spacing after the pan bar (same as other bars)
    ImGui::Dummy(ImVec2(0, OVERLAY_BAR_SPACING_LARGE));
    // Pan smoothing and offset (placed near pan visualization)
    auto& config_ref = g_configManager.GetConfig();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Pan Smooth:");
    ImGui::SameLine(bar_start_x);
    float pan_smoothing = config_ref.audio.panSmoothing;
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##PanSmoothing", &pan_smoothing, 0.0f, 1.0f, "%.2f")) {
        config_ref.audio.panSmoothing = pan_smoothing;
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Reduces pan jitter. 0.0 = no smoothing, higher values = more smoothing");
    }
    float pan_offset = config_ref.audio.panOffset;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Pan Offset:");
    ImGui::SameLine(bar_start_x);
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##PanOffset", &pan_offset, OVERLAY_PAN_OFFSET_MIN, OVERLAY_PAN_OFFSET_MAX, "%.2f")) {
        config_ref.audio.panOffset = pan_offset;
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Adjusts detected pan left/right (-1..+1)");
    }
    ImGui::Dummy(ImVec2(0, OVERLAY_BAR_SPACING_LARGE));
    // Beat
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Beat:");
    ImGui::SameLine(bar_start_x);
    ImGui::ProgressBar(std::clamp(data.beat, 0.0f, 1.0f), ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", data.beat);
    // Show Audio Format inline in overview for reference
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Format:");
    ImGui::SameLine(bar_start_x);
    AudioFormat format = AudioFormatUtils::IntToFormat(static_cast<int>(data.audio_format));
    const char* format_name = AudioFormatUtils::FormatToString(format);
    ImGui::Text("%s (%.0f)", format_name, data.audio_format);
}

// Draws the Listeningway debug overlay using ImGui.
// Shows volume, beat, and frequency bands in real time.
void DrawListeningwayDebugOverlay(const AudioAnalysisData& data) {
    try {
        ImGui::GetIO().UserData = (void*)&data;
        LOCK_AUDIO_DATA();
    // Collapsing headers persist via ImGui .ini automatically; set defaults on first use only
        // Single-page, logically grouped layout with collapsible sections
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("System & Links", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawToggles();
            DrawLogInfo();
            DrawWebsite();
        }
        ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Levels & Beat", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Levels: Volume + L/R + Pan (with smoothing/offset) and Beat
            DrawVolumeSpatializationBeat(data);
            // Beat settings directly after Beat visual
            if (DrawBeatSettingsPanel(data, g_configManager)) {
                LOG_DEBUG("[Overlay] Beat settings changed");
            }
        }
        ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Frequency Bands", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Frequency Bands block: Bands, mapping, equalizer
            DrawFrequencyBands(data);
            DrawFrequencyRelatedControls();
        }
        ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Directional Intensity", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Direction block
            DrawDirectionRose(data);
        }
        ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Time & Phase", ImGuiTreeNodeFlags_DefaultOpen)) {
            // System/time
            DrawTimePhaseInfo();
        }
        ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Settings Management", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Settings Management:");
            ImGui::Columns(3, "settings_buttons", false);
            if (ImGui::Button("Save Settings", ImVec2(-1, 0))) {
                if (g_configManager.Save()) {
                    LOG_DEBUG("[Overlay] Settings saved to file");
                } else {
                    LOG_ERROR("[Overlay] Failed to save settings to file");
                }
            }
            ImGui::NextColumn();
            if (ImGui::Button("Load Settings", ImVec2(-1, 0))) {
                if (g_configManager.Load()) {
                    LOG_DEBUG("[Overlay] Settings loaded from file");
                } else {
                    LOG_ERROR("[Overlay] Failed to load settings to file");
                }
            }
            ImGui::NextColumn();
            if (ImGui::Button("Reset to Default", ImVec2(-1, 0))) {
                g_configManager.ResetToDefaults();
                LOG_DEBUG("[Overlay] Settings reset to default values");
            }
            ImGui::Columns(1);
        }
        ImGui::Separator();
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Overlay] Exception in DrawListeningwayDebugOverlay: ") + ex.what());
    } catch (...) {
        LOG_ERROR("[Overlay] Unknown exception in DrawListeningwayDebugOverlay.");    }
}
