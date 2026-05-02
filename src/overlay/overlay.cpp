// ---------------------------------------------
// v2 overlay. Matches the v1 visual feel: thin progress bars for meters,
// very compact stacked frequency band bars with red→green gradient,
// pan bar with center marker, collapsible sections by concern. Adds the
// per-stage DSP profiler and a full settings panel that exposes every
// tunable in the v2 Settings struct.
// ---------------------------------------------
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>

#include "overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "../audio/pipeline/audio_system.h"
#include "../config/store.h"

namespace lw {

// ----- Visual constants (mirrors v1's `OVERLAY_*` set) -------------------
namespace overlay_style {
constexpr float kBarHeightThin       = 6.0f;
constexpr float kBarHeightSubMeter   = 5.0f;   // L/R, profiler bars
constexpr float kBarSpacingSmall     = 2.0f;
constexpr float kBarSpacingLarge     = 4.0f;
constexpr float kBarRounding         = 0.0f;
constexpr float kPanCenterThickness  = 1.0f;

constexpr ImU32 kColorBg            = IM_COL32(40, 40, 40, 128);
constexpr ImU32 kColorOutline       = IM_COL32(60, 60, 60, 128);
constexpr ImU32 kColorCenterMarker  = IM_COL32(255, 255, 255, 180);
constexpr ImU32 kColorBeatAccent    = IM_COL32(255, 130,  60, 220);
constexpr ImU32 kColorProfiler      = IM_COL32(120, 200, 255, 200);
}  // namespace overlay_style

namespace {

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

// Tooltip helper: matches v1's "show on hover" idiom.
void tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

// Thin meter: a solid filled bar over a dark background, no rounding.
void thin_meter(float value, float width, float height, ImU32 fill_color) {
    using namespace overlay_style;
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 bg_max(p.x + width, p.y + height);
    dl->AddRectFilled(p, bg_max, kColorBg, kBarRounding);
    if (value > 0.0f) {
        const ImVec2 fill_max(p.x + std::clamp(value, 0.0f, 1.0f) * width, p.y + height);
        dl->AddRectFilled(p, fill_max, fill_color, kBarRounding);
    }
    dl->AddRect(p, bg_max, kColorOutline, kBarRounding);
    ImGui::Dummy(ImVec2(width, height));
}

// Center-anchored bar (pan / signed value): negative grows left, positive right.
void center_bar(float value, float width, float height) {
    using namespace overlay_style;
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float center_x = p.x + width * 0.5f;
    const ImVec2 bg_max(p.x + width, p.y + height);
    dl->AddRectFilled(p, bg_max, kColorBg, kBarRounding);

    const float v = std::clamp(value, -1.0f, 1.0f);
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    if (v < 0.0f) {
        const float w = -v * width * 0.5f;
        dl->AddRectFilled(ImVec2(center_x - w, p.y),
                          ImVec2(center_x, p.y + height), fill, kBarRounding);
    } else if (v > 0.0f) {
        const float w = v * width * 0.5f;
        dl->AddRectFilled(ImVec2(center_x, p.y),
                          ImVec2(center_x + w, p.y + height), fill, kBarRounding);
    }
    dl->AddLine(ImVec2(center_x, p.y), ImVec2(center_x, p.y + height),
                kColorCenterMarker, kPanCenterThickness);
    dl->AddRect(p, bg_max, kColorOutline, kBarRounding);
    ImGui::Dummy(ImVec2(width, height));
}

// Compact frequency-band display: stacked thin bars, red→green gradient
// by index (low frequencies red, highs green). Direct copy of v1's idiom.
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
            25 + static_cast<int>(230 * (1.0f - t)),  // R: high at low-i
            25 + static_cast<int>(230 * t),            // G: high at high-i
            230, 255);
        const ImVec2 bar_tl(start.x, start.y + i * bar_h);
        const ImVec2 bar_br(start.x + v * width, bar_tl.y + bar_h);
        dl->AddRectFilled(bar_tl, bar_br, col, kBarRounding);
        dl->AddRect(bar_tl, ImVec2(start.x + width, bar_tl.y + bar_h),
                    kColorOutline, kBarRounding);
    }
    ImGui::Dummy(ImVec2(width, bar_h * static_cast<float>(n)));
}

// Slider helpers (so each Setting line is one call site).
bool slider_f(const char* label, float* v, float lo, float hi,
              const char* fmt = "%.2f", const char* tooltip = nullptr) {
    bool changed = ImGui::SliderFloat(label, v, lo, hi, fmt);
    if (tooltip) tip(tooltip);
    return changed;
}
bool slider_i(const char* label, int* v, int lo, int hi,
              const char* tooltip = nullptr) {
    bool changed = ImGui::SliderInt(label, v, lo, hi);
    if (tooltip) tip(tooltip);
    return changed;
}

}  // namespace

// ---------------- Sections -----------------------------------------------

static void draw_system_and_source(AudioSystem& system, config::Settings& cfg, bool& dirty) {
    ImGui::Text("State: %s", state_label(system.state()));
    ImGui::SameLine();
    ImGui::TextDisabled(" • Listeningway v2.0.0-beta");

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
    ImGui::PushItemWidth(-1);
    if (ImGui::Combo("##source", &sel, name_ptrs.data(), (int)name_ptrs.size())
        && sel >= 0 && sel < (int)sources.size()) {
        // switch_source persists the change directly via Store::mutate+save,
        // so we don't double-publish here.
        system.switch_source(sources[sel].code);
    }
    ImGui::PopItemWidth();
    tip("Choose audio capture source. 'Off' disables analysis.");

    if (ImGui::Checkbox("Enable SIMD (SSE/AVX)", &cfg.audio.simd_enabled)) dirty = true;
    tip("Use SIMD intrinsics in DSP stages where available. Disable to test scalar paths.");

    if (ImGui::Checkbox("Debug logging", &cfg.debug.debug_logging)) dirty = true;
    tip("Verbose log to listeningway.log. Disabled by default.");
}

static void draw_levels_and_beat(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;

    // Layout: label + bar with a fixed bar_start_x so all rows align.
    const float label_w = ImGui::CalcTextSize("Loudness:").x;
    const float bar_start_x = ImGui::GetCursorPosX() + label_w + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float bar_width = ImGui::GetContentRegionAvail().x - (bar_start_x - ImGui::GetCursorPosX());

    auto label_then = [&](const char* lbl) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", lbl);
        ImGui::SameLine(bar_start_x);
    };

    const float vol_amp = cfg.frequency.amplifier_volume;
    label_then("Volume:");
    ImGui::ProgressBar(std::clamp(snap.volume * vol_amp, 0.0f, 1.0f),
                       ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", snap.volume * vol_amp);

    // L/R compact bars under the main volume.
    const float small_spacing = kBarSpacingSmall;
    const float half_w = (bar_width - small_spacing) * 0.5f;
    ImGui::Dummy(ImVec2(0, kBarSpacingSmall));
    ImGui::Dummy(ImVec2(0, 0)); ImGui::SameLine(bar_start_x);
    const ImVec2 lr_pos = ImGui::GetCursorScreenPos();
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    auto* dl = ImGui::GetWindowDrawList();
    // Left grows from center → left
    {
        const float center_x = lr_pos.x + bar_width * 0.5f;
        const ImVec2 bg_min(lr_pos.x, lr_pos.y);
        const ImVec2 bg_max(center_x - small_spacing * 0.5f, lr_pos.y + kBarHeightSubMeter);
        dl->AddRectFilled(bg_min, bg_max, kColorBg, kBarRounding);
        const float lv = std::clamp(snap.volume_left * vol_amp, 0.0f, 1.0f);
        if (lv > 0.0f) {
            dl->AddRectFilled(
                ImVec2(bg_max.x - lv * half_w, lr_pos.y),
                ImVec2(bg_max.x, lr_pos.y + kBarHeightSubMeter), fill, kBarRounding);
        }
        // Right grows from center → right
        const ImVec2 bg2_min(center_x + small_spacing * 0.5f, lr_pos.y);
        const ImVec2 bg2_max(lr_pos.x + bar_width, lr_pos.y + kBarHeightSubMeter);
        dl->AddRectFilled(bg2_min, bg2_max, kColorBg, kBarRounding);
        const float rv = std::clamp(snap.volume_right * vol_amp, 0.0f, 1.0f);
        if (rv > 0.0f) {
            dl->AddRectFilled(
                ImVec2(bg2_min.x, lr_pos.y),
                ImVec2(bg2_min.x + rv * half_w, lr_pos.y + kBarHeightSubMeter),
                fill, kBarRounding);
        }
    }
    ImGui::Dummy(ImVec2(bar_width, kBarHeightSubMeter));
    ImGui::Dummy(ImVec2(0, kBarSpacingLarge));

    // Pan bar (center-anchored).
    label_then("Pan:");
    center_bar(snap.audio_pan, bar_width, kBarHeightThin);
    ImGui::SameLine();
    ImGui::Text("%+0.2f", snap.audio_pan);

    // Volume amp slider (visual-only).
    label_then("Vol Boost:");
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##VolAmp", &cfg.frequency.amplifier_volume,
                            1.0f, 11.0f, "%.2f")) dirty = true;
    ImGui::PopItemWidth();
    tip("Visual-only multiplier on the volume uniforms (does not affect analysis).");

    label_then("Pan Smooth:");
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##PanSmooth", &cfg.audio.pan_smoothing,
                            0.0f, 1.0f, "%.2f")) dirty = true;
    ImGui::PopItemWidth();
    tip("Reduces pan jitter. 0 = no smoothing.");

    label_then("Pan Offset:");
    ImGui::PushItemWidth(bar_width);
    if (ImGui::SliderFloat("##PanOff", &cfg.audio.pan_offset,
                            -1.0f, 1.0f, "%.2f")) dirty = true;
    ImGui::PopItemWidth();
    tip("User pan bias (-1..+1).");

    ImGui::Dummy(ImVec2(0, kBarSpacingLarge));

    // Beat row.
    label_then("Beat:");
    ImGui::ProgressBar(std::clamp(snap.beat, 0.0f, 1.0f),
                       ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", snap.beat);

    label_then("Beat Phase:");
    ImGui::ProgressBar(std::clamp(snap.beat_phase, 0.0f, 1.0f),
                       ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", snap.beat_phase);

    label_then("Tempo:");
    if (snap.tempo_detected) {
        ImGui::Text("%.1f BPM (%.0f%% confidence)",
                    snap.tempo_bpm, snap.tempo_confidence * 100.0f);
    } else {
        ImGui::TextDisabled("unlocked (%.1f BPM, %.0f%%)",
                            snap.tempo_bpm, snap.tempo_confidence * 100.0f);
    }

    label_then("Loudness:");
    ImGui::ProgressBar(std::clamp(snap.loudness, 0.0f, 1.0f),
                       ImVec2(bar_width, 0.0f));
    ImGui::SameLine();
    ImGui::Text("%.2f", snap.loudness);
    tip("K-weighted (BS.1770) momentary loudness over a 400 ms window.");

    label_then("Format:");
    ImGui::Text("%s (%.0f ch)", format_label(static_cast<int>(snap.audio_format)),
                snap.audio_format);
}

static void draw_frequency(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    const uint32_t n = std::min<uint32_t>(snap.freq_band_count, 64);
    const float bands_amp = cfg.frequency.amplifier_bands;

    ImGui::Text("Frequency Bands  (%u)", n);
    ImGui::BeginChild("##bands_compact",
                      ImVec2(0.0f, kBarHeightThin * static_cast<float>(n) + 12.0f),
                      true, ImGuiWindowFlags_NoScrollbar);
    compact_band_bars(std::span<const float>(snap.freq_bands.data(), n),
                      bands_amp, ImGui::GetContentRegionAvail().x);
    ImGui::EndChild();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bands Boost:");
    ImGui::SameLine();
    ImGui::PushItemWidth(-1);
    if (ImGui::SliderFloat("##BandAmp", &cfg.frequency.amplifier_bands,
                            1.0f, 11.0f, "%.2f")) dirty = true;
    ImGui::PopItemWidth();
    tip("Visual-only multiplier on the bands uniforms.");

    if (ImGui::TreeNode("Band mapping")) {
        const char* const scales[] = {"Linear", "Log", "Mel (Slaney)"};
        int scale = static_cast<int>(cfg.frequency.band_scale);
        ImGui::PushItemWidth(-1);
        if (ImGui::Combo("##scale", &scale, scales, 3)) {
            cfg.frequency.band_scale = static_cast<config::FrequencyConfig::BandScale>(scale);
            dirty = true;
        }
        ImGui::PopItemWidth();
        tip("Mel matches perception best; Log was the v1 default; Linear is legacy.");

        if (slider_i("Band count", &cfg.frequency.band_count, 8, 128,
                      "Live band count published. Cap is kMaxBands.")) dirty = true;
        if (slider_i("FFT size", &cfg.frequency.fft_size, 256, 8192,
                      "Power-of-two recommended. Larger = finer resolution, more CPU.")) dirty = true;
        if (slider_f("Min freq", &cfg.frequency.min_freq, 10.0f, 500.0f, "%.0f",
                      "Lowest band edge in Hz.")) dirty = true;
        if (slider_f("Max freq", &cfg.frequency.max_freq, 2000.0f, 22050.0f, "%.0f",
                      "Highest band edge in Hz.")) dirty = true;
        if (slider_f("Log strength", &cfg.frequency.log_strength, 0.01f, 1.5f, "%.2f",
                      "Gain curve over band index when log scale enabled.")) dirty = true;
        if (slider_f("Band norm", &cfg.frequency.band_norm, 0.001f, 1.0f, "%.3f",
                      "Raw-magnitude → band-amplitude scaling.")) dirty = true;
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Equalizer (5-band, Gaussian)")) {
        const char* const names[5] = {"Low (Bass)", "Low-Mid", "Mid", "Mid-High", "High (Treble)"};
        for (int i = 0; i < 5; ++i) {
            char id[16]; std::snprintf(id, sizeof(id), "##eq%d", i);
            if (ImGui::SliderFloat(id, &cfg.frequency.equalizer_bands[i],
                                    0.0f, 4.0f, "%.2f")) dirty = true;
            ImGui::SameLine(); ImGui::Text("%s", names[i]);
        }
        if (slider_f("Width", &cfg.frequency.equalizer_width, 0.05f, 0.5f, "%.2f",
                      "Gaussian σ for each EQ knob's influence.")) dirty = true;
        ImGui::TreePop();
    }
}

static void draw_directional(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    // Rose + 8 thin bars side-by-side. Rose is in v1, but a thin-bar list
    // with labels reads better at compact sizes — provide both.
    const float dir_amp = cfg.frequency.amplifier_direction;
    const char* const labels[8] = { "F", "FR", "R", "BR", "B", "BL", "L", "FL" };

    // Rose visualization (compact).
    const float side = std::min(140.0f, ImGui::GetContentRegionAvail().x * 0.5f);
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
        dl->AddTriangleFilled(center, p0, p1,
            ImGui::GetColorU32(ImGuiCol_PlotHistogram));
    }
    ImGui::Dummy(ImVec2(side, side));

    // Thin bars list with labels (compact alternative).
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float bar_w = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < 8; ++i) {
        ImGui::Text("%-3s", labels[i]);
        ImGui::SameLine(28.0f);
        thin_meter(std::clamp(snap.direction8[i] * dir_amp, 0.0f, 1.0f),
                    bar_w - 28.0f, kBarHeightThin, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
    }
    ImGui::EndGroup();

    if (ImGui::SliderFloat("Direction Boost",
                            &cfg.frequency.amplifier_direction,
                            1.0f, 11.0f, "%.2f")) dirty = true;
    tip("Visual-only multiplier on the directional uniforms.");
}

static void draw_chronotensity_and_phases(const AudioSnapshot& snap,
                                          config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    const float w = ImGui::GetContentRegionAvail().x;
    auto* dl = ImGui::GetWindowDrawList();
    auto phase_bar = [&](const char* label, float v) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%-14s", label);
        ImGui::SameLine();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bar_w = w - ImGui::GetCursorPosX() + ImGui::GetStyle().WindowPadding.x;
        const ImVec2 max_pt(p.x + bar_w, p.y + kBarHeightThin);
        dl->AddRectFilled(p, max_pt, kColorBg, kBarRounding);
        const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
        dl->AddRectFilled(p, ImVec2(p.x + std::clamp(v, 0.0f, 1.0f) * bar_w, p.y + kBarHeightThin),
                          fill, kBarRounding);
        dl->AddRect(p, max_pt, kColorOutline, kBarRounding);
        ImGui::Dummy(ImVec2(bar_w, kBarHeightThin));
        ImGui::SameLine();
        ImGui::Text("%.2f", v);
    };
    phase_bar("phase_volume", snap.phase_volume);
    phase_bar("phase_bass",   snap.phase_bass);
    phase_bar("phase_treble", snap.phase_treble);

    ImGui::Text("AGC norm (1.0 = average loudness)");
    phase_bar("volume_norm",  std::min(snap.volume_norm * 0.25f, 1.0f));
    phase_bar("bass_norm",    std::min(snap.bass_norm   * 0.25f, 1.0f));
    phase_bar("mid_norm",     std::min(snap.mid_norm    * 0.25f, 1.0f));
    phase_bar("treb_norm",    std::min(snap.treb_norm   * 0.25f, 1.0f));

    ImGui::Text("Spectral centroid: %.3f", snap.spectral_centroid);

    if (ImGui::TreeNode("Chronotensity gains")) {
        if (slider_f("Volume gain", &cfg.chronotensity.gain_volume, 0.0f, 5.0f, "%.2f",
                      "How much volume_norm modulates phase_volume's rate.")) dirty = true;
        if (slider_f("Bass gain", &cfg.chronotensity.gain_bass, 0.0f, 5.0f, "%.2f",
                      "Modulates phase_bass.")) dirty = true;
        if (slider_f("Treble gain", &cfg.chronotensity.gain_treble, 0.0f, 5.0f, "%.2f",
                      "Modulates phase_treble.")) dirty = true;
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("AGC settings")) {
        if (slider_f("Window (s)", &cfg.agc.window_seconds, 0.5f, 30.0f, "%.1f",
                      "Running-mean window for AGC normalization.")) dirty = true;
        if (slider_f("Clamp max",  &cfg.agc.clamp_max,      1.5f, 8.0f,  "%.1f",
                      "Upper bound on the normalized ratio.")) dirty = true;
        if (slider_f("Attack (ms)", &cfg.agc.att_attack_ms,  1.0f, 1000.0f, "%.0f",
                      "Smoothing attack for *_att uniforms.")) dirty = true;
        if (slider_f("Release (ms)", &cfg.agc.att_release_ms, 1.0f, 5000.0f, "%.0f",
                      "Smoothing release for *_att uniforms.")) dirty = true;
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Loudness")) {
        if (slider_f("Window (ms)", &cfg.loudness.window_ms, 50.0f, 3000.0f, "%.0f",
                      "K-weighted RMS window. 400 ms = BS.1770 'momentary'.")) dirty = true;
        ImGui::TreePop();
    }
}

static void draw_beat_settings(config::Settings& cfg, bool& dirty) {
    if (slider_f("Threshold lambda", &cfg.beat.threshold_lambda, 0.0f, 1.0f, "%.3f",
                  "Aubio-formula λ in `median + λ·mean` adaptive threshold.")) dirty = true;
    if (slider_f("Threshold window (ms)", &cfg.beat.threshold_window_ms, 10.0f, 1000.0f, "%.0f",
                  "Sliding window for adaptive threshold computation.")) dirty = true;
    if (slider_f("Refractory (ms)", &cfg.beat.refractory_ms, 5.0f, 500.0f, "%.0f",
                  "Minimum interval between detected onsets (suppresses doubles).")) dirty = true;
    if (slider_f("Phase k_p", &cfg.beat.phase_kp, 0.0f, 1.0f, "%.3f",
                  "PLL phase-pull strength on confident detected onsets.")) dirty = true;
    if (slider_f("Phase k_i", &cfg.beat.phase_ki, 0.0f, 0.5f, "%.4f",
                  "PLL BPM-drift correction strength.")) dirty = true;
    if (slider_f("Tempo prior BPM", &cfg.beat.tempo_prior_bpm, 60.0f, 200.0f, "%.0f",
                  "Center of the log-Gaussian tempo prior (mitigates octave errors).")) dirty = true;
    if (slider_f("Tempo prior σ (oct)", &cfg.beat.tempo_prior_sigma, 0.1f, 2.0f, "%.2f",
                  "Width of the tempo prior in octaves.")) dirty = true;
    if (slider_f("Tempo window (s)", &cfg.beat.tempo_window_sec, 1.0f, 30.0f, "%.1f",
                  "Autocorrelation history length.")) dirty = true;
    if (slider_f("Beat decay /s", &cfg.beat.beat_decay_per_sec, 0.1f, 10.0f, "%.2f",
                  "Decay rate for the `beat` uniform after a detected onset.")) dirty = true;
}

static void draw_profiler(const AudioSnapshot& snap) {
    using namespace overlay_style;
    ImGui::Text("Pipeline: %.1f µs total (EMA)", snap.pipeline_micros);

    if (snap.stage_count == 0) {
        ImGui::TextDisabled("(no timings yet)");
        return;
    }
    // Find the worst-case µs across stages so the bars are scaled to it.
    float max_micros = 1.0f;
    for (uint32_t i = 0; i < snap.stage_count; ++i) {
        max_micros = std::max(max_micros, snap.stage_timings[i].micros);
    }

    const float name_w = 130.0f;
    const float val_w  = 60.0f;
    const float bar_w  = std::max(40.0f, ImGui::GetContentRegionAvail().x - name_w - val_w - 12.0f);

    for (uint32_t i = 0; i < snap.stage_count; ++i) {
        const auto& t = snap.stage_timings[i];
        ImGui::Text("%-22s", t.name);
        ImGui::SameLine(name_w);
        thin_meter(t.micros / max_micros, bar_w, kBarHeightThin, kColorProfiler);
        ImGui::SameLine();
        ImGui::Text("%.1f µs", t.micros);
    }
}

static void draw_settings_management(config::Store& store, config::Settings& cfg,
                                      AudioSystem& system, bool& dirty) {
    if (dirty) {
        // Live updates immediately so DSP threads pick up changes on next frame.
        store.publish(cfg);
        dirty = false;
    }

    ImGui::Columns(3, "##settings_buttons", false);
    if (ImGui::Button("Save to disk", ImVec2(-1, 0))) {
        store.save();
    }
    ImGui::NextColumn();
    if (ImGui::Button("Reload from disk", ImVec2(-1, 0))) {
        store.load();
    }
    ImGui::NextColumn();
    if (ImGui::Button("Reset to defaults", ImVec2(-1, 0))) {
        store.publish(config::Settings{});
        store.save();
    }
    ImGui::Columns(1);

    ImGui::TextDisabled("Config file: %s", store.path().u8string().c_str());
    if (ImGui::Button("Restart audio pipeline", ImVec2(-1, 0))) {
        const auto code = cfg.audio.capture_source_code;
        system.switch_source(code);
    }
    tip("Stop and restart the active source. Useful after changing FFT size or band count.");
}

// ---------------- Top-level entry ----------------------------------------

void draw_overlay(reshade::api::effect_runtime*,
                  AudioSystem& system,
                  config::Store& store) {
    const auto snap = system.snapshot();
    auto cfg = store.snapshot();
    bool dirty = false;

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("System & Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_system_and_source(system, cfg, dirty);
    }
    ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Levels & Beat", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_levels_and_beat(snap, cfg, dirty);
    }
    ImGui::Separator();

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader("Frequency Bands", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_frequency(snap, cfg, dirty);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Directional Intensity")) {
        draw_directional(snap, cfg, dirty);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Chronotensity / AGC / Loudness")) {
        draw_chronotensity_and_phases(snap, cfg, dirty);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Beat detection settings")) {
        draw_beat_settings(cfg, dirty);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("DSP profiler")) {
        draw_profiler(snap);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Settings management", ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_settings_management(store, cfg, system, dirty);
    }
    // Final flush in case dirty was set inside the management section.
    if (dirty) {
        store.publish(cfg);
    }
}

}  // namespace lw
