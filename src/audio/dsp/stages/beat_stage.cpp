#include "beat_stage.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lw::dsp {

void BeatStage::reset() {
    envelope_.clear();
    env_head_ = env_size_ = 0;
    thresh_window_.clear();
    thresh_head_ = 0;
    beat_value_ = 0.0f;
    last_t_ = {};
    last_onset_t_ = {};
    tempo_bpm_ = 0.0f;
    tempo_confidence_ = 0.0f;
    tempo_detected_ = false;
    beat_phase_ = 0.0f;
    frames_since_tempo_update_ = 0;
}

bool BeatStage::detect_onset(float flux, const config::BeatConfig& bc) {
    // Maintain a sliding threshold window. Aubio formula:
    //   threshold = median(window) + λ · mean(window)
    // Window length derived from threshold_window_ms; frame rate is the
    // process() call rate (the source's typical_frame_count drives this).
    // We approximate frame rate as 100 Hz; window in samples = ms / 10.
    const size_t target_w = std::max<size_t>(
        4, static_cast<size_t>(bc.threshold_window_ms * 0.1f));
    if (thresh_window_.size() < target_w) {
        thresh_window_.resize(target_w, 0.0f);
        thresh_head_ = 0;
    }
    thresh_window_[thresh_head_] = flux;
    thresh_head_ = (thresh_head_ + 1) % thresh_window_.size();

    // Compute median + mean.
    std::vector<float> sorted = thresh_window_;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];
    float mean = 0.0f;
    for (float v : thresh_window_) mean += v;
    mean /= static_cast<float>(thresh_window_.size());

    const float threshold = median + bc.threshold_lambda * mean;
    return flux > threshold;
}

void BeatStage::update_tempo(const config::BeatConfig& bc) {
    if (env_size_ < 32) {
        tempo_bpm_ = 0.0f;
        tempo_confidence_ = 0.0f;
        tempo_detected_ = false;
        return;
    }

    // Linearize the envelope into a contiguous span for autocorrelation.
    std::vector<float> env(env_size_);
    for (size_t i = 0; i < env_size_; ++i) {
        const size_t idx = (env_head_ + kEnvCapacity - env_size_ + i) % kEnvCapacity;
        env[i] = envelope_[idx];
    }
    // Subtract mean so zero-flux regions don't bias the autocorrelation.
    float mean = 0.0f;
    for (float v : env) mean += v;
    mean /= static_cast<float>(env.size());
    for (float& v : env) v -= mean;

    // Sample rate of the envelope ≈ 100 Hz (one entry per process call,
    // assuming ~10 ms hops on typical loopback).
    constexpr float kEnvFps = 100.0f;
    // Search BPM range 50..220.
    constexpr float kBpmMin = 50.0f;
    constexpr float kBpmMax = 220.0f;
    const int lag_min = static_cast<int>(60.0f * kEnvFps / kBpmMax);
    const int lag_max = std::min(static_cast<int>(60.0f * kEnvFps / kBpmMin),
                                 static_cast<int>(env.size() - 1));
    if (lag_min >= lag_max) {
        tempo_bpm_ = 0.0f;
        tempo_confidence_ = 0.0f;
        tempo_detected_ = false;
        return;
    }

    const float prior_bpm = bc.tempo_prior_bpm;
    const float sigma_oct = std::max(0.1f, bc.tempo_prior_sigma);

    // Autocorrelation with log-Gaussian tempo prior.
    float best_score = 0.0f;
    int best_lag = lag_min;
    float second_best = 0.0f;
    for (int lag = lag_min; lag <= lag_max; ++lag) {
        float ac = 0.0f;
        for (size_t i = 0; i + lag < env.size(); ++i) {
            ac += env[i] * env[i + lag];
        }
        const float bpm = 60.0f * kEnvFps / static_cast<float>(lag);
        const float octaves = std::log2(bpm / prior_bpm);
        const float prior = std::exp(-(octaves * octaves)
                                       / (2.0f * sigma_oct * sigma_oct));
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
    const float flux = frame.flux_total.value_or(0.0f);

    // Append to envelope ring.
    if (envelope_.size() < kEnvCapacity) envelope_.resize(kEnvCapacity, 0.0f);
    envelope_[env_head_] = flux;
    env_head_ = (env_head_ + 1) % kEnvCapacity;
    env_size_ = std::min(env_size_ + 1, kEnvCapacity);

    auto now = frame.captured_at;
    float dt_sec = 1.0f / 100.0f;
    if (last_t_.time_since_epoch().count() != 0) {
        dt_sec = std::chrono::duration<float>(now - last_t_).count();
    }
    last_t_ = now;

    // Detect onset with refractory.
    const auto& bc = cfg.beat;
    bool is_beat = detect_onset(flux, bc);
    if (is_beat) {
        const float since_last = std::chrono::duration<float>(now - last_onset_t_).count();
        if (last_onset_t_.time_since_epoch().count() != 0
            && since_last < bc.refractory_ms * 0.001f) {
            is_beat = false;
        }
    }

    // Beat envelope: spike to 1.0, decay.
    if (is_beat) {
        beat_value_ = 1.0f;
        last_onset_t_ = now;
    } else {
        beat_value_ = std::max(0.0f, beat_value_ - bc.beat_decay_per_sec * dt_sec);
    }
    frame.beat = beat_value_;

    // Recompute tempo every ~250 ms (every ~25 frames at 100 Hz).
    if (++frames_since_tempo_update_ >= 25) {
        frames_since_tempo_update_ = 0;
        update_tempo(bc);
    }
    frame.tempo_bpm = tempo_bpm_;
    frame.tempo_confidence = tempo_confidence_;
    frame.tempo_detected = tempo_detected_;

    // PLL beat-phase tracker. Phase advances at the locked tempo; on a
    // confident detected onset, soft-correct toward expected phase 0.
    if (tempo_detected_ && tempo_bpm_ > 0.0f) {
        beat_phase_ += dt_sec * tempo_bpm_ / 60.0f;
        beat_phase_ -= std::floor(beat_phase_);
        if (is_beat && tempo_confidence_ > 0.3f) {
            // Phase error: distance to nearest integer beat (mod 1).
            const float wrapped = beat_phase_ - std::round(beat_phase_);
            const float err = -wrapped;  // pull toward zero
            beat_phase_ += bc.phase_kp * err;
            tempo_bpm_  += bc.phase_ki * err * 60.0f;  // tiny BPM nudge
            beat_phase_ -= std::floor(beat_phase_);
        }
    } else {
        beat_phase_ = 0.0f;
    }
    frame.beat_phase = beat_phase_;
}

}  // namespace lw::dsp
