// ---------------------------------------------
// v2 overlay — UX-pass edition.
//
// Layout philosophy:
//   - Each section's header carries the title plus a live hint (e.g.
//     "Spectrum (64 bands)" or "Beat (128 BPM)") and, when the section
//     has settings, a right-aligned "Settings" toggle on the same line.
//     One line of header instead of three; live data where you'd glance.
//   - Visual content (meters, bars, rose) is always shown. Only the
//     tunable knobs hide behind the Settings disclosure.
//   - Engineer-only knobs hide further behind an "Advanced" sub-disclosure
//     so newcomers see only the four or five settings they actually want.
//   - Tooltips lead with what the control DOES; technical name and units
//     live in a "Technical:" footer at the bottom of the tooltip.
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
#include <string_view>
#include <vector>

#include "../audio/pipeline/audio_system.h"
#include "../config/store.h"
#include "../output/consumer_registry.h"
#include "../output/i_output_consumer.h"

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

constexpr ImU32 kColorIntegrationOn        = IM_COL32(60, 160, 75, 255);
constexpr ImU32 kColorIntegrationOnHover   = IM_COL32(80, 190, 95, 255);
constexpr ImU32 kColorIntegrationOnActive  = IM_COL32(50, 140, 65, 255);
}  // namespace overlay_style

namespace {

float g_label_col = 0.0f;
float g_value_col_w = 0.0f;

constexpr float kLabelGap = 8.0f;  // padding between label's right edge and bar's left edge

void compute_columns() {
    // Probe strings without trailing colons (labels render colon-less now).
    // Picked the widest label currently in use across all sections so the
    // bar-start column accommodates everything without clipping.
    const char* probes[] = {
        "Threshold window (ms)",
        "Smoothing release (ms)",
        "Beat position",
        "Brightness",
    };
    float widest = 0.0f;
    for (const char* p : probes) widest = std::max(widest, ImGui::CalcTextSize(p).x);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    g_label_col   = ImGui::GetCursorPosX() + widest + spacing + kLabelGap;
    g_value_col_w = ImGui::CalcTextSize("99.99 \xC2\xB5s").x + spacing;
}

void tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

// Right-align the label so its right edge sits kLabelGap pixels to the left
// of g_label_col. Then jump to g_label_col so the bar starts there.
// Colons are no longer expected in the label text.
//
// Uses SetCursorPosX rather than SameLine(g_label_col) for the bar-column
// jump because SameLine(N) inside an active BeginGroup adds the group's
// X-offset to N, double-counting and pushing the bar far past the
// intended column. SetCursorPosX is group-offset-agnostic and treats its
// argument as an absolute window-content-relative position.
void label_left(const char* label) {
    ImGui::AlignTextToFramePadding();
    const float label_w = ImGui::CalcTextSize(label).x;
    const float target_x = g_label_col - kLabelGap - label_w;
    if (target_x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target_x);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(g_label_col);
}

// ---- Row helpers (unchanged semantics) ---------------------------------

void meter_row(const char* label, float value, ImU32 fill,
               const char* value_fmt = "%.2f") {
    using namespace overlay_style;
    label_left(label);
    const float frame_h = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
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

void info_row(const char* label, const char* fmt, ...) {
    label_left(label);
    va_list ap; va_start(ap, fmt);
    ImGui::TextV(fmt, ap);
    va_end(ap);
}

// ---- Section header helpers --------------------------------------------

// RAII: tighter vertical metrics in panels of consecutive meter_row bars
// (Advanced, Performance). Shrinks frame padding and item spacing so rows
// stack ~8 px tighter without changing each bar's own visual style. Scope
// it only around the meter-row groups so sliders and headers keep their
// natural rhythm.
class TightRowSpacing {
public:
    TightRowSpacing() {
        const ImVec2 fp = ImGui::GetStyle().FramePadding;
        const ImVec2 sp = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(fp.x, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(sp.x, 0.0f));
    }
    ~TightRowSpacing() { ImGui::PopStyleVar(2); }
    TightRowSpacing(const TightRowSpacing&)            = delete;
    TightRowSpacing& operator=(const TightRowSpacing&) = delete;
};

// RAII: scope a narrower bar-start column when a section's labels are
// shorter than the overlay-wide widest probe. By default g_label_col is
// computed from probes like "Smoothing release (ms)", which leaves a huge
// dead zone before the bars when the actual labels are tiny ("F", "BR").
// This helper recomputes g_label_col from the supplied labels at the
// current cursor position, then restores it on scope exit.
class LocalLabelColumn {
public:
    LocalLabelColumn(std::span<const char* const> labels) : prev_(g_label_col) {
        float widest = 0.0f;
        for (const char* l : labels) widest = std::max(widest, ImGui::CalcTextSize(l).x);
        g_label_col = ImGui::GetCursorPosX() + widest
                    + ImGui::GetStyle().ItemSpacing.x + kLabelGap;
    }
    ~LocalLabelColumn() { g_label_col = prev_; }
    LocalLabelColumn(const LocalLabelColumn&)            = delete;
    LocalLabelColumn& operator=(const LocalLabelColumn&) = delete;
private:
    float prev_;
};

// Right-align cursor for a label of given pixel width.
void cursor_to_right_for(float label_w_with_padding) {
    const float right_x = ImGui::GetWindowContentRegionMax().x - label_w_with_padding;
    ImGui::SameLine();
    if (right_x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right_x);
}

// Subdued "settings ·/●" disclosure. Transparent background by default,
// subtle white tint on hover. Closed state shows a small middle dot;
// open state fills it in. The whole widget reads as quiet metadata until
// the user reaches for it. Toggles `open` on click; returns the new state.
//
// Caller is responsible for the right-align cursor positioning and for
// wrapping in PushID/PopID so the SmallButton's ID is unique per section.
bool subtle_settings_toggle(bool& open) {
    // ImGui's default font ships only Latin-1 in its glyph atlas, so
    // geometric-shape codepoints (U+25CF ● etc.) render as "?". The
    // middle dot · (U+00B7) is in Latin-1 and renders fine; we use a
    // plain ASCII '*' for the filled (open) state.
    //   Closed: "settings ·"
    //   Open:   "settings *"
    const char* label = open ? "settings *" : "settings \xC2\xB7";

    const float w = ImGui::CalcTextSize(label).x
                  + ImGui::GetStyle().FramePadding.x * 2.0f;
    cursor_to_right_for(w);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(4);
    if (clicked) open = !open;
    return open;
}

// Render a section header with optional dim hint and a right-aligned
// Settings disclosure. Returns the disclosure's open/closed state.
// `id` MUST be unique per section (used for ImGui state storage).
bool section_header_with_settings(const char* title, const char* hint, const char* id) {
    ImGui::PushID(id);
    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    if (hint && hint[0]) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("(%s)", hint);
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID state_key = ImGui::GetID("settings_open");
    bool open = storage->GetBool(state_key, false);
    subtle_settings_toggle(open);
    storage->SetBool(state_key, open);

    ImGui::Separator();
    ImGui::PopID();
    return open;
}

// Section header for sections that don't carry per-section settings
// (Performance, Integrations — Integrations has per-row settings instead).
void section_header_only(const char* title, const char* hint) {
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    if (hint && hint[0]) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("(%s)", hint);
    }
    ImGui::Separator();
}

// Sub-group label inside a section.
void subgroup_label(const char* label) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", label);
}


// ---- Integration row (Integrations section) ----------------------------

// Renders one integration: [Name] toggle button, status text, right-aligned
// Settings disclosure. Returns true if per-integration settings should be
// drawn below the row. The toggle button is highlighted when enabled.
bool integration_row(const char* name, bool& enabled, bool& dirty,
                     std::string_view status, const char* id) {
    ImGui::PushID(id);
    ImGui::Indent(overlay_style::kSubGroupIndent);

    char btn_label[40];
    std::snprintf(btn_label, sizeof(btn_label), "%s", name);

    if (enabled) {
        ImGui::PushStyleColor(ImGuiCol_Button,        overlay_style::kColorIntegrationOn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, overlay_style::kColorIntegrationOnHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  overlay_style::kColorIntegrationOnActive);
    }
    if (ImGui::Button(btn_label)) {
        enabled = !enabled;
        dirty = true;
    }
    if (enabled) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to %s.", enabled ? "disable" : "enable");
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (status.empty()) {
        ImGui::TextDisabled("Off");
    } else {
        // Use std::string for null-terminated TextDisabled().
        const std::string s(status);
        ImGui::TextDisabled("%s", s.c_str());
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID state_key = ImGui::GetID("settings_open");
    bool open = storage->GetBool(state_key, false);
    subtle_settings_toggle(open);
    storage->SetBool(state_key, open);

    ImGui::Unindent(overlay_style::kSubGroupIndent);
    ImGui::PopID();
    return open;
}

// ---- Common helpers -----------------------------------------------------

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

// Per-band peak hold buffer for the spectrum visualization. Sized for the
// max possible band count; only the first N entries are used at any time.
// Decays toward each frame's live amplitude so the user sees recent peaks
// trailing slightly above the live shape.
constexpr size_t kSpectrumMaxBands = 128;
struct SpectrumPeakState {
    std::array<float, kSpectrumMaxBands> peaks{};
    double                                last_t = 0.0;
};
SpectrumPeakState g_spectrum_peaks;

void update_peak_hold(std::span<const float> values, float amp) {
    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(std::clamp(now - g_spectrum_peaks.last_t, 0.0, 0.25));
    g_spectrum_peaks.last_t = now;
    constexpr float kDecayPerSec = 0.55f;     // ~half decay over ~1.3 s
    const float decay = std::exp(-kDecayPerSec * dt);
    for (size_t i = 0; i < values.size() && i < kSpectrumMaxBands; ++i) {
        const float v = std::clamp(values[i] * amp, 0.0f, 1.0f);
        float& p = g_spectrum_peaks.peaks[i];
        p = std::max(v, p * decay);
    }
}

// Map an anchor frequency to an X position [0, width] using the same band
// scale as the analysis pipeline. A visual approximation: bands are assumed
// to span (min_f, max_f) with the chosen scale's geometry. Linear and Log
// match exactly; Mel uses the standard 2595·log10(1+f/700) curve.
float freq_to_x(float f, config::FrequencyConfig::BandScale scale,
                float min_f, float max_f, float width) {
    using BandScale = config::FrequencyConfig::BandScale;
    if (max_f <= min_f) return 0.0f;
    f = std::clamp(f, min_f, max_f);
    float t = 0.0f;
    switch (scale) {
        case BandScale::Linear:
            t = (f - min_f) / (max_f - min_f);
            break;
        case BandScale::Log: {
            const float lo = std::log(std::max(min_f, 1.0f));
            const float hi = std::log(std::max(max_f, lo + 1.0f));
            t = (std::log(f) - lo) / (hi - lo);
            break;
        }
        case BandScale::Mel: {
            auto to_mel = [](float hz) {
                return 2595.0f * std::log10(1.0f + hz / 700.0f);
            };
            const float lo = to_mel(min_f);
            const float hi = to_mel(max_f);
            t = (to_mel(f) - lo) / (hi - lo);
            break;
        }
    }
    return std::clamp(t, 0.0f, 1.0f) * width;
}

// Per-band hue along the spectrum (warm low → cool high). Same gradient
// for both render orientations so the panel's identity is consistent.
ImU32 band_color(float t, int alpha = 255) {
    t = std::clamp(t, 0.0f, 1.0f);
    const int r = 25 + static_cast<int>(230.0f * (1.0f - t));
    const int g = 25 + static_cast<int>(230.0f * t);
    const int b = 230;
    return IM_COL32(r, g, b, alpha);
}

// ---- Spectrum: horizontal orientation (default) -------------------------
//
// Frequency on X (left = low, right = high), amplitude on Y (bottom = 0,
// top = 1). Each adjacent band pair fills a quad with the top edge linearly
// interpolated between band amplitudes, producing a smooth-looking shape at
// 64+ bands without explicit spline math. Per-band hue gradient gives the
// spectrum its identity. Peak-hold trace is drawn as a thin polyline above
// the live fill.
//
// Frequency-axis anchor labels (30 / 200 / 1k / 5k / 20k Hz) sit at the top
// of the pane. A faint solid vertical guide line drops from each label down
// to the baseline. The guide draws BEFORE the spectrum fill so the live
// shape covers it where loud and it shows through where quiet.
void render_spectrum_horizontal(std::span<const float> values, float amp,
                                  ImVec2 size,
                                  config::FrequencyConfig::BandScale scale,
                                  float min_f, float max_f) {
    using namespace overlay_style;
    if (values.empty() || size.x <= 0.0f || size.y <= 0.0f) return;

    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 br(origin.x + size.x, origin.y + size.y);

    // Pane background.
    dl->AddRectFilled(origin, br, kColorBg, kBarRounding);

    update_peak_hold(values, amp);

    const size_t n = values.size();
    if (n < 2) {
        dl->AddRect(origin, br, kColorOutline, kBarRounding);
        ImGui::Dummy(size);
        return;
    }

    // Reserve a small strip at the top for the frequency-axis labels so the
    // shape doesn't draw over them. Labels live in [origin.y, label_band).
    const float label_h    = ImGui::GetTextLineHeight();
    const float label_band = origin.y + label_h + 2.0f;

    // Frequency-axis anchors. Geometry is needed before the fill so the
    // guide lines can be drawn underneath.
    static const float kAnchors[] = { 30.0f, 200.0f, 1000.0f, 5000.0f, 20000.0f };
    static const char* const kAnchorLabels[] = { "30", "200", "1k", "5k", "20k" };
    const ImU32 guide_col = IM_COL32(70, 70, 70, 255);
    const ImU32 label_col = IM_COL32(255, 255, 255, 110);

    // Pass 1: vertical guide lines from the label band down to the baseline.
    // Drawn before the live fill so the spectrum covers them when loud.
    for (size_t i = 0; i < std::size(kAnchors); ++i) {
        const float f = kAnchors[i];
        if (f < min_f || f > max_f) continue;
        const float x = origin.x + freq_to_x(f, scale, min_f, max_f, size.x);
        dl->AddLine(ImVec2(x, label_band), ImVec2(x, br.y), guide_col, 1.0f);
    }

    // Per-band X centers spread across the full width.
    auto band_x = [&](size_t i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        return origin.x + t * size.x;
    };
    const float baseline = br.y;
    // Reserve only the label-band strip for axis labels; the spectrum can
    // still reach the very top of the remaining area at peak amplitude.
    const float draw_h = std::max(0.0f, baseline - label_band);
    auto band_y = [&](float v) {
        return baseline - std::clamp(v, 0.0f, 1.0f) * draw_h;
    };

    // Live fill: one convex quad per inter-band segment. Linear top edge
    // between adjacent band amplitudes; the eye reads the assembled shape
    // as smooth at 64+ bands.
    for (size_t i = 0; i + 1 < n; ++i) {
        const float v0 = std::clamp(values[i]     * amp, 0.0f, 1.0f);
        const float v1 = std::clamp(values[i + 1] * amp, 0.0f, 1.0f);
        const float t  = (static_cast<float>(i) + 0.5f) / static_cast<float>(n - 1);
        const ImU32 col = band_color(t);
        const ImVec2 quad[4] = {
            ImVec2(band_x(i),     baseline),
            ImVec2(band_x(i + 1), baseline),
            ImVec2(band_x(i + 1), band_y(v1)),
            ImVec2(band_x(i),     band_y(v0)),
        };
        dl->AddConvexPolyFilled(quad, 4, col);
    }

    // Peak-hold outline: a thin polyline traced through the per-band peaks.
    {
        std::array<ImVec2, kSpectrumMaxBands> pts{};
        const size_t k = std::min(n, kSpectrumMaxBands);
        for (size_t i = 0; i < k; ++i) {
            pts[i] = ImVec2(band_x(i), band_y(g_spectrum_peaks.peaks[i]));
        }
        dl->AddPolyline(pts.data(), static_cast<int>(k),
                        IM_COL32(255, 255, 255, 110), 0, 1.0f);
    }

    // Pass 2: frequency-axis labels at the top. Drawn after the fill so they
    // sit over anything that might bleed into their reserved strip.
    for (size_t i = 0; i < std::size(kAnchors); ++i) {
        const float f = kAnchors[i];
        if (f < min_f || f > max_f) continue;
        const float x = origin.x + freq_to_x(f, scale, min_f, max_f, size.x);
        const ImVec2 ts = ImGui::CalcTextSize(kAnchorLabels[i]);
        const float lx = std::clamp(x - ts.x * 0.5f, origin.x + 1.0f, br.x - ts.x - 1.0f);
        dl->AddText(ImVec2(lx, origin.y + 1.0f), label_col, kAnchorLabels[i]);
    }

    dl->AddRect(origin, br, kColorOutline, kBarRounding);
    ImGui::Dummy(size);
}

// ---- Spectrum: vertical orientation (alternate) -------------------------
//
// Bands stacked top-to-bottom, each filling the full pane width based on
// amplitude. Per-band height = pane_height / N (with leftover pixels
// distributed across the first few rows so the total fills the pane). Same
// per-band hue gradient as the horizontal mode.
void render_spectrum_vertical(std::span<const float> values, float amp,
                               ImVec2 size) {
    using namespace overlay_style;
    if (values.empty() || size.x <= 0.0f || size.y <= 0.0f) return;

    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 br(origin.x + size.x, origin.y + size.y);

    dl->AddRectFilled(origin, br, kColorBg, kBarRounding);

    const size_t n = values.size();
    const int total_h = static_cast<int>(size.y);
    const int base_row = total_h / static_cast<int>(std::max<size_t>(1, n));
    const int leftover = total_h - base_row * static_cast<int>(n);

    float y = origin.y;
    for (size_t i = 0; i < n; ++i) {
        const float v = std::clamp(values[i] * amp, 0.0f, 1.0f);
        const float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, n - 1));
        const int row_h = base_row + (static_cast<int>(i) < leftover ? 1 : 0);
        const float y_next = y + static_cast<float>(row_h);
        if (v > 0.0f) {
            dl->AddRectFilled(ImVec2(origin.x, y),
                              ImVec2(origin.x + v * size.x, y_next),
                              band_color(t), 0.0f);
        }
        y = y_next;
    }

    dl->AddRect(origin, br, kColorOutline, kBarRounding);
    ImGui::Dummy(size);
}


// Build the live hint string for each section.
const char* current_source_label(AudioSystem& system, const config::Settings& cfg) {
    const auto sources = system.available_sources();
    for (const auto& s : sources) {
        if (s.code == cfg.audio.capture_source_code) return s.display.c_str();
    }
    return cfg.audio.capture_source_code.c_str();
}

}  // namespace

// ============================ Sections ===================================

// ---- Audio Source -------------------------------------------------------
//
// Single row: title on the left, the Source dropdown filling the rest of
// the line. The dropdown's selected text already names the active source,
// so no separate hint or "Source:" body row is needed.

static void section_audio_source(AudioSystem& system, config::Settings& cfg, bool&) {
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Audio Source");
    ImGui::SameLine();

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
    if (ImGui::Combo("##source_combo", &sel, name_ptrs.data(), (int)name_ptrs.size())
        && sel >= 0 && sel < (int)sources.size()) {
        system.switch_source(sources[sel].code);
    }
    ImGui::PopItemWidth();
    tip("Where Listeningway listens.\n"
        "  - System Audio: everything the speakers play.\n"
        "  - Game Audio Only: just this game (Win10 22H2+).\n"
        "  - None: turn analysis off.");

    ImGui::Separator();
}

// ---- Levels -------------------------------------------------------------

static void section_levels(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    const char* fmt_label = format_label(static_cast<int>(snap.audio_format));
    const bool show = section_header_with_settings("Levels", fmt_label, "levels");

    const float vol_amp = cfg.frequency.amplifier_volume;
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    meter_row("Volume", std::clamp(snap.volume * vol_amp, 0.0f, 1.0f), fill);

    subgroup_label("Stereo:");
    ImGui::Indent(kSubGroupIndent);
    meter_row("Left",  std::clamp(snap.volume_left  * vol_amp, 0.0f, 1.0f), fill);
    meter_row("Right", std::clamp(snap.volume_right * vol_amp, 0.0f, 1.0f), fill);
    center_meter_row("Pan", snap.audio_pan);
    ImGui::Unindent(kSubGroupIndent);

    if (show) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Volume Boost", &cfg.frequency.amplifier_volume, 1.0f, 11.0f, "%.2f"))
            dirty = true;
        tip("Visual-only multiplier on the volume readouts and the listeningway_volume uniform. Doesn't change beat detection or analysis.\nTechnical: frequency.amplifier_volume, [1, 11]");
        if (slider_row("Pan Smoothing", &cfg.audio.pan_smoothing, 0.0f, 1.0f, "%.2f"))
            dirty = true;
        tip("Smooths out pan jitter. 0 = instant response, 1 = very slow.\nTechnical: audio.pan_smoothing, [0, 1]");
        if (slider_row("Pan Offset", &cfg.audio.pan_offset, -1.0f, 1.0f, "%.2f"))
            dirty = true;
        tip("Shifts the perceived stereo center. Useful if your room/headphones are biased.\nTechnical: audio.pan_offset, [-1, +1]");
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---- Beat Detection -----------------------------------------------------

static void section_beat(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;

    char hint[32];
    if (snap.tempo_detected) {
        std::snprintf(hint, sizeof(hint), "%.0f BPM", snap.tempo_bpm);
    } else {
        std::snprintf(hint, sizeof(hint), "searching...");
    }
    const bool show = section_header_with_settings("Beat Detection", hint, "beat");

    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    meter_row("Beat",          std::clamp(snap.beat,       0.0f, 1.0f), fill);
    meter_row("Beat position", std::clamp(snap.beat_phase, 0.0f, 1.0f), fill);
    if (snap.tempo_detected) {
        info_row("Tempo", "%.1f BPM (%.0f%% confidence)",
                 snap.tempo_bpm, snap.tempo_confidence * 100.0f);
    } else {
        label_left("Tempo");
        ImGui::TextDisabled("searching... (%.1f BPM, %.0f%% confidence)",
                            snap.tempo_bpm, snap.tempo_confidence * 100.0f);
    }

    if (show) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Beat sensitivity", &cfg.beat.threshold_lambda, 0.0f, 1.0f, "%.3f"))
            dirty = true;
        tip("Higher = pickier (only the loudest hits register). Lower = more sensitive to subtle rhythm. Default works for most music.\nTechnical: beat.threshold_lambda, [0, 1]");
        if (slider_row("Beat decay", &cfg.beat.beat_decay_per_sec, 0.1f, 10.0f, "%.2f"))
            dirty = true;
        tip("How fast the listeningway_beat uniform fades after each onset. Higher = sharper flashes.\nTechnical: beat.beat_decay_per_sec, /sec");

        subgroup_label("Advanced:");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Threshold window (ms)", &cfg.beat.threshold_window_ms, 10.0f, 1000.0f, "%.0f"))
            dirty = true;
        tip("Sliding window for the adaptive threshold.\nTechnical: beat.threshold_window_ms");
        if (slider_row("Refractory (ms)", &cfg.beat.refractory_ms, 5.0f, 500.0f, "%.0f"))
            dirty = true;
        tip("Minimum spacing between detected onsets. Suppresses double-fires.\nTechnical: beat.refractory_ms");
        if (slider_row("PLL gain (P)", &cfg.beat.phase_kp, 0.0f, 1.0f, "%.3f"))
            dirty = true;
        tip("How hard the beat phase pulls toward each detected onset.\nTechnical: beat.phase_kp");
        if (slider_row("PLL gain (I)", &cfg.beat.phase_ki, 0.0f, 0.5f, "%.4f"))
            dirty = true;
        tip("How fast tempo drift is corrected.\nTechnical: beat.phase_ki");
        if (slider_row("Tempo prior (BPM)", &cfg.beat.tempo_prior_bpm, 60.0f, 200.0f, "%.0f"))
            dirty = true;
        tip("Center of the tempo prior. Mitigates octave errors (locking to half/double speed).\nTechnical: beat.tempo_prior_bpm");
        if (slider_row("Tempo prior width (oct)", &cfg.beat.tempo_prior_sigma, 0.1f, 2.0f, "%.2f"))
            dirty = true;
        tip("Width of the tempo prior in octaves. Tighter = stronger pull toward the prior BPM.\nTechnical: beat.tempo_prior_sigma");
        if (slider_row("Tempo window (s)", &cfg.beat.tempo_window_sec, 1.0f, 30.0f, "%.1f"))
            dirty = true;
        tip("Autocorrelation history length. Longer = more stable tempo, slower to adapt.\nTechnical: beat.tempo_window_sec");
        ImGui::Unindent(kSubGroupIndent);
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---- Spectrum (was: Frequency Bands) ------------------------------------
//
// The spectrum panel has two render orientations (toggled by the inline
// "~" button in the section header):
//   - Horizontal (default): frequency on X, amplitude on Y, smooth filled
//     shape with peak-hold trace and frequency-axis anchors.
//   - Vertical (alternate): bands stacked top-to-bottom, each filling the
//     full pane width based on amplitude.
// Both render into a fixed 300 px tall pane with variable width, so the
// panel's footprint stays stable across band-count changes.

constexpr float kSpectrumPaneHeight = 300.0f;

static void section_spectrum(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    using Orient = config::UiConfig::SpectrumOrientation;

    const uint32_t n = std::min<uint32_t>(snap.freq_band_count,
                                           static_cast<uint32_t>(kSpectrumMaxBands));
    char hint[24];
    std::snprintf(hint, sizeof(hint), "%u bands", n);

    // Custom section header: title + hint + ~ toggle + subtle settings
    // disclosure, all on one line.
    ImGui::PushID("spectrum");
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Spectrum");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("(%s)", hint);

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID state_key = ImGui::GetID("settings_open");
    bool show = storage->GetBool(state_key, false);

    // Compute right-aligned position for [~] [settings] cluster.
    const char* tilde = "~";
    const char* settings_label = show ? "settings *" : "settings \xC2\xB7";
    const float pad      = ImGui::GetStyle().FramePadding.x * 2.0f;
    const float spacing  = ImGui::GetStyle().ItemSpacing.x;
    const float tilde_w    = ImGui::CalcTextSize(tilde).x          + pad;
    const float settings_w = ImGui::CalcTextSize(settings_label).x + pad;
    cursor_to_right_for(tilde_w + spacing + settings_w);

    // ~ orientation toggle.
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::SmallButton(tilde)) {
        cfg.ui.spectrum_orientation =
            (cfg.ui.spectrum_orientation == Orient::Horizontal)
                ? Orient::Vertical : Orient::Horizontal;
        dirty = true;
    }
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", cfg.ui.spectrum_orientation == Orient::Horizontal
            ? "Switch to vertical band stack"
            : "Switch to horizontal spectrum");
    }

    // Subtle settings disclosure (inline; mirrors subtle_settings_toggle).
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::SmallButton(settings_label)) show = !show;
    ImGui::PopStyleColor(4);
    storage->SetBool(state_key, show);

    ImGui::Separator();
    ImGui::PopID();

    const float bands_amp = cfg.frequency.amplifier_bands;
    const float pane_w = std::max(80.0f, ImGui::GetContentRegionAvail().x);
    const std::span<const float> values(snap.freq_bands.data(), n);

    if (cfg.ui.spectrum_orientation == Orient::Horizontal) {
        render_spectrum_horizontal(values, bands_amp,
                                    ImVec2(pane_w, kSpectrumPaneHeight),
                                    cfg.frequency.band_scale,
                                    cfg.frequency.min_freq,
                                    cfg.frequency.max_freq);
    } else {
        render_spectrum_vertical(values, bands_amp,
                                  ImVec2(pane_w, kSpectrumPaneHeight));
    }

    if (show) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Bands Boost", &cfg.frequency.amplifier_bands, 1.0f, 11.0f, "%.2f"))
            dirty = true;
        tip("Visual-only multiplier on the spectrum readouts and the listeningway_freqbands uniform.\nTechnical: frequency.amplifier_bands, [1, 11]");
        if (slider_row("Bass detail", &cfg.frequency.log_strength, 0.01f, 1.5f, "%.2f"))
            dirty = true;
        tip("Higher = more visible detail in the bass bands; lower = flatter spectrum.\nTechnical: frequency.log_strength, [0.01, 1.5]");

        subgroup_label("Equalizer (5-band):");
        ImGui::Indent(kSubGroupIndent);
        const char* const eq_names[5] = {"Bass", "Low-Mid", "Mid", "High-Mid", "Treble"};
        for (int i = 0; i < 5; ++i) {
            if (slider_row(eq_names[i], &cfg.frequency.equalizer_bands[i], 0.0f, 4.0f, "%.2f"))
                dirty = true;
        }
        if (slider_row("Equalizer width", &cfg.frequency.equalizer_width, 0.05f, 0.5f, "%.2f"))
            dirty = true;
        tip("Width of each EQ knob's influence (Gaussian σ).\nTechnical: frequency.equalizer_width");
        ImGui::Unindent(kSubGroupIndent);

        subgroup_label("Advanced:");
        ImGui::Indent(kSubGroupIndent);
        const char* const scales[] = {"Linear", "Log", "Mel (Slaney)"};
        int scale = static_cast<int>(cfg.frequency.band_scale);
        if (combo_row("Band scale", &scale, scales, 3)) {
            cfg.frequency.band_scale =
                static_cast<config::FrequencyConfig::BandScale>(scale);
            dirty = true;
        }
        tip("How frequencies map onto the bands.\n  • Mel: matches human pitch perception.\n  • Log: v1 default.\n  • Linear: legacy.\nTechnical: frequency.band_scale");

        if (slider_int_row("Band count", &cfg.frequency.band_count, 8, 128))
            dirty = true;
        tip("Number of frequency bands published. Must match shader array size.\nTechnical: frequency.band_count, [8, 128]");
        if (slider_int_row("Analysis resolution (FFT)", &cfg.frequency.fft_size, 256, 8192))
            dirty = true;
        tip("FFT window size. Higher = more frequency detail, more CPU. Power-of-two recommended.\nTechnical: frequency.fft_size");
        if (slider_row("Low cutoff (Hz)", &cfg.frequency.min_freq, 10.0f, 500.0f, "%.0f"))
            dirty = true;
        tip("Lowest frequency included.\nTechnical: frequency.min_freq, Hz");
        if (slider_row("High cutoff (Hz)", &cfg.frequency.max_freq, 2000.0f, 22050.0f, "%.0f"))
            dirty = true;
        tip("Highest frequency included.\nTechnical: frequency.max_freq, Hz");
        if (slider_row("Magnitude scaling", &cfg.frequency.band_norm, 0.001f, 1.0f, "%.3f"))
            dirty = true;
        tip("Raw FFT magnitude → band amplitude scaling factor.\nTechnical: frequency.band_norm");
        ImGui::Unindent(kSubGroupIndent);
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---- Spatial (was: Directional Intensity) -------------------------------

static void section_spatial(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    const char* fmt_label = format_label(static_cast<int>(snap.audio_format));
    const bool show = section_header_with_settings("Spatial", fmt_label, "spatial");

    const float dir_amp = cfg.frequency.amplifier_direction;
    const char* const labels[8] = { "F", "FR", "R", "BR", "B", "BL", "L", "FL" };
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    // Sized to match the bar column under TightRowSpacing: 8 rows × ~14 px
    // each plus a small margin so the rose's vertical extent aligns with
    // the column.
    constexpr float kRoseSide = 120.0f;
    constexpr float kLabelMargin = 9.0f;     // extra padding outside the rose for cardinal labels
    const float side = kRoseSide;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + side * 0.5f, origin.y + side * 0.5f);
    const float radius = side * 0.5f - kLabelMargin;
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, radius, IM_COL32(30, 30, 30, 128), 64);
    dl->AddCircle(center, radius, kColorOutline, 64, 1.0f);

    // F = North. Rotate so index 0 (Front) sits at the top of the rose.
    constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
    constexpr float kRoseAngleOffset = -std::numbers::pi_v<float> / 2.0f;
    for (int i = 0; i < 8; ++i) {
        const float v = std::clamp(snap.direction8[i] * dir_amp, 0.0f, 1.0f);
        const float a0 = kRoseAngleOffset + (two_pi / 8.0f) * (i - 0.5f);
        const float a1 = kRoseAngleOffset + (two_pi / 8.0f) * (i + 0.5f);
        const float r  = radius * v;
        const ImVec2 p0(center.x + r * cosf(a0), center.y + r * sinf(a0));
        const ImVec2 p1(center.x + r * cosf(a1), center.y + r * sinf(a1));
        dl->AddTriangleFilled(center, p0, p1, fill);
    }

    // Cardinal labels positioned inside the rose, centered along each slice's
    // direction at a radius about 12 px short of the perimeter. Faint white
    // so the labels read as quiet annotation, not chrome.
    const ImU32 cardinal_col = IM_COL32(255, 255, 255, 130);
    const float label_radius = std::max(0.0f, radius - 12.0f);
    for (int i = 0; i < 8; ++i) {
        const float a = kRoseAngleOffset + (two_pi / 8.0f) * static_cast<float>(i);
        const ImVec2 anchor(center.x + label_radius * cosf(a),
                             center.y + label_radius * sinf(a));
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const float lx = anchor.x - ts.x * 0.5f;
        const float ly = anchor.y - ts.y * 0.5f;
        dl->AddText(ImVec2(lx, ly), cardinal_col, labels[i]);
    }

    ImGui::Dummy(ImVec2(side, side));

    ImGui::SameLine();
    {
        TightRowSpacing tight;
        LocalLabelColumn col(std::span<const char* const>{labels, 8});
        ImGui::BeginGroup();
        for (int i = 0; i < 8; ++i) {
            meter_row(labels[i], std::clamp(snap.direction8[i] * dir_amp, 0.0f, 1.0f), fill, "%.2f");
        }
        ImGui::EndGroup();
    }

    if (show) {
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Direction Boost", &cfg.frequency.amplifier_direction,
                        1.0f, 11.0f, "%.2f"))
            dirty = true;
        tip("Visual-only multiplier on the directional uniforms (listeningway_front, _front_right, etc.).\nTechnical: frequency.amplifier_direction, [1, 11]");
        if (slider_row("Spread", &cfg.frequency.spatial_spread, 0.0f, 0.5f, "%.2f"))
            dirty = true;
        tip("How much each direction's energy bleeds into its two neighbours on the rose. 0 = sharp peaks per channel; 0.5 = soft glow.\nTechnical: frequency.spatial_spread, [0, 0.5]");
        if (slider_row("Smoothing", &cfg.frequency.spatial_smoothing, 0.0f, 0.95f, "%.2f"))
            dirty = true;
        tip("Temporal smoothing on the rose. 0 = raw per-frame; higher = calmer rose, slower to react.\nTechnical: frequency.spatial_smoothing, [0, 0.95]");
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---- Advanced metrics (auto-leveled, phases, perceptual) ---------------

static void section_advanced(const AudioSnapshot& snap, config::Settings& cfg, bool& dirty) {
    using namespace overlay_style;
    const bool show = section_header_with_settings("Advanced", nullptr, "advanced");

    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_PlotHistogram);

    subgroup_label("Auto-leveled (1.0 = recent average loudness):");
    ImGui::Indent(kSubGroupIndent);
    const float scale = 1.0f / std::max(0.1f, cfg.agc.clamp_max);
    auto leveled_row = [&](const char* lbl, float v) {
        meter_row(lbl, std::clamp(v * scale, 0.0f, 1.0f), fill, nullptr);
        ImGui::SameLine();
        ImGui::Text("%.2f", v);
    };
    {
        TightRowSpacing tight;
        leveled_row("Volume", snap.volume_norm);
        leveled_row("Bass",   snap.bass_norm);
        leveled_row("Mid",    snap.mid_norm);
        leveled_row("Treble", snap.treb_norm);
    }
    ImGui::Unindent(kSubGroupIndent);

    subgroup_label("Energy phases:");
    ImGui::Indent(kSubGroupIndent);
    {
        TightRowSpacing tight;
        meter_row("Volume", snap.phase_volume, fill);
        meter_row("Bass",   snap.phase_bass,   fill);
        meter_row("Treble", snap.phase_treble, fill);
    }
    ImGui::Unindent(kSubGroupIndent);

    subgroup_label("Perceptual:");
    ImGui::Indent(kSubGroupIndent);
    {
        TightRowSpacing tight;
        meter_row("Brightness", snap.spectral_centroid, fill, "%.3f");
        meter_row("Loudness",   std::clamp(snap.loudness, 0.0f, 1.0f), fill, "%.2f");
    }
    ImGui::Unindent(kSubGroupIndent);

    if (show) {
        ImGui::Indent(kSubGroupIndent);

        subgroup_label("Energy phases (rate per band):");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Volume rate", &cfg.chronotensity.gain_volume, 0.0f, 5.0f, "%.2f"))
            dirty = true;
        tip("How fast the volume-driven phase advances per unit of auto-leveled volume.\nTechnical: chronotensity.gain_volume");
        if (slider_row("Bass rate", &cfg.chronotensity.gain_bass, 0.0f, 5.0f, "%.2f"))
            dirty = true;
        tip("Rate for the bass-driven phase.\nTechnical: chronotensity.gain_bass");
        if (slider_row("Treble rate", &cfg.chronotensity.gain_treble, 0.0f, 5.0f, "%.2f"))
            dirty = true;
        tip("Rate for the treble-driven phase.\nTechnical: chronotensity.gain_treble");
        ImGui::Unindent(kSubGroupIndent);

        subgroup_label("Auto-leveling (AGC):");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Window (s)", &cfg.agc.window_seconds, 0.5f, 30.0f, "%.1f"))
            dirty = true;
        tip("Running-mean window for auto-leveling.\nTechnical: agc.window_seconds");
        if (slider_row("Clamp max", &cfg.agc.clamp_max, 1.5f, 8.0f, "%.1f"))
            dirty = true;
        tip("Upper bound on the auto-leveled value.\nTechnical: agc.clamp_max");
        if (slider_row("Smoothing attack (ms)", &cfg.agc.att_attack_ms, 1.0f, 1000.0f, "%.0f"))
            dirty = true;
        tip("How fast the smoothed (_att) sibling rises.\nTechnical: agc.att_attack_ms");
        if (slider_row("Smoothing release (ms)", &cfg.agc.att_release_ms, 1.0f, 5000.0f, "%.0f"))
            dirty = true;
        tip("How fast the smoothed sibling falls.\nTechnical: agc.att_release_ms");
        ImGui::Unindent(kSubGroupIndent);

        subgroup_label("Loudness:");
        ImGui::Indent(kSubGroupIndent);
        if (slider_row("Window (ms)", &cfg.loudness.window_ms, 50.0f, 3000.0f, "%.0f"))
            dirty = true;
        tip("K-weighted RMS window. 400 ms = BS.1770 'momentary' loudness.\nTechnical: loudness.window_ms");
        ImGui::Unindent(kSubGroupIndent);
        ImGui::Unindent(kSubGroupIndent);
    }
}

// ---- Performance (was: DSP Profiler) -----------------------------------

static void section_performance(const AudioSnapshot& snap) {
    using namespace overlay_style;

    char hint[32];
    std::snprintf(hint, sizeof(hint), "%.1f \xC2\xB5s total", snap.pipeline_micros);
    section_header_only("Performance", hint);

    if (snap.stage_count == 0) {
        ImGui::TextDisabled("(no timings yet)");
        return;
    }
    float max_micros = 1.0f;
    for (uint32_t i = 0; i < snap.stage_count; ++i) {
        max_micros = std::max(max_micros, snap.stage_timings[i].micros);
    }
    TightRowSpacing tight;
    for (uint32_t i = 0; i < snap.stage_count; ++i) {
        const auto& t = snap.stage_timings[i];
        label_left(t.name);
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

// ---- Integrations (was: Network Outputs) --------------------------------

namespace {

bool host_port_rows(const char* prefix, std::string& host, int& port,
                     int port_lo, int port_hi) {
    bool changed = false;

    label_left("Host");
    char host_buf[64] = {};
    std::snprintf(host_buf, sizeof(host_buf), "%s", host.c_str());
    char id[40]; std::snprintf(id, sizeof(id), "##host_%s", prefix);
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText(id, host_buf, sizeof(host_buf))) {
        host = host_buf;
        changed = true;
    }
    ImGui::PopItemWidth();
    tip("Destination address. 127.0.0.1 = this machine. Anything else sends data over the network.");

    label_left("Port");
    char id2[40]; std::snprintf(id2, sizeof(id2), "##port_%s", prefix);
    ImGui::PushItemWidth(-1);
    if (ImGui::InputInt(id2, &port)) {
        port = std::clamp(port, port_lo, port_hi);
        changed = true;
    }
    ImGui::PopItemWidth();

    return changed;
}

void section_integrations(config::Settings& cfg, bool& dirty,
                            output::ConsumerRegistry& registry) {
    using namespace overlay_style;

    const int active_count =
        (cfg.network.osc.enabled ? 1 : 0) +
        (cfg.network.openrgb.enabled ? 1 : 0);
    char hint[24];
    if (active_count == 0) std::snprintf(hint, sizeof(hint), "none active");
    else                   std::snprintf(hint, sizeof(hint), "%d active", active_count);
    section_header_only("Integrations", hint);

    // ---- OSC ------------------------------------------------------------
    {
        std::string status;
        if (auto* c = registry.find_by_id("osc")) status = c->status_line();

        if (integration_row("OSC", cfg.network.osc.enabled, dirty,
                             status, "osc")) {
            ImGui::Indent(kSubGroupIndent * 2.0f);
            ImGui::TextDisabled("Send to creative tools:\n"
                "TouchDesigner, Resolume, Max/MSP, vvvv, MadMapper, VRChat OSC.\n"
                "Send-only — no port is opened on this machine. Safe in any anti-cheat context.");
            ImGui::Spacing();
            if (host_port_rows("osc", cfg.network.osc.host, cfg.network.osc.port, 1, 65535))
                dirty = true;
            if (slider_int_row("Update rate (Hz)", &cfg.network.osc.rate_hz, 1, 120)) {
                cfg.network.osc.rate_hz = std::clamp(cfg.network.osc.rate_hz, 1, 120);
                dirty = true;
            }
            tip("How often to send each OSC message per second. Default 60.\nTechnical: network.osc.rate_hz, [1, 120]");
            label_left("Test");
            if (ImGui::Button("Send test packet##osc", ImVec2(-1, 0))) {
                if (auto* c = registry.find_by_id("osc")) c->send_test_packet();
            }
            tip("Sends a single /listeningway/test message. Use samples/integration_harness.py to verify reception.");
            ImGui::Unindent(kSubGroupIndent * 2.0f);
        }
    }

    // ---- OpenRGB --------------------------------------------------------
    {
        std::string status;
        if (auto* c = registry.find_by_id("openrgb")) status = c->status_line();

        if (integration_row("OpenRGB", cfg.network.openrgb.enabled, dirty,
                             status, "openrgb")) {
            ImGui::Indent(kSubGroupIndent * 2.0f);
            ImGui::TextDisabled("Drive RGB peripherals:\n"
                "Connects to a running OpenRGB server to light up keyboards,\n"
                "mice, RAM, fans, and case strips along with your music.\n"
                "Client-only — no port is opened on this machine.");
            ImGui::Spacing();
            if (host_port_rows("openrgb", cfg.network.openrgb.host,
                                cfg.network.openrgb.port, 1, 65535))
                dirty = true;
            if (slider_int_row("Update rate (Hz)", &cfg.network.openrgb.rate_hz, 5, 60)) {
                cfg.network.openrgb.rate_hz = std::clamp(cfg.network.openrgb.rate_hz, 5, 60);
                dirty = true;
            }
            tip("Frame rate. Default 30. The OpenRGB server has known CPU issues above ~60 Hz.\nTechnical: network.openrgb.rate_hz, [5, 60]");
            if (slider_row("Brightness", &cfg.network.openrgb.brightness, 0.0f, 1.0f, "%.2f"))
                dirty = true;
            tip("Global multiplier on output color intensity (0..1).\nTechnical: network.openrgb.brightness");
            label_left("Test");
            if (ImGui::Button("Flash all LEDs##openrgb", ImVec2(-1, 0))) {
                if (auto* c = registry.find_by_id("openrgb")) c->send_test_packet();
            }
            tip("Connects briefly and flashes every LED to white for one frame. Verifies the server is reachable and devices respond.");
            ImGui::Unindent(kSubGroupIndent * 2.0f);
        }
    }
}

}  // namespace

// ---- Settings (was: Settings Management) -------------------------------

static void section_settings(config::Store& store, config::Settings& cfg,
                              AudioSystem& system, bool& dirty) {
    section_header_only("Settings", nullptr);

    ImGui::Columns(3, "##settings_buttons", false);
    if (ImGui::Button("Save", ImVec2(-1, 0))) store.save();
    tip("Save current settings to Listeningway.json (next to the addon).");
    ImGui::NextColumn();
    if (ImGui::Button("Load", ImVec2(-1, 0))) store.load();
    tip("Reload Listeningway.json from disk. Discards any unsaved changes.");
    ImGui::NextColumn();
    if (ImGui::Button("Reset", ImVec2(-1, 0))) {
        store.publish(config::Settings{});
        store.save();
    }
    tip("Reset all settings to defaults and save.");
    ImGui::Columns(1);

    if (ImGui::Button("Restart audio pipeline", ImVec2(-1, 0))) {
        system.switch_source(cfg.audio.capture_source_code);
    }
    tip("Stop and restart the active source. Use this after changing the FFT size or band count.");

    if (ImGui::Checkbox("Debug logging", &cfg.debug.debug_logging)) dirty = true;
    tip("Verbose log to listeningway.log next to the addon.\nTechnical: debug.debug_logging");

    ImGui::TextDisabled("Config file: %s", store.path().u8string().c_str());
}

// ---- Top level ----------------------------------------------------------

void draw_overlay(reshade::api::effect_runtime*,
                  AudioSystem& system,
                  config::Store& store,
                  output::ConsumerRegistry& consumers,
                  HMODULE addon_module) {
    const auto snap = system.snapshot();
    auto cfg = store.snapshot();
    bool dirty = false;

    compute_columns();

    section_audio_source(system, cfg, dirty);
    section_levels(snap, cfg, dirty);
    section_beat(snap, cfg, dirty);
    section_spectrum(snap, cfg, dirty);
    section_spatial(snap, cfg, dirty);
    section_advanced(snap, cfg, dirty);
    section_integrations(cfg, dirty, consumers);
    section_performance(snap);
    section_settings(store, cfg, system, dirty);

    if (dirty) {
        store.publish(cfg);
        consumers.on_settings_changed(system, addon_module, cfg);
    }
}

}  // namespace lw
