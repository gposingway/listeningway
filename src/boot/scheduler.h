// ---------------------------------------------
// Boot scheduler — phased deferred initialization (ADR-followup to 0010).
//
// Why this exists: DllMain runs under the Windows loader lock. Heavy work
// done in DllMain (thread spawning, WSAStartup, COM init) risks loader-
// lock deadlock when the new thread tries to acquire the loader lock for
// any DLL operation. The fix is to keep DllMain trivial and defer real
// work to a per-frame callback that fires once the loader lock has been
// released — and to spread that work across frames so even a heavy boot
// stays under ~1 ms per tick.
//
// Each step is a named, self-contained closure returning a StepResult.
// The scheduler drives one tick per render frame; a step that needs to
// span multiple frames returns `Continue` and is called again on the
// next tick.
// ---------------------------------------------
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lw::boot {

/// Tri-state result of one scheduler step.
enum class StepResult {
    Done,      ///< step completed successfully; advance to the next step
    Continue,  ///< step needs more time; call me again next tick
    Failed,    ///< step failed; halt the queue
};

class Scheduler {
public:
    using Step = std::function<StepResult()>;

    /// Queue a step. Order of insertion is order of execution.
    void enqueue(std::string name, Step step);

    /// Run the current step once. Returns true if a step was attempted
    /// (regardless of outcome), false if there was nothing to do
    /// (queue drained or scheduler halted).
    ///
    /// On `Done`: advance to next step.
    /// On `Continue`: stay on current step; same step runs next tick.
    /// On `Failed`: halt; `failed()` becomes true.
    bool tick();

    bool empty() const noexcept { return next_ >= steps_.size(); }
    bool done() const noexcept { return empty() && !failed_; }
    bool failed() const noexcept { return failed_; }

    /// Name of the most recently attempted step, or empty if none have run.
    std::string_view last_step_name() const noexcept;

private:
    struct Entry {
        std::string name;
        Step        step;
    };
    std::vector<Entry> steps_;
    size_t             next_   = 0;
    bool               failed_ = false;
};

}  // namespace lw::boot
