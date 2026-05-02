// ---------------------------------------------
// Pipeline — ordered sequence of IDspStage (ADR-0002)
//
// Owns the stages, validates composition order at startup, runs them in
// sequence on each AnalysisFrame, and records per-stage EMA-smoothed
// wall-clock timings (read by the DSP thread for snapshot publication).
// ---------------------------------------------
#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "i_dsp_stage.h"
#include "stage_profile.h"

namespace lw::dsp {

class Pipeline {
public:
    Pipeline() = default;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Append a stage. Order matters: later stages can read fields earlier
    /// stages wrote. Composition is validated by `validate()`.
    void add(std::unique_ptr<IDspStage> stage);

    void clear();
    size_t size() const noexcept { return stages_.size(); }

    /// Verify that every field a stage reads is produced by an earlier stage
    /// (or is `Samples`/`Format`, always present from capture).
    /// Returns an error description on failure, empty string on success.
    std::string validate() const;

    /// Run all stages in order, transforming `frame` in place. Records
    /// per-stage and total elapsed time as EMA-smoothed values.
    void process(AnalysisFrame& frame, const config::Settings& cfg);

    /// Reset stage state (after stop/restart or format change).
    void reset();

    /// Diagnostic: list of stage names in order (for the overlay profiler).
    std::vector<std::string_view> stage_names() const;

    /// EMA-smoothed per-stage timings; first `last_timing_count()` slots are
    /// populated. Read by the DSP thread between process() calls.
    const StageTimings& last_timings() const noexcept { return timings_; }
    size_t              last_timing_count() const noexcept { return stages_.size(); }
    float               last_total_micros() const noexcept { return total_micros_; }

private:
    static constexpr float kEmaAlpha = 0.1f;

    std::vector<std::unique_ptr<IDspStage>> stages_;
    StageTimings timings_{};
    float        total_micros_ = 0.0f;
};

}  // namespace lw::dsp
