// ---------------------------------------------
// v2 overlay.
//
// Layout philosophy:
//   - Each "block" with both visuals AND settings shows the visuals
//     unconditionally; the settings are tucked behind a collapsed-by-default
//     `Settings` TreeNode inside the block. Even with everything closed, the
//     visual dashboard is intact.
//   - Every interactive row passes through one helper layer that aligns
//     labels to a global pixel column, so meters and sliders line up
//     across the entire window.
//   - Custom thin bars are vertically centered within FrameHeight rows so
//     they sit on the same baseline as text and ImGui sliders.
// ---------------------------------------------
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>

#include "overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "../audio/pipeline/audio_system.h"
#include "../config/store.h"

namespace lw {

// ----- Visual constants --------------------------------------------------
namespace overlay_style {
constexpr float kBarHeightThin       = 6.0f;
constexpr float kSubGroupIndent      = 6.0f;
constexpr float kBarRounding         = 0.0f;
constexpr float kPanCenterThickness  = 1.0f;

constexpr ImU32 kColorBg            = IM_COL32(40, 40, 40, 128);
constexpr ImU32 kColorOutline       = IM_COL32(60, 60, 60, 128);
constexpr ImU32 kColorCenterMarker  = IM_COL32(255, 255, 255, 180);
constexpr ImU32 kColorProfiler      = IM_COL32(120, 200, 255, 200);
}  // namespace overlay_style

namespace {

float g_label_col = 0.0f;       ///< pixel X (window-relative) where meters/widgets start
float g_value_col_w = 0.0f;     ///< reserved width for the right-side value text

// One-time-per-draw setup of the alignment columns. Computes from the widest
// label string we know we'll print so every section uses the same layout.
void compute_columns() {
    // Use a known-long label for sizing so all rows align even when only a
    // subset is rendered. We pick the actual longest used label below.
    const char* probes[] = {
        "Threshold window (ms):",
        "Tempo prior \xCF\x83 (oct):",   // UTF-8 σ
        "Direction Boost:",
        "Volume:",
    };
    float widest = 0.0f;
    for (const char* p : probes) {
        widest = std::max(widest, ImGui::CalcTextSize(p).x);
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    g_label_col   = ImGui::GetCursorPosX() + widest + spacing * 2.0f;
    g_value_col_w = ImGui::CalcTextSize("99.99 \xC2\xB5s").x + spacing;  // µs leaves room for profiler
}

void tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

// Position cursor at label_col, after writing the label flush-left.
void label_left(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(g_label_col);
}

// ---- Row helpers ---------------------------------------------------------
// All rows occupy exactly FrameHeight vertical space, so meters, sliders,
// combos, and text rows visually align row-to-row.

void meter_row(const char* label, float value, ImU32 fill,
               const char* value_fmt = "%.2f") {
    using namespace overlay_style;
    label_left(label);
    const float frame_h = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // Always reserve the value column so rows align even when this row has
    // no built-in value text — caller may use SameLine+Text to fill it.
    const float val_w  = g_value_col_w;
    const float bar_w  = std::max(8.0f, ImGui::GetContentRegionAvail().x - val_w - spacing);

    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y_off = (frame_h - kBarHeightThin) * 0.5f;
    const ImVec2 bar_tl(p.x, p.y + y_off);
    const ImVec2 bar_br(p.x + bar_w, bar_tl.y + kBarHeightThin);
    dl->AddRectFilled(bar_tl, bar_br, kColorBg, kBarRounding);
    if (value > 0.0f) {
        const float v = std::clamp(value, 0.0f, 1.0f);
        dl->AddRectFilled(bar_tl, ImVec2(p.x + v * bar_w, bar_br.y), fill, kBarRounding);
    }
    dl->AddRect(bar_tl, bar_br, kColorOutline, kBarRounding);
    ImGui::Dummy(ImVec2(bar_w, frame_h));
    if (value_fmt && value_fmt[0]) {
        ImGui::SameLine();
        ImGui::Text(value_fmt, value);
    }
}

void center_meter_row(const char* label, float value) {
    using namespace overlay_style;
    label_left(label);
    const float frame_h = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float val_w  = g_value_col_w;
    const float bar_w  = std::max(8.0f, ImGui::GetContentRegionAvail().x - val_w - spacing);

    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y_off = (frame_h - kBarHeightThin) * 0.5f;
    const ImVec2 bg_tl(p.x, p.y + y_off);
    const ImVec2 bg_br(p.x + bar_w, bg_tl.y + kBarHeightThin);
    dl->AddRectFilled(bg_tl, bg_br, kColorBg, kBarRounding);

    const float v = std::clamp(value, -1.0f, 1.0f);
    const float center_x = p.x + bar_w * 0.5f;
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    if (v < 0.0f) {
        const float w = -v * bar_w * 0.5f;
        dl->AddRectFilled(ImVec2(center_x - w, bg_tl.y),
                          ImVec2(center_x, bg_br.y), fill, kBarRounding);
    } else if (v > 0.0f) {
        const float w = v * bar_w * 0.5f;
        dl->AddRectFilled(ImVec2(center_x, bg_tl.y),
                          ImVec2(center_x + w, bg_br.y), fill, kBarRounding);
    }
    dl->AddLine(ImVec2(center_x, bg_tl.y), ImVec2(center_x, bg_br.y),
                kColorCenterMarker, kPanCenterThickness);
    dl->AddRect(bg_tl, bg_br, kColorOutline, kBarRounding);
    ImGui::Dummy(ImVec2(bar_w, frame_h));
    ImGui::SameLine();
    ImGui::Text("%+.2f", value);
}

bool slider_row(const char* label, float* v, float lo, float hi,
                const char* fmt = "%.2f", const char* tooltip = nullptr) {
    label_left(label);
    char id[40]; std::snprintf(id, sizeof(id), "##sf_%s", label);
    ImGui::PushItemWidth(-1);
    bool changed = ImGui::SliderFloat(id, v, lo, hi, fmt);
    ImGui::PopItemWidth();
    if (tooltip) tip(tooltip);
    return changed;
}

bool slider_int_row(const char* label, int* v, int lo, int hi,
                    const char* tooltip = nullptr) {
    label_left(label);
    char id[40]; std::snprintf(id, sizeof(id), "##si_%s", label);
    ImGui::PushItemWidth(-1);
    bool changed = ImGui::SliderInt(id, v, lo, hi);
    ImGui::PopItemWidth();
    if (tooltip) tip(tooltip);
    return changed;
}

bool combo_row(const char* label, int* sel,
               const char* const items[], int count,
               const char* tooltip = nullptr) {
    label_left(label);
    char id[40]; std::snprintf(id, sizeof(id), "##co_%s", label);
    ImGui::PushItemWidth(-1);
    bool changed = ImGui::Combo(id, sel, items, count);
    ImGui::PopItemWidth();
    if (tooltip) tip(tooltip);
    return changed;
}

bool checkbox_row(const char* label, bool* v, const char* tooltip = nullptr) {
    label_left(label);
    char id[40]; std::snprintf(id, sizeof(id), "##cb_%s", label);
    bool changed = ImGui::Checkbox(id, v);
    if (tooltip) tip(tooltip);
    return changed;
}

void info_row(const char* label, const char* fmt, ...) {
    label_left(label);
    va_list ap; va_start(ap, fmt);
    ImGui::TextV(fmt, ap);
    va_end(ap);
}

// ---- Common building blocks ---------------------------------------------

const char* state_label(State s) {
    switch (s) {
        case State::Off:      return "Off";
        case State::Starting: return "Starting";
        case State::Running:  return "Running";
        case State::Stopping: return "Stopping";
        case State::Error:    return "Error";
    }
    return "?";
}

const char* format_label(int channels) {
    switch (channels) {
        case 0:  return "None";
        case 1:  return "Mono";
        case 2:  return "Stereo";
        case 6:  return "5.1";
        case 8:  return "7.1";
        default: return "Multi";
    }
}

// Compact stacked frequency band display with red→green gradient.
void compact_band_bars(std::span<const float> values, float amp, float width) {
    using namespace overlay_style;
    if (values.empty() || width <= 0.0f) return;
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float bar_h = kBarHeightThin;
    const size_t n = values.size();
    for (size_t i = 0; i < n; ++i) {
        const float v = std::clamp(values[i] * amp, 0.0f, 1.0f);
        const float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, n - 1));
        const ImU32 col = IM_COL32(
            25 + static_cast<int>(230 * (1.0f - t)),
            25 + static_cast<int>(230 * t),
            230, 255);
        const ImVec2 bar_tl(start.x, start.y + i * bar_h);
        const ImVec2 bar_br(start.x + v * width, bar_tl.y + bar_h);
        dl->AddRectFilled(bar_tl, bar_br, col, kBarRounding);
        dl->AddRect(bar_tl, ImVec2(start.x + width, bar_tl.y + bar_h),
                    kColorOutline, kBarRounding);
    }
    ImGui::Dummy(ImVec2(width, bar_h * static_cast<float>(n)));
}

// Default-collapsed Settings sub-tree. Returns true if open (caller draws
// config inside). `section_id` MUST be unique across the overlay, otherwise
// ImGui hashes the same ID for every "Settings" node and they toggle as one.
bool settings_subtree(const char* section_id) {
    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    char id[64];
    std::snprintf(id, sizeof(id), "Settings##%s", section_id);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx(id, flags);
    ImGui::PopStyleColor();
    return open;
}

// Section title (always visible, never collapses).
void section_title(const char* title) {
    ImGui::SeparatorText(title);
}

// Sub-group label inside a section (e.g. "Phases:"). Items below should be
// indented kSubGroupIndent.
void subgroup_label(const char* label) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", label);
}

}  // namespace

// ---------------- Section: System & Source -------------------------------

static void section_system(AudioSystem& system, config::Settings& cfg, bool& dirty) {
    section_title("System & Source");

    info_row("State:", "%s   \xE2\x80\xA2   Listeningway v2.0.0-beta", state_label(system.state()));

    const auto sources = system.available_sources();
    int current = 0;
    std::vector<std::string> names;
    for (size_t i = 0; i < sources.size(); ++i) {
        names.push_back(sources[i].display);
        if (sources[i].code == cfg.audio.capture_source_code) current = (int)i;
    }
    std::vector<const char*> name_ptrs;
    for (auto& n : names) name_ptrs.push_back(n.c_str());

    int sel = current;
    label_left("Source:");
    ImGui::PushItemWidth(-1);
    if (ImGui::Combo("##source_combo", &sel, name_ptrs.data(), (int)name_ptrs.size())
        && sel >= 0 && sel < (int)sources.size()) {
        system.switch_source(sources[sel].code);
    }
    ImGui::PopItemWidth();
    tip("Audio capture source. 'Off' disables analysis.");

    if (settings_subtree("system")) {
        ImGui::Indent(overlay_style::kSubGroupIndent);
        if (checkbox_row("Enable SIMD (SSE/AVX)", &cfg.audio.simd_enabled,
            "Use SIMD intrinsics in DSP stages where available."))   dirty = true;
        if (checkbox_row("Debug logging", &cfg.debug.debug_logging,
            "Verbose log to listeningway.log."))                     dirty = true;
        ImGui::Unindent(overlay_style::kSubGroupIndent);
    }
}

// ---------------- Section: Levels & Beat ---------------------------------

static void section_levels(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    section_title("Levels");

    const float vol_amp = cfg.frequency.amplifier_volume;
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    meter_row("Volume:", std::clamp(snap.volume * vol_amp, 0.0f, 1.0f), fill, "%.2f");

    subgroup_label("Stereo:");
    ImGui::Indent(overlay_style::kSubGroupIndent);
    meter_row("Left:",  std::clamp(snap.volume_left  * vol_amp, 0.0f, 1.0f), fill);
    meter_row("Right:", std::clamp(snap.volume_right * vol_amp, 0.0f, 1.0f), fill);
    center_meter_row("Pan:", snap.audio_pan);
    ImGui::Unindent(overlay_style::kSubGroupIndent);

    info_row("Format:", "%s (%.0f ch)", format_label(static_cast<int>(snap.audio_format)),
             snap.audio_format);

    if (settings_subtree("levels")) {
        ImGui::Indent(overlay_style::kSubGroupIndent);
        if (slider_row("Volume Boost", &cfg.frequency.amplifier_volume, 1.0f, 11.0f, "%.2f",
            "Visual-only multiplier on the volume uniforms (does not affect analysis)."))
            dirty = true;
        if (slider_row("Pan Smoothing", &cfg.audio.pan_smoothing, 0.0f, 1.0f, "%.2f",
            "Reduces pan jitter. 0 = no smoothing."))                         dirty = true;
        if (slider_row("Pan Offset", &cfg.audio.pan_offset, -1.0f, 1.0f, "%.2f",
            "User pan bias (-1 = full left, +1 = full right)."))             dirty = true;
        ImGui::Unindent(overlay_style::kSubGroupIndent);
    }
}

// ---------------- Section: Beat Detection (visual + settings) -----------

static void section_beat(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    section_title("Beat Detection");

    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    meter_row("Beat:",       std::clamp(snap.beat,       0.0f, 1.0f), fill, "%.2f");
    meter_row("Beat Phase:", std::clamp(snap.beat_phase, 0.0f, 1.0f), fill, "%.2f");
    if (snap.tempo_detected) {
        info_row("Tempo:", "%.1f BPM (%.0f%% confidence)",
                 snap.tempo_bpm, snap.tempo_confidence * 100.0f);
    } else {
        label_left("Tempo:");
        ImGui::TextDisabled("unlocked  (%.1f BPM, %.0f%%)",
                            snap.tempo_bpm, snap.tempo_confidence * 100.0f);
    }

    if (settings_subtree("beat")) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Threshold lambda", &cfg.beat.threshold_lambda, 0.0f, 1.0f, "%.3f",
            "Aubio-formula \xCE\xBB in `median + \xCE\xBB\xC2\xB7mean` adaptive threshold.")) dirty = true;
        if (slider_row("Threshold window (ms)", &cfg.beat.threshold_window_ms, 10.0f, 1000.0f, "%.0f",
            "Sliding window for adaptive threshold computation.")) dirty = true;
        if (slider_row("Refractory (ms)", &cfg.beat.refractory_ms, 5.0f, 500.0f, "%.0f",
            "Minimum interval between detected onsets (suppresses doubles).")) dirty = true;
        if (slider_row("Phase k_p", &cfg.beat.phase_kp, 0.0f, 1.0f, "%.3f",
            "PLL phase-pull strength on confident detected onsets.")) dirty = true;
        if (slider_row("Phase k_i", &cfg.beat.phase_ki, 0.0f, 0.5f, "%.4f",
            "PLL BPM-drift correction strength.")) dirty = true;
        if (slider_row("Tempo prior BPM", &cfg.beat.tempo_prior_bpm, 60.0f, 200.0f, "%.0f",
            "Center of the log-Gaussian tempo prior (mitigates octave errors).")) dirty = true;
        if (slider_row("Tempo prior \xCF\x83 (oct)", &cfg.beat.tempo_prior_sigma, 0.1f, 2.0f, "%.2f",
            "Width of the tempo prior in octaves.")) dirty = true;
        if (slider_row("Tempo window (s)", &cfg.beat.tempo_window_sec, 1.0f, 30.0f, "%.1f",
            "Autocorrelation history length.")) dirty = true;
        if (slider_row("Beat decay /s", &cfg.beat.beat_decay_per_sec, 0.1f, 10.0f, "%.2f",
            "Decay rate for the `beat` uniform after a detected onset.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---------------- Section: Frequency Bands -------------------------------

static void section_frequency(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    section_title("Frequency Bands");

    const uint32_t n = std::min<uint32_t>(snap.freq_band_count, 64);
    const float bands_amp = cfg.frequency.amplifier_bands;
    info_row("Count:", "%u bands  (post-EQ, post-LogBoost)", n);

    ImGui::BeginChild("##bands_compact",
                      ImVec2(0.0f, kBarHeightThin * static_cast<float>(n) + 12.0f),
                      true, ImGuiWindowFlags_NoScrollbar);
    compact_band_bars(std::span<const float>(snap.freq_bands.data(), n),
                      bands_amp, ImGui::GetContentRegionAvail().x);
    ImGui::EndChild();

    if (settings_subtree("frequency")) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Bands Boost", &cfg.frequency.amplifier_bands, 1.0f, 11.0f, "%.2f",
            "Visual-only multiplier on the bands uniforms.")) dirty = true;

        subgroup_label("Band mapping:");
        ImGui::Indent(kSubGroupIndent);
        const char* const scales[] = {"Linear", "Log", "Mel (Slaney)"};
        int scale = static_cast<int>(cfg.frequency.band_scale);
        if (combo_row("Band scale", &scale, scales, 3,
            "Mel matches perception best; Log was the v1 default; Linear is legacy.")) {
            cfg.frequency.band_scale =
                static_cast<config::FrequencyConfig::BandScale>(scale);
            dirty = true;
        }
        if (slider_int_row("Band count", &cfg.frequency.band_count, 8, 128,
            "Live band count published. Cap is kMaxBands.")) dirty = true;
        if (slider_int_row("FFT size", &cfg.frequency.fft_size, 256, 8192,
            "Power-of-two recommended. Larger = finer resolution, more CPU.")) dirty = true;
        if (slider_row("Min freq", &cfg.frequency.min_freq, 10.0f, 500.0f, "%.0f",
            "Lowest band edge in Hz.")) dirty = true;
        if (slider_row("Max freq", &cfg.frequency.max_freq, 2000.0f, 22050.0f, "%.0f",
            "Highest band edge in Hz.")) dirty = true;
        if (slider_row("Log strength", &cfg.frequency.log_strength, 0.01f, 1.5f, "%.2f",
            "Gain curve over band index when log scale enabled.")) dirty = true;
        if (slider_row("Band norm", &cfg.frequency.band_norm, 0.001f, 1.0f, "%.3f",
            "Raw-magnitude → band-amplitude scaling.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);

        subgroup_label("Equalizer (5-band, Gaussian):");
        ImGui::Indent(kSubGroupIndent);
        const char* const eq_names[5] = {"Low (Bass)", "Low-Mid", "Mid", "Mid-High", "High (Treble)"};
        for (int i = 0; i < 5; ++i) {
            char buf[24]; std::snprintf(buf, sizeof(buf), "%s", eq_names[i]);
            if (slider_row(buf, &cfg.frequency.equalizer_bands[i], 0.0f, 4.0f, "%.2f"))
                dirty = true;
        }
        if (slider_row("Width", &cfg.frequency.equalizer_width, 0.05f, 0.5f, "%.2f",
            "Gaussian σ for each EQ knob's influence.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);

        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---------------- Section: Directional Intensity --------------------------

static void section_directional(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    section_title("Directional Intensity");

    const float dir_amp = cfg.frequency.amplifier_direction;
    const char* const labels[8] = { "F", "FR", "R", "BR", "B", "BL", "L", "FL" };
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    // Rose visualization (compact, on the left).
    const float side = std::min(140.0f, ImGui::GetContentRegionAvail().x * 0.4f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + side * 0.5f, origin.y + side * 0.5f);
    const float radius = side * 0.5f;
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, radius, IM_COL32(30, 30, 30, 128), 64);
    dl->AddCircle(center, radius, kColorOutline, 64, 1.0f);
    constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
    for (int i = 0; i < 8; ++i) {
        const float v = std::clamp(snap.direction8[i] * dir_amp, 0.0f, 1.0f);
        const float a0 = (two_pi / 8.0f) * (i - 0.5f);
        const float a1 = (two_pi / 8.0f) * (i + 0.5f);
        const float r  = radius * v;
        const ImVec2 p0(center.x + r * cosf(a0), center.y + r * sinf(a0));
        const ImVec2 p1(center.x + r * cosf(a1), center.y + r * sinf(a1));
        dl->AddTriangleFilled(center, p0, p1, fill);
    }
    ImGui::Dummy(ImVec2(side, side));

    // Per-direction thin bars on the right of the rose.
    ImGui::SameLine();
    ImGui::BeginGroup();
    for (int i = 0; i < 8; ++i) {
        char buf[8]; std::snprintf(buf, sizeof(buf), "%s:", labels[i]);
        meter_row(buf, std::clamp(snap.direction8[i] * dir_amp, 0.0f, 1.0f), fill, "%.2f");
    }
    ImGui::EndGroup();

    if (settings_subtree("directional")) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Direction Boost", &cfg.frequency.amplifier_direction,
                        1.0f, 11.0f, "%.2f",
            "Visual-only multiplier on the directional uniforms.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---------------- Section: Chronotensity / AGC / Loudness ---------------

static void section_chronotensity(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    section_title("Phases, AGC & Loudness");

    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    subgroup_label("Phases (chronotensity):");
    ImGui::Indent(kSubGroupIndent);
    meter_row("Volume:", snap.phase_volume, fill);
    meter_row("Bass:",   snap.phase_bass,   fill);
    meter_row("Treble:", snap.phase_treble, fill);
    ImGui::Unindent(kSubGroupIndent);

    subgroup_label("AGC norm  (1.0 = recent average loudness):");
    ImGui::Indent(kSubGroupIndent);
    // AGC ratios live in [0, ~clamp_max]; show as 0..1 with /clamp_max scaling.
    const float scale = 1.0f / std::max(0.1f, cfg.agc.clamp_max);
    auto agc_row = [&](const char* lbl, float v) {
        meter_row(lbl, std::clamp(v * scale, 0.0f, 1.0f), fill, nullptr);
        ImGui::SameLine();
        ImGui::Text("%.2f", v);
    };
    agc_row("Volume:", snap.volume_norm);
    agc_row("Bass:",   snap.bass_norm);
    agc_row("Mid:",    snap.mid_norm);
    agc_row("Treble:", snap.treb_norm);
    ImGui::Unindent(kSubGroupIndent);

    subgroup_label("Other:");
    ImGui::Indent(kSubGroupIndent);
    meter_row("Spectral Centroid:", snap.spectral_centroid, fill, "%.3f");
    meter_row("Loudness (BS.1770):",
              std::clamp(snap.loudness, 0.0f, 1.0f), fill, "%.2f");
    ImGui::Unindent(kSubGroupIndent);

    if (settings_subtree("phases_agc")) {
        ImGui::Indent(kSubGroupIndent);

        subgroup_label("Chronotensity gains:");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Volume gain", &cfg.chronotensity.gain_volume, 0.0f, 5.0f, "%.2f",
            "How much volume_norm modulates phase_volume's rate.")) dirty = true;
        if (slider_row("Bass gain", &cfg.chronotensity.gain_bass, 0.0f, 5.0f, "%.2f",
            "Modulates phase_bass.")) dirty = true;
        if (slider_row("Treble gain", &cfg.chronotensity.gain_treble, 0.0f, 5.0f, "%.2f",
            "Modulates phase_treble.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);

        subgroup_label("AGC:");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Window (s)", &cfg.agc.window_seconds, 0.5f, 30.0f, "%.1f",
            "Running-mean window for AGC normalization.")) dirty = true;
        if (slider_row("Clamp max", &cfg.agc.clamp_max, 1.5f, 8.0f, "%.1f",
            "Upper bound on the normalized ratio.")) dirty = true;
        if (slider_row("Attack (ms)", &cfg.agc.att_attack_ms, 1.0f, 1000.0f, "%.0f",
            "Smoothing attack for *_att uniforms.")) dirty = true;
        if (slider_row("Release (ms)", &cfg.agc.att_release_ms, 1.0f, 5000.0f, "%.0f",
            "Smoothing release for *_att uniforms.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);

        subgroup_label("Loudness:");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Window (ms)", &cfg.loudness.window_ms, 50.0f, 3000.0f, "%.0f",
            "K-weighted RMS window. 400 ms = BS.1770 'momentary'.")) dirty = true;
        ImGui::Unindent(kSubGroupIndent);

        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---------------- Section: DSP profiler ----------------------------------

static void section_profiler(const AudioSnapshot& snap) {
    using namespace overlay_style;
    section_title("DSP Profiler");

    info_row("Pipeline:", "%.1f \xC2\xB5s total (EMA)", snap.pipeline_micros);

    if (snap.stage_count == 0) {
        ImGui::TextDisabled("(no timings yet)");
        return;
    }
    float max_micros = 1.0f;
    for (uint32_t i = 0; i < snap.stage_count; ++i) {
        max_micros = std::max(max_micros, snap.stage_timings[i].micros);
    }
    char buf[32];
    for (uint32_t i = 0; i < snap.stage_count; ++i) {
        const auto& t = snap.stage_timings[i];
        std::snprintf(buf, sizeof(buf), "%s:", t.name);
        // Use meter_row but with profiler color and µs value formatting.
        label_left(buf);
        const float frame_h = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float val_w = g_value_col_w;
        const float bar_w = std::max(8.0f, ImGui::GetContentRegionAvail().x - val_w - spacing);
        auto* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float y_off = (frame_h - kBarHeightThin) * 0.5f;
        const ImVec2 bg_tl(p.x, p.y + y_off);
        const ImVec2 bg_br(p.x + bar_w, bg_tl.y + kBarHeightThin);
        dl->AddRectFilled(bg_tl, bg_br, kColorBg, kBarRounding);
        const float v = std::clamp(t.micros / max_micros, 0.0f, 1.0f);
        if (v > 0.0f) {
            dl->AddRectFilled(bg_tl, ImVec2(p.x + v * bar_w, bg_br.y),
                              kColorProfiler, kBarRounding);
        }
        dl->AddRect(bg_tl, bg_br, kColorOutline, kBarRounding);
        ImGui::Dummy(ImVec2(bar_w, frame_h));
        ImGui::SameLine();
        ImGui::Text("%.1f \xC2\xB5s", t.micros);
    }
}

// ---------------- Section: Settings management ---------------------------

static void section_settings_mgmt(config::Store& store, config::Settings& cfg,
                                   AudioSystem& system) {
    section_title("Settings Management");

    ImGui::Columns(3, "##settings_buttons", false);
    if (ImGui::Button("Save to disk", ImVec2(-1, 0)))    store.save();
    ImGui::NextColumn();
    if (ImGui::Button("Reload from disk", ImVec2(-1, 0))) store.load();
    ImGui::NextColumn();
    if (ImGui::Button("Reset to defaults", ImVec2(-1, 0))) {
        store.publish(config::Settings{});
        store.save();
    }
    ImGui::Columns(1);

    if (ImGui::Button("Restart audio pipeline", ImVec2(-1, 0))) {
        system.switch_source(cfg.audio.capture_source_code);
    }
    tip("Stop and restart the active source (useful after FFT size or band count changes).");

    ImGui::TextDisabled("Config file: %s", store.path().u8string().c_str());
}

// ---------------- Top-level entry ----------------------------------------

void draw_overlay(reshade::api::effect_runtime*,
                  AudioSystem& system,
                  config::Store& store) {
    const auto snap = system.snapshot();
    auto cfg = store.snapshot();
    bool dirty = false;

    compute_columns();

    section_system(system, cfg, dirty);
    section_levels(snap, cfg, dirty);
    section_beat(snap, cfg, dirty);
    section_frequency(snap, cfg, dirty);
    section_directional(snap, cfg, dirty);
    section_chronotensity(snap, cfg, dirty);
    section_profiler(snap);
    section_settings_mgmt(store, cfg, system);

    if (dirty) {
        store.publish(cfg);
    }
}

}  // namespace lw
