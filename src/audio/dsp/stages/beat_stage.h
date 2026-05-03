// BeatStage — assembly of the clean-room beat tracker submodules.
//
//     CSD ODF  ──→  ODF buffer  ──→  comb_tempo (every kTempoInterval hops)
//        │                                  │
//        └──→  beat_tracker  ←──────────────┘ (current beat_period)
//                  │
//                  ├──→ beat (continuous pulse curve)
//                  ├──→ beat_phase (sub-hop forward-predicted)
//                  ├──→ tempo_bpm / tempo_confidence / tempo_detected
//                  └──→ beat_pulse_strength / beat_auto_locked (UI hints)
//
// The four submodules (odf_csd, odf_buffer, comb_tempo, beat_tracker)
// implement the algorithm. This stage owns one of each and runs them
// in the right order, plus the user-facing Mode / Profile / Custom
// presentation layer.
//
// Mode behaviour (all modes share the same underlying detection — the
// modes only shape the visible Pulse curve and report the working
// strength to the UI):
//
//   Auto    — pulse_strength = 1.0 always; the tempo Viterbi and
//             cumulative-score tracker self-tune. The "Locked" badge
//             lights up once tempo_confidence has been above threshold
//             for ~3 s; before that it shows "Adapting…".
//   Profile — three named presets; each picks a (pulse_strength,
//             decay_tau) pair tuned for one signal class.
//   Custom  — pulse_strength comes from cfg.beat.pulse_strength.
//
// Reads:  Magnitudes, Phases, FluxTotal
// Writes: Beat, BeatPhase, TempoBpm, TempoConfidence, TempoDetected,
//         BeatPulseStrength, BeatAutoLocked
#pragma once

#include <chrono>

#include "../i_dsp_stage.h"
#include "../beat/beat_tracker.h"
#include "../beat/comb_tempo.h"
#include "../beat/odf_buffer.h"
#include "../beat/odf_csd.h"

namespace lw::dsp {

class BeatStage final : public IDspStage {
public:
    std::string_view name() const override { return "beat"; }
    std::span<const FieldId> reads() const override {
        static constexpr FieldId r[] = {
            FieldId::Magnitudes, FieldId::Phases, FieldId::FluxTotal,
        };
        return r;
    }
    std::span<const FieldId> writes() const override {
        static constexpr FieldId w[] = {
            FieldId::Beat, FieldId::BeatPhase,
            FieldId::TempoBpm, FieldId::TempoConfidence, FieldId::TempoDetected,
            FieldId::BeatPulseStrength, FieldId::BeatAutoLocked,
        };
        return w;
    }

    void reset() override;
    void process(AnalysisFrame& frame, const config::Settings& cfg) override;

private:
    // Submodules (clean-room — see docs/adr/research-notes-beat.md).
    beat::OdfCsd      csd_;
    beat::OdfBuffer   odf_buf_;
    beat::CombTempo   comb_;
    beat::BeatTracker tracker_;

    // Pulse-curve state.
    float beat_value_ = 0.0f;
    std::chrono::steady_clock::time_point last_t_{};

    // Tempo update throttle. Recompute every kTempoUpdateHops calls;
    // the tempo is a slow signal and the comb filterbank is the most
    // expensive op in the stage.
    static constexpr int kTempoUpdateHops = 25;  // ~250 ms at 100 Hz envelope
    int frames_since_tempo_update_ = 0;

    // Estimated envelope-update rate (how often this->process gets called),
    // used to convert canonical (86 Hz) beat-period samples to native ones
    // for the BeatTracker.
    float est_hop_rate_hz_ = 100.0f;

    // Auto-mode "Locked" indicator state.
    int   confidence_above_thresh_frames_ = 0;
    bool  auto_locked_ = false;

    // Last published values for snapshot fields (so we keep emitting
    // sensible numbers even on stale frames).
    float last_pulse_strength_ = 1.0f;
};

}  // namespace lw::dsp
