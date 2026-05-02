// ---------------------------------------------
// Pipeline — ordered sequence of IDspStage (ADR-0002)
//
// Owns the stages, validates composition order at startup, runs them in
// sequence on each AnalysisFrame.
// ---------------------------------------------
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "i_dsp_stage.h"

namespace lw::dsp {

class Pipeline {
public:
    Pipeline() = default;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Append a stage. Order matters: later stages can read fields earlier
    /// stages wrote. Composition is validated by `validate()`.
    void add(std::unique_ptr<IDspStage> stage) {
        stages_.push_back(std::move(stage));
    }

    void clear() { stages_.clear(); }
    size_t size() const noexcept { return stages_.size(); }

    /// Verify that every field a stage reads is produced by an earlier stage
    /// (or is `Samples`/`Format`, which are always present from capture).
    /// Returns an error description on failure, empty string on success.
    std::string validate() const;

    /// Run all stages in order, transforming `frame` in place.
    void process(AnalysisFrame& frame, const config::Settings& cfg) {
        for (auto& stage : stages_) {
            stage->process(frame, cfg);
        }
    }

    /// Reset stage state (after stop/restart or format change).
    void reset() {
        for (auto& stage : stages_) {
            stage->reset();
        }
    }

    /// Diagnostic: list of stage names in order.
    std::vector<std::string_view> stage_names() const {
        std::vector<std::string_view> names;
        names.reserve(stages_.size());
        for (const auto& s : stages_) names.push_back(s->name());
        return names;
    }

private:
    std::vector<std::unique_ptr<IDspStage>> stages_;
};

}  // namespace lw::dsp
