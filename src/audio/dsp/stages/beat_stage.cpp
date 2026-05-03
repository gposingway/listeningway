#include "beat_stage.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lw::dsp {

namespace {

// --- Internal constants (no UI exposure) ---------------------------------

constexpr float kBaselineAlpha = 0.97f;   // ~30-frame time constant
constexpr float kSigmaBase     = 3.0f;    // N · sigma; effective N = kSigmaBase / strength
constexpr float kStrengthScale = 2.0f;    // sigmas-above-threshold to map onto [0, 1]
constexpr float kRefractorySec = 0.080f;

// Tempo / phase
constexpr float kEnvFps         = 100.0f;
constexpr float kBpmMin         = 60.0f;
constexpr float kBpmMax         = 180.0f;
constexpr float kTempoPriorBpm  = 120.0f;
constexpr float kTempoPriorSig  = 0.7f;
constexpr float kPhaseKp        = 0.15f;
constexpr float kPhaseKi        = 0.01f;

// Auto adaptation
constexpr float kAutoTargetRate    = 2.0f;     // onsets per second
constexpr float kAutoRateLowBound  = 1.0f;     // adapt up below this
constexpr float kAutoRateHighBound = 4.0f;     // adapt down above this
constexpr float kAutoStrengthMin   = 0.30f;
constexpr float kAutoStrengthMax   = 3.00f;
constexpr float kAutoStepSec       = 1.0f;     // adapt at most once per second
constexpr float kAutoStepSize      = 0.10f;
constexpr float kAutoLockHoldSec   = 3.0f;     // stable this long → "Locked"
constexpr float kAutoSilenceFlux   = 1e-4f;    // skip adaptation when total flux is this low

struct ProfileParams {
    float strength;
    float weight_low;
    float weight_mid;
    float weight_high;
    float decay_tau_sec;
};

ProfileParams profile_params(config::BeatConfig::Profile p) {
    using P = config::BeatConfig::Profile;
    switch (p) {
        case P::Percussive:
            // Drums / EDM / hip-hop: bass-driven, tight pulse for sharp visuals.
            return { /*strength*/ 1.10f, 1.00f, 0.50f, 0.30f, 0.120f };
        case P::Melodic:
            // Vocal / rock / jazz / classical: balanced bands, medium decay.
            return { /*strength*/ 1.40f, 0.90f, 0.90f, 0.70f, 0.160f };
        case P::Sustained:
            // Ambient / cinematic / sparse: more sensitive across bands,
            // longer decay so what little there is reads as motion.
            return { /*strength*/ 2.00f, 0.80f, 0.80f, 0.65f, 0.230f };
    }
    return { 1.0f, 1.0f, 0.7f, 0.5f, 0.150f };
}

float onset_strength(float flux, float baseline, float var, float n_sigma) {
    const float stddev    = std::sqrt(std::max(var, 1e-12f));
    const float threshold = baseline + n_sigma * stddev;
    if (flux <= threshold) return 0.0f;
    return std::clamp(
        (flux - threshold) / std::max(stddev, 1e-6f) / kStrengthScale,
        0.0f, 1.0f);
}

}  // namespace

void BeatStage::reset() {
    working_strength_ = 1.0f;
    weight_low_ = 1.00f; weight_mid_ = 0.70f; weight_high_ = 0.50f;
    decay_tau_sec_ = 0.150f;
    last_mode_ = -1;

    baseline_low_ = baseline_mid_ = baseline_high_ = 0.0f;
    var_low_ = var_mid_ = var_high_ = 0.0f;
    beat_value_ = 0.0f;
    last_t_ = {};
    last_onset_t_ = {};

    auto_onset_log_.clear();
    last_auto_step_t_ = {};
    last_auto_change_t_ = {};
    auto_locked_ = false;

    envelope_.clear();
    env_head_ = env_size_ = 0;
    tempo_bpm_ = 0.0f;
    tempo_confidence_ = 0.0f;
    tempo_detected_ = false;
    beat_phase_ = 0.0f;
    frames_since_tempo_update_ = 0;
}

void BeatStage::update_tempo() {
    if (env_size_ < 32) {
        tempo_bpm_ = 0.0f;
        tempo_confidence_ = 0.0f;
        tempo_detected_ = false;
        return;
    }
    std::vector<float> env(env_size_);
    for (size_t i = 0; i < env_size_; ++i) {
        const size_t idx = (env_head_ + kEnvCapacity - env_size_ + i) % kEnvCapacity;
        env[i] = envelope_[idx];
    }
    float mean = 0.0f;
    for (float v : env) mean += v;
    mean /= static_cast<float>(env.size());
    for (float& v : env) v -= mean;

    const int lag_min = static_cast<int>(60.0f * kEnvFps / kBpmMax);
    const int lag_max = std::min(static_cast<int>(60.0f * kEnvFps / kBpmMin),
                                  static_cast<int>(env.size() - 1));
    if (lag_min >= lag_max) {
        tempo_bpm_ = 0.0f;
        tempo_confidence_ = 0.0f;
        tempo_detected_ = false;
        return;
    }
    float best_score = 0.0f, second_best = 0.0f;
    int best_lag = lag_min;
    for (int lag = lag_min; lag <= lag_max; ++lag) {
        float ac = 0.0f;
        for (size_t i = 0; i + lag < env.size(); ++i) {
            ac += env[i] * env[i + lag];
        }
        const float bpm     = 60.0f * kEnvFps / static_cast<float>(lag);
        const float octaves = std::log2(bpm / kTempoPriorBpm);
        const float prior   = std::exp(-(octaves * octaves)
                                          / (2.0f * kTempoPriorSig * kTempoPriorSig));
        const float score = ac * prior;
        if (score > best_score) {
            second_best = best_score;
            best_score = score;
            best_lag = lag;
        } else if (score > second_best) {
            second_best = score;
        }
    }
    tempo_bpm_ = 60.0f * kEnvFps / static_cast<float>(best_lag);
    tempo_confidence_ = (best_score > 0.0f)
        ? std::clamp(1.0f - second_best / std::max(best_score, 1e-6f), 0.0f, 1.0f)
        : 0.0f;
    tempo_detected_ = tempo_confidence_ > 0.4f;
}

void BeatStage::update_auto_strength(float /*dt_sec*/, bool onset_fired,
                                      float audio_energy) {
    const auto now = std::chrono::steady_clock::now();

    if (onset_fired) {
        auto_onset_log_.push_back(now);
    }
    // Drop entries older than the window.
    const auto cutoff = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<float>(kAutoWindowSec));
    auto first_keep = std::find_if(auto_onset_log_.begin(), auto_onset_log_.end(),
        [&](const auto& t) { return t >= cutoff; });
    auto_onset_log_.erase(auto_onset_log_.begin(), first_keep);

    // Adapt once per kAutoStepSec.
    const float since_last_step = (last_auto_step_t_.time_since_epoch().count() != 0)
        ? std::chrono::duration<float>(now - last_auto_step_t_).count()
        : 1e6f;
    if (since_last_step < kAutoStepSec) return;
    last_auto_step_t_ = now;

    // Skip adaptation during silence — hold whatever we converged to.
    if (audio_energy < kAutoSilenceFlux) {
        return;
    }

    const float rate = static_cast<float>(auto_onset_log_.size()) / kAutoWindowSec;
    bool changed = false;
    if (rate < kAutoRateLowBound) {
        // Too few onsets — make the system more reactive.
        const float prev = working_strength_;
        working_strength_ = std::min(kAutoStrengthMax,
                                       working_strength_ + kAutoStepSize);
        if (std::abs(working_strength_ - prev) > 1e-4f) changed = true;
    } else if (rate > kAutoRateHighBound) {
        // Too many onsets — back off.
        const float prev = working_strength_;
        working_strength_ = std::max(kAutoStrengthMin,
                                       working_strength_ - kAutoStepSize);
        if (std::abs(working_strength_ - prev) > 1e-4f) changed = true;
    }

    if (changed) {
        last_auto_change_t_ = now;
        auto_locked_ = false;
    } else if (rate >= kAutoRateLowBound && rate <= kAutoRateHighBound) {
        // Hit the target band; lock once we've been stable for a while.
        const float since_last_change = (last_auto_change_t_.time_since_epoch().count() != 0)
            ? std::chrono::duration<float>(now - last_auto_change_t_).count()
            : 1e6f;
        if (since_last_change >= kAutoLockHoldSec) {
            auto_locked_ = true;
        }
    }
}

void BeatStage::process(AnalysisFrame& frame, const config::Settings& cfg) {
    const float flux_low   = frame.flux_low.value_or(0.0f);
    const float flux_mid   = frame.flux_mid.value_or(0.0f);
    const float flux_high  = frame.flux_high.value_or(0.0f);
    const float flux_total = frame.flux_total.value_or(0.0f);

    // ----- dt ----------------------------------------------------------
    const auto now = frame.captured_at;
    float dt_sec = 1.0f / kEnvFps;
    if (last_t_.time_since_epoch().count() != 0) {
        dt_sec = std::chrono::duration<float>(now - last_t_).count();
        dt_sec = std::clamp(dt_sec, 0.001f, 0.250f);
    }
    last_t_ = now;

    // ----- Apply mode / profile to working parameters -------------------
    const int mode_int = static_cast<int>(cfg.beat.mode);
    const bool mode_changed = (last_mode_ != mode_int);
    last_mode_ = mode_int;

    using Mode = config::BeatConfig::Mode;
    switch (cfg.beat.mode) {
        case Mode::Auto:
            // working_strength_ is what the auto controller has converged
            // to (or carries over from a previous mode); weights and decay
            // use the neutral defaults.
            weight_low_ = 1.00f; weight_mid_ = 0.70f; weight_high_ = 0.50f;
            decay_tau_sec_ = 0.150f;
            // On a fresh switch into Auto, reset the lock indicator so the
            // user sees "Adapting…" until we re-stabilise.
            if (mode_changed) {
                auto_locked_ = false;
                last_auto_change_t_ = now;
            }
            break;
        case Mode::Profile: {
            const auto pp = profile_params(cfg.beat.profile);
            working_strength_ = pp.strength;
            weight_low_  = pp.weight_low;
            weight_mid_  = pp.weight_mid;
            weight_high_ = pp.weight_high;
            decay_tau_sec_ = pp.decay_tau_sec;
            auto_locked_ = false;
            break;
        }
        case Mode::Custom:
            working_strength_ = std::clamp(cfg.beat.pulse_strength, 0.05f, 3.0f);
            weight_low_ = 1.00f; weight_mid_ = 0.70f; weight_high_ = 0.50f;
            decay_tau_sec_ = 0.150f;
            auto_locked_ = false;
            break;
    }

    // ----- Per-band baselines + variance -------------------------------
    auto update_baseline = [](float& baseline, float& var, float flux) {
        const float dev = flux - baseline;
        baseline = kBaselineAlpha * baseline + (1.0f - kBaselineAlpha) * flux;
        var      = kBaselineAlpha * var      + (1.0f - kBaselineAlpha) * dev * dev;
    };
    update_baseline(baseline_low_,  var_low_,  flux_low);
    update_baseline(baseline_mid_,  var_mid_,  flux_mid);
    update_baseline(baseline_high_, var_high_, flux_high);

    // ----- Onset detection ---------------------------------------------
    const float n_sigma = kSigmaBase / std::max(working_strength_, 0.05f);

    const float os_low  = onset_strength(flux_low,  baseline_low_,  var_low_,  n_sigma) * weight_low_;
    const float os_mid  = onset_strength(flux_mid,  baseline_mid_,  var_mid_,  n_sigma) * weight_mid_;
    const float os_high = onset_strength(flux_high, baseline_high_, var_high_, n_sigma) * weight_high_;
    const float onset   = std::max({os_low, os_mid, os_high});

    const float since_last_onset = (last_onset_t_.time_since_epoch().count() != 0)
        ? std::chrono::duration<float>(now - last_onset_t_).count()
        : 1e6f;
    const bool in_refractory = since_last_onset < kRefractorySec;
    bool onset_fired = false;
    if (onset > 0.0f && !in_refractory) {
        beat_value_   = std::max(beat_value_, onset);
        last_onset_t_ = now;
        onset_fired = true;
    }

    // Smooth exponential decay.
    beat_value_ *= std::exp(-dt_sec / decay_tau_sec_);
    frame.beat   = std::clamp(beat_value_, 0.0f, 1.0f);

    // ----- Auto adaptation ---------------------------------------------
    if (cfg.beat.mode == Mode::Auto) {
        const float audio_energy = baseline_low_ + baseline_mid_ + baseline_high_;
        update_auto_strength(dt_sec, onset_fired, audio_energy);
    }

    // ----- UI / instrumentation -----------------------------------------
    frame.beat_pulse_strength = working_strength_;
    frame.beat_auto_locked    = (cfg.beat.mode == Mode::Auto) && auto_locked_;

    // ----- Tempo / phase (instrumentation) -----------------------------
    if (envelope_.size() < kEnvCapacity) envelope_.resize(kEnvCapacity, 0.0f);
    envelope_[env_head_] = flux_total;
    env_head_ = (env_head_ + 1) % kEnvCapacity;
    env_size_ = std::min(env_size_ + 1, kEnvCapacity);

    if (++frames_since_tempo_update_ >= 25) {
        frames_since_tempo_update_ = 0;
        update_tempo();
    }
    frame.tempo_bpm        = tempo_bpm_;
    frame.tempo_confidence = tempo_confidence_;
    frame.tempo_detected   = tempo_detected_;

    if (tempo_detected_ && tempo_bpm_ > 0.0f) {
        beat_phase_ += dt_sec * tempo_bpm_ / 60.0f;
        beat_phase_ -= std::floor(beat_phase_);
        if (onset_fired && tempo_confidence_ > 0.3f) {
            const float wrapped = beat_phase_ - std::round(beat_phase_);
            const float err = -wrapped;
            beat_phase_ += kPhaseKp * err;
            tempo_bpm_  += kPhaseKi * err * 60.0f;
            beat_phase_ -= std::floor(beat_phase_);
        }
    } else {
        beat_phase_ = 0.0f;
    }
    frame.beat_phase = beat_phase_;
}

}  // namespace lw::dsp
