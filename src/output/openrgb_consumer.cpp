// ---------------------------------------------
// OpenRgbConsumer implementation. See ADR-0012 for design.
//
// The mapping is intentionally opinionated for "delight with least
// noise" — every controller's LEDs get a freqbands-driven spectrum
// painted with a bass→treble blue-cyan-green-yellow-red ramp, modulated
// by volume_norm and beat. Per-controller customization (zone-specific
// roles, per-zone enable/disable) is a future refinement.
// ---------------------------------------------
#include "openrgb_consumer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <OpenRGB/Client.hpp>
#include <OpenRGB/Color.hpp>
#include <OpenRGB/DeviceInfo.hpp>

#include "../audio/pipeline/audio_system.h"
#include "../audio/snapshot/audio_snapshot.h"
#include "../config/settings.h"
#include "../config/store.h"
#include "../util/log.h"
#include "openrgb_patterns/linear_patterns.h"
#include "openrgb_patterns/matrix_patterns.h"
#include "openrgb_patterns/pattern_common.h"
#include "openrgb_patterns/single_patterns.h"

namespace lw::output {

namespace {

/// Convert our internal RGB to the SDK's color type at the boundary.
inline orgb::Color to_orgb(patterns::ColorRgb c) noexcept {
    return orgb::Color{c.r, c.g, c.b};
}

/// Per-device working buffer: full LED color array, plus one PatternState
/// per zone. Lives across frames so stateful patterns (VU peak hold,
/// spectrogram waterfall) can carry history.
struct DeviceFrame {
    std::vector<orgb::Color>           leds;
    std::vector<patterns::PatternState> zone_states;
};

}  // namespace

OpenRgbConsumer::~OpenRgbConsumer() {
    stop();
}

bool OpenRgbConsumer::is_enabled(const config::Settings& s) const {
    return s.network.openrgb.enabled;
}

void OpenRgbConsumer::disarm_in_settings(config::Settings& s) const {
    s.network.openrgb.enabled = false;
}

bool OpenRgbConsumer::start(AudioSystem& system, HMODULE) {
    if (running_.load()) return true;
    if (!store_) return false;

    system_ = &system;
    wants_disarm_.store(false);   // clear stale request from a previous run
    {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_.clear();
        const auto cfg = store_->snapshot().network.openrgb;
        current_dest_ = cfg.host + ":" + std::to_string(cfg.port);
        current_rate_hz_ = std::clamp(cfg.rate_hz, 5, 60);
        frames_sent_.store(0);
        device_count_.store(0);
        connected_ = false;
    }

    running_.store(true);
    worker_ = std::thread(&OpenRgbConsumer::worker_main, this);
    return true;
}

void OpenRgbConsumer::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    wants_disarm_.store(false);   // clear so a future start() doesn't disarm immediately
    system_ = nullptr;
    {
        std::lock_guard<std::mutex> g(status_mutex_);
        connected_ = false;
        device_count_.store(0);
    }
}

std::string OpenRgbConsumer::status_line() const {
    std::lock_guard<std::mutex> g(status_mutex_);
    if (!running_.load()) return "off";

    char buf[256];
    if (!last_error_.empty()) {
        std::snprintf(buf, sizeof(buf), "%s — error: %s",
                       current_dest_.c_str(), last_error_.c_str());
    } else if (!connected_) {
        std::snprintf(buf, sizeof(buf), "%s — connecting...", current_dest_.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%s @ %d Hz — %d devices, %llu frames",
                       current_dest_.c_str(), current_rate_hz_,
                       device_count_.load(),
                       static_cast<unsigned long long>(frames_sent_.load()));
    }
    return buf;
}

bool OpenRgbConsumer::send_test_packet() {
    if (!store_) return false;
    const auto cfg = store_->snapshot().network.openrgb;

    orgb::Client client("Listeningway-test");
    const auto cs = client.connect(cfg.host, static_cast<uint16_t>(cfg.port));
    if (cs != orgb::ConnectStatus::Success) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = std::string("connect: ") + orgb::enumString(cs);
        return false;
    }

    const auto list = client.requestDeviceList();
    if (list.status != orgb::RequestStatus::Success) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = std::string("device list: ") + orgb::enumString(list.status);
        return false;
    }

    // Flash all LEDs white briefly.
    bool ok = true;
    for (const auto& dev : list.devices) {
        client.switchToCustomMode(dev);
        const std::vector<orgb::Color> frame(dev.leds.size(), orgb::Color::White);
        const auto rs = client.setDeviceColors(dev, frame);
        if (rs != orgb::RequestStatus::Success) ok = false;
    }
    return ok;
}

void OpenRgbConsumer::worker_main() {
    if (!store_ || !system_) {
        running_.store(false);
        return;
    }

    auto cfg = store_->snapshot().network.openrgb;
    int rate_hz = std::clamp(cfg.rate_hz, 5, 60);

    orgb::Client client("Listeningway");
    auto record_error = [&](std::string msg) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = std::move(msg);
        connected_ = false;
    };

    auto try_connect = [&]() -> bool {
        log::info("openrgb: connecting to %s:%d", cfg.host.c_str(), cfg.port);
        const auto cs = client.connect(cfg.host, static_cast<uint16_t>(cfg.port));
        if (cs != orgb::ConnectStatus::Success) {
            const std::string s = orgb::enumString(cs);
            log::error("openrgb: connect failed: %s", s.c_str());
            record_error(std::string("connect: ") + s);
            return false;
        }
        log::info("openrgb: connected");
        std::lock_guard<std::mutex> g(status_mutex_);
        connected_ = true;
        last_error_.clear();
        return true;
    };

    // The DeviceList holds Devices via unique_ptr internally, so we keep the
    // whole DeviceListResult around and iterate by reference. orgb::Device
    // has const members which makes it non-assignable; copying into a
    // std::vector<Device> trips C++20's std::construct_at constraints.
    orgb::DeviceListResult devices_result;

    auto refresh_devices = [&]() -> bool {
        // Diagnostic: ask for the count separately first so we can tell
        // (a) whether the cppSDK's request/reply round-trip works at all,
        // and (b) whether the count we get matches the devices we get
        // back. Logged regardless of debug_logging at error level if the
        // numbers don't add up — this is exactly the diagnostic path users
        // should see in listeningway.log when devices fail to enumerate.
        const auto count_result = client.requestDeviceCount();
        const std::string count_status_str = orgb::enumString(count_result.status);
        const uint32_t reported_count =
            (count_result.status == orgb::RequestStatus::Success)
                ? count_result.count : 0u;
        log::info("openrgb: requestDeviceCount -> status=%s count=%u",
                  count_status_str.c_str(), reported_count);

        // Retry on Success-but-empty: OpenRGB sometimes needs a beat
        // after SetClientName before the device-list query returns the
        // populated list. Three attempts × 250 ms covers any plausible
        // server-side debounce.
        constexpr int kRetries = 3;
        for (int attempt = 0; attempt < kRetries; ++attempt) {
            devices_result = client.requestDeviceList();
            const std::string ls = orgb::enumString(devices_result.status);
            log::info("openrgb: requestDeviceList attempt %d -> status=%s devices=%zu",
                      attempt + 1, ls.c_str(), devices_result.devices.size());
            if (devices_result.status != orgb::RequestStatus::Success) {
                log::error("openrgb: requestDeviceList failed: %s [count was %u / %s]",
                           ls.c_str(), reported_count, count_status_str.c_str());
                record_error(std::string("device list (attempt ")
                              + std::to_string(attempt + 1) + "): " + ls
                              + " [count was " + std::to_string(reported_count)
                              + " / " + count_status_str + "]");
                device_count_.store(0);
                return false;
            }
            if (devices_result.devices.size() > 0) break;
            if (attempt + 1 < kRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }

        const size_t got = devices_result.devices.size();
        device_count_.store(static_cast<int>(got));

        // If we got nothing or got fewer than the count claims, surface
        // the discrepancy in both the status line and the log.
        if (got == 0) {
            log::error("openrgb: device list empty after %d attempts (count was %u / %s) — "
                       "either server-side filter, mode mismatch, or protocol-v3 deserialisation drift",
                       kRetries, reported_count, count_status_str.c_str());
            std::lock_guard<std::mutex> g(status_mutex_);
            last_error_ = std::string("device list empty after ")
                          + std::to_string(kRetries) + " attempts (count: "
                          + std::to_string(reported_count) + " / "
                          + count_status_str + ")";
            return false;
        }
        if (static_cast<uint32_t>(got) != reported_count) {
            log::warn("openrgb: device count mismatch — list returned %zu but count says %u",
                      got, reported_count);
            std::lock_guard<std::mutex> g(status_mutex_);
            last_error_ = std::string("device count mismatch: list returned ")
                          + std::to_string(got) + " but count says "
                          + std::to_string(reported_count);
        }

        // Tally non-empty zones by type for the UI counters.
        int n_single = 0, n_linear = 0, n_matrix = 0;
        size_t dev_idx = 0;
        for (const auto& dev : devices_result.devices) {
            log::info("openrgb: device[%zu]=\"%s\" leds=%zu zones=%zu",
                      dev_idx++, dev.name.c_str(), dev.leds.size(),
                      dev.zones.size());
            const auto rs = client.switchToCustomMode(dev);
            if (rs != orgb::RequestStatus::Success) {
                const std::string ms = orgb::enumString(rs);
                log::warn("openrgb: switchToCustomMode failed for \"%s\": %s",
                          dev.name.c_str(), ms.c_str());
                std::lock_guard<std::mutex> g(status_mutex_);
                last_error_ = std::string("switchToCustomMode failed for ")
                              + dev.name + ": " + ms;
            }
            for (const auto& zone : dev.zones) {
                if (zone.leds_count == 0) continue;
                switch (zone.type) {
                    case orgb::ZoneType::Single: ++n_single; break;
                    case orgb::ZoneType::Linear: ++n_linear; break;
                    case orgb::ZoneType::Matrix: ++n_matrix; break;
                }
                log::info("openrgb:   zone[%u]=\"%s\" type=%s leds=%u (matrix=%ux%u)",
                          zone.idx, zone.name.c_str(),
                          orgb::enumString(zone.type),
                          zone.leds_count, zone.matrix_width, zone.matrix_height);
            }
        }
        count_single_.store(n_single);
        count_linear_.store(n_linear);
        count_matrix_.store(n_matrix);
        return true;
    };

    if (!try_connect()) {
        // Initial connect failed. Signal the registry to disarm the
        // settings flag so the toggle reflects reality.
        wants_disarm_.store(true);
        running_.store(false);
        return;
    }
    refresh_devices();

    // Per-device working storage: parallel to devices_result.devices,
    // resized whenever refresh_devices() runs. Holds the assembled
    // per-LED color array and one PatternState per zone (so stateful
    // patterns — VU peak hold, spectrogram waterfall — carry history
    // across frames).
    std::vector<DeviceFrame> dev_frames;
    auto resize_dev_frames = [&]() {
        dev_frames.assign(devices_result.devices.size(), DeviceFrame{});
        for (uint32_t i = 0; i < devices_result.devices.size(); ++i) {
            const auto& dev = devices_result.devices[i];
            dev_frames[i].leds.assign(dev.leds.size(), orgb::Color{});
            dev_frames[i].zone_states.assign(dev.zones.size(), patterns::PatternState{});
        }
    };
    resize_dev_frames();

    uint64_t cached_settings_version = store_->version();
    auto next_tick = std::chrono::steady_clock::now();
    auto last_frame_t = std::chrono::steady_clock::now();
    int  ticks_since_update_check = 0;

    while (running_.load(std::memory_order_relaxed)) {
        // Refresh settings if changed (rate_hz / brightness).
        const uint64_t v = store_->version();
        if (v != cached_settings_version) {
            cached_settings_version = v;
            const auto new_cfg = store_->snapshot().network.openrgb;
            const int new_rate = std::clamp(new_cfg.rate_hz, 5, 60);
            if (new_rate != rate_hz) {
                std::lock_guard<std::mutex> g(status_mutex_);
                rate_hz = new_rate;
                current_rate_hz_ = rate_hz;
            }
            cfg.brightness = new_cfg.brightness;
            // host/port changes ignored until next start cycle.
        }

        const auto interval = std::chrono::milliseconds(1000 / std::max(rate_hz, 5));
        next_tick += interval;
        std::this_thread::sleep_until(next_tick);
        if (!running_.load(std::memory_order_relaxed)) break;

        // Hot-plug check every ~2 s of wall-clock.
        if (++ticks_since_update_check >= rate_hz * 2) {
            ticks_since_update_check = 0;
            const auto u = client.checkForDeviceUpdates();
            if (u == orgb::UpdateStatus::OutOfDate) {
                refresh_devices();
                resize_dev_frames();
            } else if (u == orgb::UpdateStatus::ConnectionClosed
                    || u == orgb::UpdateStatus::OtherSystemError) {
                record_error("server connection lost; will retry");
                client.disconnect();
                if (!try_connect()) {
                    // Keep running; back off until next tick. Status reflects error.
                    continue;
                }
                refresh_devices();
                resize_dev_frames();
            }
        }

        // dt for stateful patterns (VU peak hold, etc).
        const auto now_t = std::chrono::steady_clock::now();
        const float dt_sec = std::clamp(
            std::chrono::duration<float>(now_t - last_frame_t).count(),
            0.001f, 0.250f);
        last_frame_t = now_t;

        const auto snap        = system_->snapshot();
        const auto cur_cfg     = store_->snapshot().network.openrgb;
        const float brightness = cur_cfg.brightness;

        bool any_failed = false;
        for (uint32_t di = 0; di < devices_result.devices.size(); ++di) {
            const auto& dev = devices_result.devices[di];
            if (dev.leds.empty()) continue;
            auto& dframe = dev_frames[di];
            // Defensive: refresh_devices() should keep these in sync, but
            // re-shape if the caller dropped the ball somewhere.
            if (dframe.leds.size() != dev.leds.size())
                dframe.leds.assign(dev.leds.size(), orgb::Color{});
            if (dframe.zone_states.size() != dev.zones.size())
                dframe.zone_states.assign(dev.zones.size(), patterns::PatternState{});

            // Render each zone into its slice of the device LED array.
            // OpenRGB lays out LEDs per device in zone order: zone[0]'s
            // LEDs come first, then zone[1]'s, etc.
            std::vector<patterns::ColorRgb> zone_buf;
            size_t led_offset = 0;
            for (size_t zi = 0; zi < dev.zones.size(); ++zi) {
                const auto& zone = dev.zones[zi];
                const size_t n = zone.leds_count;
                if (n == 0) continue;
                zone_buf.assign(n, patterns::ColorRgb::black());
                std::span<patterns::ColorRgb> out(zone_buf.data(), n);

                switch (zone.type) {
                    case orgb::ZoneType::Single: {
                        const auto c = patterns::render_single(
                            cur_cfg.pattern_single, snap, dframe.zone_states[zi], brightness);
                        std::fill(out.begin(), out.end(), c);
                        break;
                    }
                    case orgb::ZoneType::Linear:
                        patterns::render_linear(
                            cur_cfg.pattern_linear, snap, out,
                            dframe.zone_states[zi], brightness, dt_sec);
                        break;
                    case orgb::ZoneType::Matrix:
                        patterns::render_matrix(
                            cur_cfg.pattern_matrix, snap,
                            patterns::MatrixGeometry{
                                zone.matrix_width, zone.matrix_height,
                                std::span<const uint32_t>(zone.matrix_values.data(),
                                                           zone.matrix_values.size()),
                            },
                            out, dframe.zone_states[zi], brightness, dt_sec);
                        break;
                }

                // Copy zone buffer into the device-wide LED array at the
                // correct offset.
                for (size_t i = 0; i < n && led_offset + i < dframe.leds.size(); ++i) {
                    dframe.leds[led_offset + i] = to_orgb(out[i]);
                }
                led_offset += n;
            }

            const auto rs = client.setDeviceColors(dev, dframe.leds);
            if (rs != orgb::RequestStatus::Success) {
                any_failed = true;
                if (rs == orgb::RequestStatus::ConnectionClosed) {
                    record_error("server closed connection");
                    client.disconnect();
                    break;  // re-attempt on next tick
                }
            }
        }
        if (!any_failed) {
            frames_sent_.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> g(status_mutex_);
            if (connected_ && !last_error_.empty()) last_error_.clear();
        }

        // If we lost the connection above, retry on next iteration.
        if (!client.isConnected()) {
            try_connect();
            if (client.isConnected()) {
                refresh_devices();
                resize_dev_frames();
            }
        }
    }

    if (client.isConnected()) client.disconnect();
}

}  // namespace lw::output
