#include "scheduler.h"

namespace lw::boot {

void Scheduler::enqueue(std::string name, Step step) {
    steps_.push_back({std::move(name), std::move(step)});
}

bool Scheduler::tick() {
    if (failed_ || empty()) return false;
    auto& entry = steps_[next_];
    const StepResult r = entry.step ? entry.step() : StepResult::Done;
    switch (r) {
        case StepResult::Done:     ++next_;        break;
        case StepResult::Continue: /* stay put */  break;
        case StepResult::Failed:   failed_ = true; break;
    }
    return true;
}

std::string_view Scheduler::last_step_name() const noexcept {
    if (next_ == 0 && !failed_) return {};
    const size_t idx = failed_ ? next_ : next_ - 1;
    return steps_[idx].name;
}

}  // namespace lw::boot
