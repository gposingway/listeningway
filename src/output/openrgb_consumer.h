// ---------------------------------------------
// OpenRgbConsumer — drives an OpenRGB server's RGB peripherals from the
// audio analysis snapshot (ADR-0012).
//
// User-toggleable network consumer. When enabled, connects as a TCP client
// to a running OpenRGB server (default 127.0.0.1:6742), enumerates all
// controllers, switches each to direct/custom mode, and pushes per-LED
// color frames at rate_hz from a worker thread.
//
// We are the *client* — no listening port is opened on this machine.
// Wire layer: Youda008's OpenRGB-cppSDK (vendored under
// third_party/Youda008-OpenRGB-cppSDK/, MIT-licensed). cppSDK handles
// protocol version negotiation, controller enumeration, and the binary
// frame encoding so we don't risk the documented memory-corruption
// failure modes from rolling our own.
// ---------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "i_output_consumer.h"

namespace lw {
namespace config { class Store; }
}

namespace lw::output {

class OpenRgbConsumer final : public IOutputConsumer {
public:
    explicit OpenRgbConsumer(config::Store& store) : store_(&store) {}
    ~OpenRgbConsumer() override;

    std::string_view id() const override { return "openrgb"; }
    std::string_view display_name() const override { return "OpenRGB (RGB peripherals)"; }
    bool is_user_configurable() const override { return true; }
    bool is_enabled(const config::Settings& s) const override;

    bool start(AudioSystem& system, HMODULE addon_module) override;
    void stop() override;
    std::string status_line() const override;

    /// Send a single full-white flash to all LEDs (one frame). Lets the
    /// overlay verify connectivity without waiting for music.
    bool send_test_packet() override;

    bool wants_self_disarm() const override { return wants_disarm_.load(); }
    void disarm_in_settings(config::Settings& s) const override;

private:
    void worker_main();

    config::Store* store_;
    AudioSystem*   system_ = nullptr;

    std::thread       worker_;
    std::atomic<bool> running_{false};

    // Set by worker_main() when initial connect fails or socket creation
    // fails — i.e., when the worker is going to exit and the on/off
    // toggle in settings should follow.
    std::atomic<bool> wants_disarm_{false};

    // Status — read from any thread.
    mutable std::mutex    status_mutex_;
    std::string           current_dest_;       ///< "host:port"
    int                   current_rate_hz_ = 0;
    std::atomic<uint64_t> frames_sent_{0};
    std::atomic<int>      device_count_{0};
    std::string           last_error_;
    bool                  connected_ = false;
};

}  // namespace lw::output
