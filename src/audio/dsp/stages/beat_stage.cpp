#include "beat_stage.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lw::dsp {

namespace {

// --- Internal constants (no UI exposure) ---------------------------------

// Per-band baseline / variance EMA. ~30-frame time constant, so a sustained
// loud passage lifts the baseline and we stop spuriously firing.
constexpr float kBaselineAlpha = 0.97f;

// Per-band threshold = baseline + N · sigma. N is computed as
// kSigmaBase / pulse_strength so pulse_strength = 1 yields the balanced
// default and higher strength lowers N (more triggers).
constexpr float kSigmaBase = 3.0f;

// Sigma multiples to map (flux - threshold) into onset strength [0, 1].
// Two sigmas above threshold = max strength.
constexpr float kStrengthScale = 2.0f;

// Per-band weights when aggregating: bass dominates, treble accents.
constexpr float kWeightLow  = 1.00f;
constexpr float kWeightMid  = 0.70f;
constexpr float kWeightHigh = 0.50f;

// Refractory: minimum gap between onsets to avoid rapid re-trigger.
// 80 ms accommodates fast genres (~750 BPM cap) without flickering.
constexpr float kRefractorySec = 0.080f;

// Smooth release of the beat curve. tau ~ 150 ms reads as a clean pulse.
constexpr float kDecayTauSec = 0.150f;

// --- Tempo / phase internals ---------------------------------------------

constexpr float kEnvFps         = 100.0f;       // envelope sample rate
constexpr float kBpmMin         = 60.0f;        // tempo search lower bound
constexpr float kBpmMax         = 180.0f;       // one octave to mitigate octave errors
constexpr float kTempoPriorBpm  = 120.0f;       // log-Gaussian prior centre
constexpr float kTempoPriorSig  = 0.7f;         // octaves
constexpr float kPhaseKp        = 0.15f;        // phase pull on confident onset
constexpr float kPhaseKi        = 0.01f;        // BPM drift correction

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
    baseline_low_ = baseline_mid_ = baseline_high_ = 0.0f;
    var_low_ = var_mid_ = var_high_ = 0.0f;
    beat_value_ = 0.0f;
    last_t_ = {};
    last_onset_t_ = {};
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

    // Linearise the envelope ring for autocorrelation.
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
        // Guard against stalls / first frame anomalies.
        dt_sec = std::clamp(dt_sec, 0.001f, 0.250f);
    }
    last_t_ = now;

    // ----- Per-band baselines + variance (auto sensitivity source) -----
    auto update_baseline = [](float& baseline, float& var, float flux) {
        const float dev = flux - baseline;
        baseline = kBaselineAlpha * baseline + (1.0f - kBaselineAlpha) * flux;
        var      = kBaselineAlpha * var      + (1.0f - kBaselineAlpha) * dev * dev;
    };
    update_baseline(baseline_low_,  var_low_,  flux_low);
    update_baseline(baseline_mid_,  var_mid_,  flux_mid);
    update_baseline(baseline_high_, var_high_, flux_high);

    // ----- Onset detection ---------------------------------------------
    const float strength = std::clamp(cfg.beat.pulse_strength, 0.05f, 3.0f);
    const float n_sigma  = kSigmaBase / strength;

    const float os_low  = onset_strength(flux_low,  baseline_low_,  var_low_,  n_sigma) * kWeightLow;
    const float os_mid  = onset_strength(flux_mid,  baseline_mid_,  var_mid_,  n_sigma) * kWeightMid;
    const float os_high = onset_strength(flux_high, baseline_high_, var_high_, n_sigma) * kWeightHigh;
    const float onset   = std::max({os_low, os_mid, os_high});

    // Refractory: don't allow rapid re-triggers.
    const float since_last = (last_onset_t_.time_since_epoch().count() != 0)
        ? std::chrono::duration<float>(now - last_onset_t_).count()
        : 1e6f;
    const bool in_refractory = since_last < kRefractorySec;

    if (onset > 0.0f && !in_refractory) {
        beat_value_   = std::max(beat_value_, onset);
        last_onset_t_ = now;
    }

    // Smooth exponential decay.
    beat_value_ *= std::exp(-dt_sec / kDecayTauSec);
    frame.beat   = std::clamp(beat_value_, 0.0f, 1.0f);

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
        if (onset > 0.0f && tempo_confidence_ > 0.3f) {
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
