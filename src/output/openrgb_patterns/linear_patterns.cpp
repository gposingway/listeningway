#include "linear_patterns.h"

#include <algorithm>
#include <cmath>

#include "../../audio/snapshot/audio_snapshot.h"

namespace lw::output::patterns {

namespace {

constexpr float kStaticAccentHue = 0.50f;
constexpr float kStaticAccentSat = 0.85f;
constexpr float kStaticAccentVal = 0.45f;

float compress_volume(float v) noexcept { return clamp01(v * 0.5f); }

// Sample the freq band amplitude at a normalised position t ∈ [0, 1] in
// the strip. Linear interpolation between bins so a 16-LED strip on a
// 64-band spectrum still reads smoothly.
float sample_band(const AudioSnapshot& snap, float t) noexcept {
    const size_t nb = std::min<size_t>(snap.freq_band_count, kMaxBands);
    if (nb == 0) return 0.0f;
    const float fpos = clamp01(t) * static_cast<float>(nb - 1);
    const size_t lo  = static_cast<size_t>(std::floor(fpos));
    const size_t hi  = std::min(lo + 1, nb - 1);
    const float frac = fpos - static_cast<float>(lo);
    return snap.freq_bands[lo] * (1.0f - frac) + snap.freq_bands[hi] * frac;
}

void render_off(std::span<ColorRgb> out) noexcept {
    std::fill(out.begin(), out.end(), ColorRgb::black());
}

void render_static(std::span<ColorRgb> out, float brightness) noexcept {
    const ColorRgb c = apply_brightness(
        hsv_to_rgb(kStaticAccentHue, kStaticAccentSat, kStaticAccentVal),
        brightness);
    std::fill(out.begin(), out.end(), c);
}

void render_color_wash(const AudioSnapshot& snap, std::span<ColorRgb> out,
                        float brightness) noexcept {
    const float vol = noise_gate(compress_volume(snap.volume_att), 0.02f, 0.05f);
    const float h   = centroid_to_hue(snap.spectral_centroid);
    const ColorRgb c = apply_brightness(
        hsv_to_rgb(h, 1.0f, lerp(0.10f, 1.0f, vol)),
        brightness);
    std::fill(out.begin(), out.end(), c);
}

void render_spectrum_bar(const AudioSnapshot& snap, std::span<ColorRgb> out,
                          float brightness) noexcept {
    const size_t n = out.size();
    if (n == 0) return;
    const float vol  = clamp01(snap.volume_norm * 0.5f);
    const float beat = clamp01(snap.beat);

    for (size_t i = 0; i < n; ++i) {
        const float t = (n == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
        const float band_v = sample_band(snap, t);
        const ColorRgb c = spectrum_ramp(t);
        const float intensity = clamp01(band_v * 1.5f + vol * 0.3f + beat * 0.4f);
        out[i] = apply_brightness(dim(c, intensity), brightness);
    }
}

void render_vu_meter(const AudioSnapshot& snap, std::span<ColorRgb> out,
                      PatternState& state, float brightness, float dt_sec) noexcept {
    const size_t n = out.size();
    if (n == 0) return;

    // Compress AGC-normalised volume into [0, 1] and gate silence.
    const float v = noise_gate(clamp01(snap.volume_norm * 0.40f), 0.02f, 0.04f);

    // Peak hold: track the highest position seen, decay slowly.
    if (v > state.vu_peak_pos) {
        state.vu_peak_pos = v;
        state.vu_peak_decay_seconds = 0.6f;  // hold ~0.6 s, then start decaying
    } else if (state.vu_peak_decay_seconds > 0.0f) {
        state.vu_peak_decay_seconds = std::max(0.0f, state.vu_peak_decay_seconds - dt_sec);
    } else {
        state.vu_peak_pos = std::max(v, state.vu_peak_pos - dt_sec * 0.6f);
    }

    const float fill_n = v * static_cast<float>(n);
    const float peak_n = state.vu_peak_pos * static_cast<float>(n);

    for (size_t i = 0; i < n; ++i) {
        const float t = (n == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
        ColorRgb c{0, 0, 0};
        if (static_cast<float>(i) < fill_n) {
            // Green at low end, yellow in the middle, red near the top.
            float h;
            if (t < 0.6f) h = 0.33f;             // green
            else if (t < 0.85f) h = 0.16f;        // yellow
            else                h = 0.0f;          // red
            c = hsv_to_rgb(h, 1.0f, 1.0f);
        }
        // Peak-hold dot at the apex (one LED, white).
        const int peak_idx = static_cast<int>(std::round(peak_n - 0.5f));
        if (static_cast<int>(i) == peak_idx && peak_idx >= 0 &&
            peak_idx < static_cast<int>(n)) {
            c = ColorRgb{255, 255, 255};
        }
        out[i] = apply_brightness(c, brightness);
    }
}

void render_chase_orbit(const AudioSnapshot& snap, std::span<ColorRgb> out,
                        float brightness) noexcept {
    const size_t n = out.size();
    if (n == 0) return;
    const float phase = snap.phase_volume - std::floor(snap.phase_volume);
    const float head_pos = phase * static_cast<float>(n);
    constexpr float kTailLen = 4.0f;
    constexpr float kHeadHueAt0 = 0.55f;  // teal-cyan head; tail falls toward purple

    for (size_t i = 0; i < n; ++i) {
        // Distance from head (forward direction). Wraps around the strip
        // so the tail bleeds into the start when the head reaches the end.
        float d = head_pos - static_cast<float>(i);
        if (d < 0.0f) d += static_cast<float>(n);
        if (d > kTailLen) {
            out[i] = ColorRgb::black();
            continue;
        }
        const float falloff = clamp01(1.0f - d / kTailLen);
        const ColorRgb c = hsv_to_rgb(kHeadHueAt0, 0.85f, falloff);
        out[i] = apply_brightness(c, brightness);
    }
}

void render_pulse_from_center(const AudioSnapshot& snap, std::span<ColorRgb> out,
                                float brightness) noexcept {
    const size_t n = out.size();
    if (n == 0) return;
    const float pulse = clamp01(snap.bass_norm * 0.5f + snap.beat * 0.6f);
    const float center = static_cast<float>(n - 1) * 0.5f;
    const float radius = pulse * static_cast<float>(n) * 0.5f;

    for (size_t i = 0; i < n; ++i) {
        const float d = std::abs(static_cast<float>(i) - center);
        const float intensity = clamp01(1.0f - d / std::max(radius, 0.5f));
        // Bass-warm hue; pulses from center outward.
        const ColorRgb c = hsv_to_rgb(0.05f, 0.85f, intensity);
        out[i] = apply_brightness(c, brightness);
    }
}

void render_stereo_split(const AudioSnapshot& snap, std::span<ColorRgb> out,
                          float brightness) noexcept {
    const size_t n = out.size();
    if (n == 0) return;
    const float vL = noise_gate(clamp01(snap.volume_left  * 1.5f), 0.02f, 0.04f);
    const float vR = noise_gate(clamp01(snap.volume_right * 1.5f), 0.02f, 0.04f);
    const size_t half = n / 2;

    for (size_t i = 0; i < n; ++i) {
        const bool left = (i < half);
        const float v = left ? vL : vR;
        // Left half gets a cooler hue (cyan), right half a warmer one (magenta).
        const float h = left ? 0.50f : 0.85f;
        const ColorRgb c = hsv_to_rgb(h, 0.85f, lerp(0.05f, 1.0f, v));
        out[i] = apply_brightness(c, brightness);
    }
}

}  // namespace

void render_linear(config::OpenRgbConfig::LinearPattern pattern,
                    const AudioSnapshot& snap,
                    std::span<ColorRgb>  out,
                    PatternState&        state,
                    float                brightness,
                    float                dt_sec) noexcept {
    using P = config::OpenRgbConfig::LinearPattern;
    switch (pattern) {
        case P::SpectrumBar:     render_spectrum_bar(snap, out, brightness); break;
        case P::VuMeter:         render_vu_meter(snap, out, state, brightness, dt_sec); break;
        case P::ChaseOrbit:      render_chase_orbit(snap, out, brightness); break;
        case P::PulseFromCenter: render_pulse_from_center(snap, out, brightness); break;
        case P::StereoSplit:     render_stereo_split(snap, out, brightness); break;
        case P::ColorWash:       render_color_wash(snap, out, brightness); break;
        case P::Static:          render_static(out, brightness); break;
        case P::Off:             render_off(out); break;
    }
}

}  // namespace lw::output::patterns
