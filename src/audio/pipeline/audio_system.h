// ---------------------------------------------
// AudioSystem — orchestrator: source → ring → DSP thread → snapshot.
// (ADR-0002, ADR-0007)
//
// Owns the AudioCaptureManager-equivalent role: registers sources, picks the
// active one, drives a DSP thread that drains the ring, runs the Pipeline,
// and publishes to the SeqlockSnapshot. State machine:
//   Off → Starting → Running → Stopping → Off | Error
// ---------------------------------------------
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../dsp/pipeline.h"
#include "../ring/frame_ring.h"
#include "../snapshot/seqlock_snapshot.h"
#include "../source/i_audio_source.h"
#include "../../config/store.h"

namespace lw {

enum class State { Off, Starting, Running, Stopping, Error };

class AudioSystem {
public:
    explicit AudioSystem(config::Store* store);
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    /// Register an available source. Done at startup, before start().
    void register_source(std::unique_ptr<source::IAudioSource> src);

    /// Compose the DSP pipeline. Done at startup, before start().
    dsp::Pipeline& pipeline() { return pipeline_; }

    /// Bring the system from Off to Running, using the source code in
    /// settings (`audio.capture_source_code`). Returns false on failure.
    bool start();

    /// Stop the active source and the DSP thread. Idempotent.
    void stop();

    /// Atomic source switch: stop current → set new code → start.
    bool switch_source(std::string_view code);

    /// Latest published analysis snapshot. Lock-free read; thread-safe.
    AudioSnapshot snapshot() const { return snapshot_.read(); }

    /// State machine query.
    State state() const { return state_.load(); }

    /// List of registered sources (for the overlay dropdown).
    std::vector<source::Info> available_sources() const;

private:
    void dsp_thread_main();
    bool start_locked();           // assumes state_mutex_ held
    void stop_locked();            // assumes state_mutex_ held
    source::IAudioSource* find_source(std::string_view code) const;

    config::Store* store_;
    std::vector<std::unique_ptr<source::IAudioSource>> sources_;
    source::IAudioSource* current_ = nullptr;

    ring::FrameRing  ring_;
    dsp::Pipeline    pipeline_;
    SeqlockSnapshot  snapshot_;

    // DSP thread
    std::atomic<bool> dsp_running_ {false};
    std::thread       dsp_thread_;

    // Volume history ring (snapshot-side; updated by DSP thread).
    std::array<float, kVolumeHistoryLength> volume_history_{};
    uint32_t                                  volume_history_head_ = 0;

    // Per-band history ring (frame-major, full resolution up to kMaxBands).
    std::array<std::array<float, kMaxBands>,
               AudioSnapshot::kBandsHistoryFrames> bands_history_{};
    uint32_t bands_history_head_ = 0;

    // Frame counter for snapshot provenance.
    std::atomic<uint64_t> frame_index_ {0};

    std::mutex state_mutex_;
    std::atomic<State> state_ {State::Off};
};

}  // namespace lw
