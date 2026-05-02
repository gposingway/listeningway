// BandNormStage — 3-band macro AGC (bass / mid / treble running-mean ratios).
// Mirrors MilkDrop's bass/mid/treb + bass_att/mid_att/treb_att variables
// (research-notes.md §4 — the v2 addition shader authors most want).
//
// Reads:  Bands, Format
// Writes: BassNorm, MidNorm, TrebNorm, BassAtt, MidAtt, TrebAtt
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>

#include "../i_dsp_stage.h"

namespace lw::dsp {

class BandNormStage final : public IDspStage {
public:
    std::string_view name() const override { return "band_norm"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {FieldId::Bands, FieldId::Format};
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::BassNorm, FieldId::MidNorm, FieldId::TrebNorm,
            FieldId::BassAtt,  FieldId::MidAtt,  FieldId::TrebAtt,
        };
        return w;
    }

    void reset() override {
        bass_mean_ = mid_mean_ = treb_mean_ = 0.0f;
        bass_att_ = mid_att_ = treb_att_ = 1.0f;
        last_t_ = std::chrono::steady_clock::time_point{};
    }

    void process(AnalysisFrame& frame, const config::Settings& cfg) override {
        if (frame.bands.empty()) return;

        // Split bands into three macro bins by index third. (Operates on the
        // post-EQ `bands`; on a mel-scaled band layout this corresponds to
        // bass/mid/treble by perceptual frequency.)
        const size_t n = frame.bands.size();
        const size_t a = n / 3;
        const size_t b = (n * 2) / 3;
        float bass_e = 0.0f, mid_e = 0.0f, treb_e = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float v = frame.bands[i];
            if (i < a) bass_e += v;
            else if (i < b) mid_e += v;
            else treb_e += v;
        }
        bass_e /= std::max<size_t>(1, a);
        mid_e  /= std::max<size_t>(1, b - a);
        treb_e /= std::max<size_t>(1, n - b);

        // Time delta for AGC integration.
        auto now = frame.captured_at;
        float dt_sec = 1.0f / 60.0f;
        if (last_t_.time_since_epoch().count() != 0) {
            dt_sec = std::chrono::duration<float>(now - last_t_).count();
        }
        last_t_ = now;

        const float window = std::max(0.1f, cfg.agc.window_seconds);
        const float a_norm = std::clamp(dt_sec / window, 0.0f, 1.0f);
        if (bass_mean_ <= 0.0f) bass_mean_ = std::max(bass_e, 1e-4f);
        if (mid_mean_  <= 0.0f) mid_mean_  = std::max(mid_e,  1e-4f);
        if (treb_mean_ <= 0.0f) treb_mean_ = std::max(treb_e, 1e-4f);
        bass_mean_ = (1.0f - a_norm) * bass_mean_ + a_norm * std::max(bass_e, 1e-6f);
        mid_mean_  = (1.0f - a_norm) * mid_mean_  + a_norm * std::max(mid_e,  1e-6f);
        treb_mean_ = (1.0f - a_norm) * treb_mean_ + a_norm * std::max(treb_e, 1e-6f);

        const float clamp_max = cfg.agc.clamp_max;
        const float bn = std::min(bass_e / std::max(bass_mean_, 1e-6f), clamp_max);
        const float mn = std::min(mid_e  / std::max(mid_mean_,  1e-6f), clamp_max);
        const float tn = std::min(treb_e / std::max(treb_mean_, 1e-6f), clamp_max);
        frame.bass_norm = bn;
        frame.mid_norm  = mn;
        frame.treb_norm = tn;

        // Asymmetric attack/release smoothing
        auto smooth = [&](float& att, float target) {
            const float alpha = (target > att)
                ? std::clamp(dt_sec / std::max(0.001f, cfg.agc.att_attack_ms * 0.001f), 0.0f, 1.0f)
                : std::clamp(dt_sec / std::max(0.001f, cfg.agc.att_release_ms * 0.001f), 0.0f, 1.0f);
            att = (1.0f - alpha) * att + alpha * target;
        };
        smooth(bass_att_, bn);
        smooth(mid_att_,  mn);
        smooth(treb_att_, tn);
        frame.bass_att = bass_att_;
        frame.mid_att  = mid_att_;
        frame.treb_att = treb_att_;
    }

private:
    float bass_mean_ = 0.0f, mid_mean_ = 0.0f, treb_mean_ = 0.0f;
    float bass_att_  = 1.0f, mid_att_  = 1.0f, treb_att_  = 1.0f;
    std::chrono::steady_clock::time_point last_t_{};
};

}  // namespace lw::dsp
