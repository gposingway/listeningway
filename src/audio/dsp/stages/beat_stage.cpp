#include "beat_stage.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lw::dsp {

namespace {

// Tempo confidence threshold for the "Locked" indicator. Tuned for the
// new aubio-style peak/sum confidence formula in CombTempo (typical
// locked-tempo readings sit ~0.05-0.20; under 0.03 is "no clear peak").
constexpr float kConfidenceLockThreshold = 0.05f;

// How many consecutive estimate updates need to be above the threshold
// before we report "Locked". At kTempoUpdateHops = 25 hops × ~10 ms per
// hop, 12 updates is roughly 3 seconds — matches the AudioLink-style
// "stable for a few seconds" expectation.
constexpr int   kLockedHoldUpdates = 12;

// Pulse-curve decay tau in seconds. Shaped per Profile mode below;
// Auto and Custom use the neutral default.
constexpr float kDecayTauSecDefault    = 0.150f;
constexpr float kDecayTauSecPercussive = 0.120f;
constexpr float kDecayTauSecMelodic    = 0.160f;
constexpr float kDecayTauSecSustained  = 0.230f;

// Strength presets for Profile mode.
constexpr float kStrengthPercussive = 1.00f;
constexpr float kStrengthMelodic    = 1.20f;
constexpr float kStrengthSustained  = 1.60f;

struct ModeParams {
    float strength;
    float decay_tau_sec;
};

ModeParams resolve_mode(const config::BeatConfig& bc) {
    using M = config::BeatConfig::Mode;
    using P = config::BeatConfig::Profile;
    switch (bc.mode) {
        case M::Auto:
            return { 1.0f, kDecayTauSecDefault };
        case M::Profile:
            switch (bc.profile) {
                case P::Percussive: return { kStrengthPercussive, kDecayTauSecPercussive };
                case P::Melodic:    return { kStrengthMelodic,    kDecayTauSecMelodic };
                case P::Sustained:  return { kStrengthSustained,  kDecayTauSecSustained };
            }
            return { 1.0f, kDecayTauSecDefault };
        case M::Custom:
            return { std::clamp(bc.pulse_strength, 0.0f, 3.0f), kDecayTauSecDefault };
    }
    return { 1.0f, kDecayTauSecDefault };
}

}  // namespace

void BeatStage::reset() {
    csd_.reset();
    odf_buf_.reset();
    comb_.reset();
    tracker_.reset();
    beat_value_ = 0.0f;
    last_t_ = {};
    frames_since_tempo_update_ = 0;
    est_hop_rate_hz_ = 100.0f;
    confidence_above_thresh_frames_ = 0;
    auto_locked_ = false;
    last_pulse_strength_ = 1.0f;
}

void BeatStage::process(AnalysisFrame& frame, const config::Settings& cfg) {
    const auto mode = resolve_mode(cfg.beat);

    // ----- Update estimated hop rate (smoothed) ------------------------
    const auto now = frame.captured_at;
    if (last_t_.time_since_epoch().count() != 0) {
        const float dt = std::chrono::duration<float>(now - last_t_).count();
        if (dt > 1e-4f && dt < 0.250f) {
            const float instantaneous_hz = 1.0f / dt;
            // Slow EMA so the hop-rate estimate doesn't chase per-frame jitter.
            est_hop_rate_hz_ = 0.95f * est_hop_rate_hz_ + 0.05f * instantaneous_hz;
        }
    }

    // ----- ODF (CSD-HWR) ----------------------------------------------
    float odf = 0.0f;
    if (frame.magnitudes && frame.phases) {
        odf = csd_.process(*frame.magnitudes, *frame.phases);
    }
    odf_buf_.push(odf);

    // ----- Tempo recompute (throttled) ---------------------------------
    if (++frames_since_tempo_update_ >= kTempoUpdateHops) {
        frames_since_tempo_update_ = 0;
        std::array<float, beat::OdfBuffer::kSnapshotLength> snap{};
        if (odf_buf_.snapshot_to(std::span<float>(snap.data(), snap.size()), est_hop_rate_hz_)) {
            comb_.estimate(std::span<const float>(snap.data(), snap.size()));
        }
    }

    // Track the lock indicator.
    if (comb_.confidence() >= kConfidenceLockThreshold) {
        if (++confidence_above_thresh_frames_ >= kLockedHoldUpdates * kTempoUpdateHops) {
            auto_locked_ = true;
        }
    } else {
        confidence_above_thresh_frames_ = 0;
        auto_locked_ = false;
    }

    // ----- Beat tracker (per-hop) --------------------------------------
    // CombTempo returns a beat period in canonical 86 Hz envelope samples.
    // BeatTracker runs at the native ODF rate, so scale the period by
    // (native_rate / canonical_rate).
    const float canonical_period = comb_.beat_period_samples();
    const float native_period = canonical_period > 0.0f
        ? canonical_period * (est_hop_rate_hz_ / 86.0f)
        : (est_hop_rate_hz_ * 0.5f);  // 120 BPM default until a real estimate lands
    const bool beat_due = tracker_.process(odf, native_period);

    // ----- Pulse curve --------------------------------------------------
    const float dt = (last_t_.time_since_epoch().count() != 0)
        ? std::clamp(std::chrono::duration<float>(now - last_t_).count(), 0.001f, 0.250f)
        : (1.0f / kTempoUpdateHops);
    last_t_ = now;

    if (beat_due) {
        beat_value_ = std::clamp(mode.strength, 0.0f, 1.0f);
    }
    beat_value_ *= std::exp(-dt / mode.decay_tau_sec);
    frame.beat = std::clamp(beat_value_, 0.0f, 1.0f);

    // ----- Other outputs -----------------------------------------------
    frame.beat_phase       = tracker_.beat_phase();
    frame.tempo_bpm        = (comb_.beat_period_samples() > 0.0f)
        ? (60.0f * 86.0f / comb_.beat_period_samples())
        : 0.0f;
    frame.tempo_confidence = comb_.confidence();
    frame.tempo_detected   = (comb_.confidence() >= kConfidenceLockThreshold);

    // Snapshot fields for the UI (Mode / Profile / Custom seeding,
    // Locked / Adapting badge).
    last_pulse_strength_      = mode.strength;
    frame.beat_pulse_strength = last_pulse_strength_;
    frame.beat_auto_locked    = (cfg.beat.mode == config::BeatConfig::Mode::Auto) && auto_locked_;
}

}  // namespace lw::dsp
