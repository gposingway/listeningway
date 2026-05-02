// ---------------------------------------------
// IDspStage — the adapter for pipeline stages (ADR-0002, ADR-0003)
//
// A stage is a small unit of analysis that transforms an AnalysisFrame.
// Stages compose into a Pipeline; each stage declares what AnalysisFrame
// fields it reads and writes so the pipeline can validate composition order
// at startup.
// ---------------------------------------------
#pragma once

#include <span>
#include <string_view>

#include "analysis_frame.h"
#include "../../config/settings.h"

namespace lw::dsp {

/// Identifier for a field on AnalysisFrame, used by stage dependency
/// declarations. The set of values is closed: stages declare what they
/// produce/consume; the pipeline rejects compositions where a stage reads
/// a field no earlier stage produces.
enum class FieldId {
    Samples,        // input from capture (always present)
    Format,
    Volume, VolumeLeft, VolumeRight, VolumeNorm, VolumeAtt,
    Magnitudes,
    RawBands, Bands, Bands16, Bands32,
    BassNorm, MidNorm, TrebNorm,
    BassAtt,  MidAtt,  TrebAtt,
    SpectralCentroid, Loudness,
    FluxTotal, FluxLow, FluxMid, FluxHigh,
    Beat, BeatPhase, TempoBpm, TempoConfidence, TempoDetected,
    PhaseVolume, PhaseBass, PhaseTreble,
    AudioPan, Direction8,
};

class IDspStage {
public:
    virtual ~IDspStage() = default;

    virtual std::string_view name() const = 0;
    virtual std::span<const FieldId> reads() const = 0;
    virtual std::span<const FieldId> writes() const = 0;

    /// Transform `frame` in place. `cfg` is a const reference to the live
    /// settings snapshot the pipeline holds for this pass. Stages MUST NOT
    /// retain pointers/references into either `frame` or `cfg` past return.
    virtual void process(AnalysisFrame& frame, const config::Settings& cfg) = 0;

    /// Optional: stages with internal state (history buffers, IIR filter
    /// state, beat detector accumulators) reset here when the pipeline is
    /// stopped or the source format changes.
    virtual void reset() {}
};

}  // namespace lw::dsp
