// ---------------------------------------------
// OscConsumer — OSC sender output adapter (ADR-0011).
//
// User-toggleable network consumer. When enabled, opens a UDP socket and
// pushes OSC messages mirroring the shader uniform names
// (/listeningway/volume, /listeningway/freqbands, ...) to host:port at
// rate_hz. Send-only — no listening port is opened on this machine.
//
// Wire layer: tinyosc (vendored under third_party/tinyosc/, ISC-licensed).
// We bring our own Winsock plumbing (socket / sendto) — tinyosc is
// transport-agnostic by design.
// ---------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "i_output_consumer.h"

namespace lw {
namespace config { class Store; }
}

namespace lw::output {

class OscConsumer final : public IOutputConsumer {
public:
    explicit OscConsumer(config::Store& store) : store_(&store) {}

    std::string_view id() const override { return "osc"; }
    std::string_view display_name() const override { return "OSC (network)"; }
    bool is_user_configurable() const override { return true; }
    bool is_enabled(const config::Settings& s) const override;

    bool start(AudioSystem& system, HMODULE addon_module) override;
    void stop() override;
    std::string status_line() const override;

    /// Send a single test packet (`/listeningway/test`) on demand. Used by
    /// the overlay's "test send" button to verify connectivity without
    /// waiting for the worker tick.
    bool send_test_packet() override;

    bool wants_self_disarm() const override { return wants_disarm_.load(); }
    void disarm_in_settings(config::Settings& s) const override;

private:
    void worker_main();

    config::Store* store_;
    AudioSystem*   system_ = nullptr;

    std::thread       worker_;
    std::atomic<bool> running_{false};

    // Set by worker_main() on hard failure (socket() or DNS resolve) —
    // when the worker is going to exit and the toggle should follow.
    std::atomic<bool> wants_disarm_{false};

    // Status — read from any thread.
    mutable std::mutex    status_mutex_;
    std::string           current_dest_;     ///< "host:port"
    int                   current_rate_hz_ = 0;
    std::atomic<uint64_t> packets_sent_{0};
    std::string           last_error_;
};

}  // namespace lw::output
