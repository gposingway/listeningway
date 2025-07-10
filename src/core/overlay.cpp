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
    for (size_t i = 0; i < available_providers.size(); ++i) {
        if (available_providers[i].code == current_code) {
            display_selection_index = static_cast<int>(i);
            break;
        }
    }

    if (ImGui::BeginCombo("Audio Analysis", provider_names[display_selection_index])) {
        int previous_selection = display_selection_index;
        bool switching_provider = g_switching_provider;
        for (int i = 0; i < provider_names.size(); ++i) {
            const bool is_selected = (display_selection_index == i);
            bool selectable = !switching_provider;            if (ImGui::Selectable(provider_names[i], is_selected, selectable ? 0 : ImGuiSelectableFlags_Disabled)) {
                if (previous_selection != i && !switching_provider) {
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

// Helper: Draw volume meter
static void DrawVolume(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    float amp = config.frequency.volume_amplifier;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Volume:");
    ImGui::SameLine();
    ImGui::ProgressBar(std::clamp(data.volume * amp, 0.0f, 1.0f), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
}

// Helper: Draw beat meter
static void DrawBeat(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    float amp = config.frequency.volume_amplifier;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Beat:");
    ImGui::SameLine();
    ImGui::ProgressBar(std::clamp(data.beat * amp, 0.0f, 1.0f), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
}

// Helper: Draw frequency bands with view mode
static void DrawFrequencyBands(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    float amp = config.frequency.band_amplifier;
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
}

// Helper: Draw Artistic Beat Detection settings
static void DrawBeatDetectionAlgorithm(const AudioAnalysisData& data) {
    auto& config = g_configManager.GetConfig();
    
    ImGui::Text("Artistic Beat Detection:");
    
    // Beat Style Selection
    const char* beat_styles[] = { "Mixed (Balanced)", "Electronic", "Acoustic", "Vocal" };
    int current_style = 0;
    if (config.beat.beat_style == "electronic") current_style = 1;
    else if (config.beat.beat_style == "acoustic") current_style = 2;
    else if (config.beat.beat_style == "vocal") current_style = 3;
    
    if (ImGui::Combo("Beat Style", &current_style, beat_styles, IM_ARRAYSIZE(beat_styles))) {
        switch(current_style) {
            case 1: config.beat.beat_style = "electronic"; break;
            case 2: config.beat.beat_style = "acoustic"; break;
            case 3: config.beat.beat_style = "vocal"; break;
            default: config.beat.beat_style = "mixed"; break;
        }
        LOG_DEBUG(std::string("[Overlay] Beat Style changed to ") + config.beat.beat_style);
    }
    
    if (ImGui::IsItemHovered(-1)) {
        switch(current_style) {
            case 1: ImGui::SetTooltip("Electronic: Emphasizes kick drums and sharp electronic beats"); break;
            case 2: ImGui::SetTooltip("Acoustic: Broader response for live instruments and drums"); break;
            case 3: ImGui::SetTooltip("Vocal: Emphasizes vocal dynamics and breathing patterns"); break;
            default: ImGui::SetTooltip("Mixed: Balanced detection for various music types"); break;
        }
    }
    
    // Artistic Beat Controls
    if (ImGui::CollapsingHeader("Beat Character Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        float kick_punch = config.beat.kick_punch;
        if (ImGui::SliderFloat("##KickPunch", &kick_punch, 0.5f, 3.0f, "%.2f")) {
            config.beat.kick_punch = kick_punch;
        }
        ImGui::SameLine();
        ImGui::Text("Kick Punch");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("How sensitive to bass drums and kick sounds");
        }
        
        float snare_snap = config.beat.snare_snap;
        if (ImGui::SliderFloat("##SnareSnap", &snare_snap, 0.5f, 2.0f, "%.2f")) {
            config.beat.snare_snap = snare_snap;
        }
        ImGui::SameLine();
        ImGui::Text("Snare Snap");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("How sensitive to snare drums and percussion");
        }
        
        float beat_boost = config.beat.beat_boost;
        if (ImGui::SliderFloat("##BeatBoost", &beat_boost, 0.5f, 3.0f, "%.2f")) {
            config.beat.beat_boost = beat_boost;
        }
        ImGui::SameLine();
        ImGui::Text("Beat Boost");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Overall beat enhancement multiplier");
        }
        
        float beat_hold = config.beat.beat_hold;
        if (ImGui::SliderFloat("##BeatHold", &beat_hold, 0.1f, 1.0f, "%.2f")) {
            config.beat.beat_hold = beat_hold;
        }
        ImGui::SameLine();
        ImGui::Text("Beat Hold");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("How long beats visually sustain (seconds)");
        }
        
        // Display current tempo info
        if (data.tempo_detected) {
            ImGui::Text("Current Tempo: %.1f BPM (Confidence: %.2f)", data.tempo_bpm, data.tempo_confidence);
            ImGui::Text("Beat Phase: %.2f", data.beat_phase);
        } else {
            ImGui::Text("No tempo detected yet");
        }
    }
}

// Helper: Draw Artistic Frequency Controls 
static void DrawBeatDecaySettings() {
    auto& config = g_configManager.GetConfig();
    ImGui::Text("Artistic Frequency Controls:");
    
    if (ImGui::CollapsingHeader("Frequency Response Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        float bass_punch = config.frequency.bass_punch;
        if (ImGui::SliderFloat("##BassPunch", &bass_punch, 0.5f, 3.0f, "%.2f")) {
            config.frequency.bass_punch = bass_punch;
        }
        ImGui::SameLine();
        ImGui::Text("Bass Punch");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Make low frequencies hit harder for visual impact");
        }
        
        float high_detail = config.frequency.high_detail;
        if (ImGui::SliderFloat("##HighDetail", &high_detail, 0.5f, 3.0f, "%.2f")) {
            config.frequency.high_detail = high_detail;
        }
        ImGui::SameLine();
        ImGui::Text("High Detail");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Bring out crisp details in high frequencies");
        }
        
        float vocal_focus = config.frequency.vocal_focus;
        if (ImGui::SliderFloat("##VocalFocus", &vocal_focus, 0.5f, 3.0f, "%.2f")) {
            config.frequency.vocal_focus = vocal_focus;
        }
        ImGui::SameLine();
        ImGui::Text("Vocal Focus");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Emphasize voices and instruments in mid-range");
        }

        ImGui::Separator();
        ImGui::Text("Frequency Range:");
        
        float min_freq = config.frequency.minFreq;
        if (ImGui::SliderFloat("##MinFreq", &min_freq, 10.0f, 500.0f, "%.0f Hz")) {
            config.frequency.minFreq = min_freq;
            // Reinitialize mapping when range changes
            // This will be handled by the analysis system automatically
        }
        ImGui::SameLine();
        ImGui::Text("Min Frequency");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Lowest frequency to visualize (10-500 Hz)");
        }
        
        float max_freq = config.frequency.maxFreq;
        if (ImGui::SliderFloat("##MaxFreq", &max_freq, 2000.0f, 20000.0f, "%.0f Hz")) {
            config.frequency.maxFreq = max_freq;
            // Reinitialize mapping when range changes
            // This will be handled by the analysis system automatically
        }
        ImGui::SameLine();
        ImGui::Text("Max Frequency");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("Highest frequency to visualize (2000-20000 Hz)");
        }
    }
    
    if (ImGui::CollapsingHeader("Visual Timing Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        float quick_response = config.frequency.quick_response;
        if (ImGui::SliderFloat("##QuickResponse", &quick_response, 0.1f, 5.0f, "%.2f")) {
            config.frequency.quick_response = quick_response;
        }
        ImGui::SameLine();
        ImGui::Text("Quick Response");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("How fast visuals react to audio changes");
        }
        
        float smooth_fade = config.frequency.smooth_fade;
        if (ImGui::SliderFloat("##SmoothFade", &smooth_fade, 0.1f, 0.99f, "%.2f")) {
            config.frequency.smooth_fade = smooth_fade;
        }
        ImGui::SameLine();
        ImGui::Text("Smooth Fade");
        if (ImGui::IsItemHovered(-1)) {
            ImGui::SetTooltip("How gently levels drop off for smooth visuals");
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
    float amp = config.frequency.volume_amplifier;
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
    // Beat
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Beat:");
    ImGui::SameLine(bar_start_x);
    ImGui::ProgressBar(data.beat, ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", data.beat * amp);    // Audio Format
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Format:");
    ImGui::SameLine(bar_start_x);
    AudioFormat format = AudioFormatUtils::IntToFormat(static_cast<int>(data.audio_format));
    const char* format_name = AudioFormatUtils::FormatToString(format);
    ImGui::Text("%s (%.0f)", format_name, data.audio_format);// Pan Smoothing
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Pan Smooth:");
    ImGui::SameLine(bar_start_x);
    float pan_smoothing = config.audio.panSmoothing;
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##PanSmoothing", &pan_smoothing, 0.0f, 1.0f, "%.2f")) {
        config.audio.panSmoothing = pan_smoothing;
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Reduces pan jitter. 0.0 = no smoothing (current behavior), higher values = more smoothing");
    }
    // Pan Offset slider (full width, under Pan Smoothing)
    float pan_offset = config.audio.panOffset;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Pan Offset:");
    ImGui::SameLine(bar_start_x);
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##PanOffset", &pan_offset, OVERLAY_PAN_OFFSET_MIN, OVERLAY_PAN_OFFSET_MAX, "%.2f")) {
        config.audio.panOffset = pan_offset;
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Adjusts the detected pan left/right. -1 = full left, 0 = no offset, +1 = full right. Use to compensate for system or room bias.");
    }
    
    // Volume Amplifier slider
    float volume_amplifier = config.frequency.volume_amplifier;
    bool vol_amp_is_spinal = volume_amplifier > 10.0f;
    if (vol_amp_is_spinal) {
        float t = std::clamp((volume_amplifier - 10.0f) / 1.0f, 0.0f, 1.0f);
        ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);
        ImVec4 red = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
        ImVec4 lerped = ImVec4(
            base.x * (1.0f - t) + red.x * t,
            base.y * (1.0f - t) + red.y * t,
            base.z * (1.0f - t) + red.z * t,
            base.w * (1.0f - t) + red.w * t
        );
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, lerped);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, lerped);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Volume Amplifier:");
    ImGui::SameLine(bar_start_x);
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##VolumeAmplifier", &volume_amplifier, 0.1f, 10.0f, "%.2f")) {
        config.frequency.volume_amplifier = volume_amplifier;
    }
    ImGui::PopItemWidth();
    if (vol_amp_is_spinal) {
        ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Controls the volume/RMS level amplification for general loudness boost");
    }
    
    // Band Amplifier slider
    float band_amplifier = config.frequency.band_amplifier;
    bool band_amp_is_spinal = band_amplifier > 10.0f;
    if (band_amp_is_spinal) {
        float t = std::clamp((band_amplifier - 10.0f) / 1.0f, 0.0f, 1.0f);
        ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);
        ImVec4 red = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
        ImVec4 lerped = ImVec4(
            base.x * (1.0f - t) + red.x * t,
            base.y * (1.0f - t) + red.y * t,
            base.z * (1.0f - t) + red.z * t,
            base.w * (1.0f - t) + red.w * t
        );
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, lerped);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, lerped);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Band Amplifier:");
    ImGui::SameLine(bar_start_x);
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##BandAmplifier", &band_amplifier, 0.1f, 10.0f, "%.2f")) {
        config.frequency.band_amplifier = band_amplifier;
    }
    ImGui::PopItemWidth();
    if (band_amp_is_spinal) {
        ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered(-1)) {
        ImGui::SetTooltip("Controls the frequency band visualization amplification for spectrum boost");
    }
}

// Draws the Listeningway debug overlay using ImGui.
// Shows volume, beat, and frequency bands in real time.
void DrawListeningwayDebugOverlay(const AudioAnalysisData& data) {
    try {
        ImGui::GetIO().UserData = (void*)&data;
        LOCK_AUDIO_DATA();
        DrawToggles();
        DrawLogInfo();
        ImGui::Separator();
        DrawWebsite();
        ImGui::Separator();        DrawVolumeSpatializationBeat(data);
        ImGui::Separator();
        DrawFrequencyBands(data);
        ImGui::Separator();
        
        // Beat Detection Algorithm Selection and Configuration
        DrawBeatDetectionAlgorithm(data);
        ImGui::Separator();
        
        // Beat Decay Settings
        DrawBeatDecaySettings();
        ImGui::Separator();
          // Consolidated buttons for Save, Load and Default
        ImGui::Text("Settings Management:");
        
        // Use columns to position the buttons side by side with equal width
        ImGui::Columns(3, "settings_buttons", false);
        // Save button
        if (ImGui::Button("Save Settings", ImVec2(-1, 0))) {
            if (g_configManager.Save()) {
                LOG_DEBUG("[Overlay] Settings saved to file");
            } else {
                LOG_ERROR("[Overlay] Failed to save settings to file");
            }
        }
        ImGui::NextColumn();
        // Load button
        if (ImGui::Button("Load Settings", ImVec2(-1, 0))) {
            if (g_configManager.Load()) {
                LOG_DEBUG("[Overlay] Settings loaded from file");
                // No longer re-apply config here; ConfigurationManager handles it
            } else {
                LOG_ERROR("[Overlay] Failed to load settings to file");
            }
        }
        ImGui::NextColumn();
        // Default button
        if (ImGui::Button("Reset to Default", ImVec2(-1, 0))) {
            g_configManager.ResetToDefaults();
            LOG_DEBUG("[Overlay] Settings reset to default values");
            // No longer re-apply config here; ConfigurationManager handles it
        }
        
        // Reset columns
        ImGui::Columns(1);
        
        ImGui::Separator();
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Overlay] Exception in DrawListeningwayDebugOverlay: ") + ex.what());
    } catch (...) {
        LOG_ERROR("[Overlay] Unknown exception in DrawListeningwayDebugOverlay.");
    }
}
