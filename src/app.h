// ---------------------------------------------
// lw::App — Listeningway's lifecycle coordinator.
//
// Owns the order in which subsystems come up and go down. The DLL entry
// point (listeningway_addon.cpp) knows nothing about audio, consumers,
// settings, or the rest of it — it just constructs an App, asks the
// boot scheduler to drive it via tick_boot(), and asks it to stop() on
// unload. Adding a new subsystem is a one-file change here.
//
// `tick_boot()` is a phased state machine: each call advances at most
// one step, returning Continue while more work remains. The outer boot
// scheduler in DllMain calls it once per render frame, so each frame's
// init overhead stays sub-millisecond.
// ---------------------------------------------
#pragma once

#include <memory>

#include <windows.h>

#include "boot/scheduler.h"

namespace lw {

class AudioSystem;
namespace config { class Store; }
namespace output { class ConsumerRegistry; }

class App {
public:
    explicit App(HMODULE addon_module);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// Advance the boot state machine by one step. Returns Done when
    /// initialization is complete, Continue while more steps remain,
    /// Failed if a step couldn't complete. Idempotent after Done/Failed:
    /// subsequent calls return the terminal state without doing work.
    boot::StepResult tick_boot();

    /// Once-per-frame post-boot maintenance: drives consumer self-disarm
    /// polling and any other periodic work. Cheap no-op until boot
    /// completes successfully.
    void tick_running();

    /// Tear down all subsystems in reverse order of bring-up. Idempotent
    /// and partial-init-safe — handles any boot phase, including the
    /// pre-init state where nothing was constructed yet.
    void stop();

private:
    enum class Phase {
        LoadSettings,         // Store::load
        BuildAudioSystem,     // sources + DSP pipeline composition
        StartAudio,           // WASAPI Initialize + capture/DSP threads (heaviest)
        BuildConsumers,       // construct internal + network consumers
        StartConsumers,       // hook ReShade events; spawn enabled network workers
        Running,              // boot complete (terminal)
        Failed,               // boot halted (terminal)
    };

    HMODULE module_;
    Phase   phase_ = Phase::LoadSettings;

    std::unique_ptr<config::Store>            store_;
    std::unique_ptr<AudioSystem>              system_;
    std::unique_ptr<output::ConsumerRegistry> consumers_;
};

}  // namespace lw
