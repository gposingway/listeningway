#include "pipeline.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <sstream>

namespace lw::dsp {

void Pipeline::add(std::unique_ptr<IDspStage> stage) {
    if (!stage) return;
    const size_t slot = stages_.size();
    if (slot < kMaxStages) {
        const auto src = stage->name();
        std::memset(timings_[slot].name, 0, sizeof(timings_[slot].name));
        const size_t n = std::min(src.size(), sizeof(timings_[slot].name) - 1);
        std::memcpy(timings_[slot].name, src.data(), n);
        timings_[slot].micros = 0.0f;
    }
    stages_.push_back(std::move(stage));
}

void Pipeline::clear() {
    stages_.clear();
    timings_ = {};
    total_micros_ = 0.0f;
}

std::string Pipeline::validate() const {
    std::set<FieldId> available {FieldId::Samples, FieldId::Format};
    for (size_t idx = 0; idx < stages_.size(); ++idx) {
        const auto& stage = *stages_[idx];
        for (FieldId f : stage.reads()) {
            if (available.find(f) == available.end()) {
                std::ostringstream os;
                os << "Pipeline composition error: stage " << idx
                   << " (" << stage.name() << ") reads field id "
                   << static_cast<int>(f)
                   << " not produced by any earlier stage";
                return os.str();
            }
        }
        for (FieldId f : stage.writes()) {
            available.insert(f);
        }
    }
    return {};
}

void Pipeline::process(AnalysisFrame& frame, const config::Settings& cfg) {
    using clock = std::chrono::steady_clock;
    const auto pipeline_start = clock::now();

    for (size_t i = 0; i < stages_.size(); ++i) {
        const auto t0 = clock::now();
        stages_[i]->process(frame, cfg);
        const auto t1 = clock::now();

        const float micros = std::chrono::duration<float, std::micro>(t1 - t0).count();
        if (i < kMaxStages) {
            auto& slot = timings_[i].micros;
            // EMA: heavy smoothing so the overlay readout is calm.
            slot = (1.0f - kEmaAlpha) * slot + kEmaAlpha * micros;
        }
    }

    const float total = std::chrono::duration<float, std::micro>(
        clock::now() - pipeline_start).count();
    total_micros_ = (1.0f - kEmaAlpha) * total_micros_ + kEmaAlpha * total;
}

void Pipeline::reset() {
    for (auto& stage : stages_) stage->reset();
    for (auto& t : timings_) t.micros = 0.0f;
    total_micros_ = 0.0f;
}

std::vector<std::string_view> Pipeline::stage_names() const {
    std::vector<std::string_view> names;
    names.reserve(stages_.size());
    for (const auto& s : stages_) names.push_back(s->name());
    return names;
}

}  // namespace lw::dsp
