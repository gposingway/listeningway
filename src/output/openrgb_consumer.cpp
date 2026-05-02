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

namespace lw::output {

namespace {

/// Bass→treble color ramp. t in [0, 1].
orgb::Color ramp_color(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    // Five-stop palette: blue → cyan → green → yellow → red.
    static constexpr float stops[5][3] = {
        {0.10f, 0.20f, 1.00f}, // blue
        {0.10f, 1.00f, 1.00f}, // cyan
        {0.10f, 1.00f, 0.30f}, // green
        {1.00f, 1.00f, 0.10f}, // yellow
        {1.00f, 0.20f, 0.10f}, // red
    };
    const float scaled = t * 4.0f;
    const int   lo = static_cast<int>(std::floor(scaled));
    const int   hi = std::min(lo + 1, 4);
    const float frac = scaled - static_cast<float>(lo);
    const float r = stops[lo][0] * (1 - frac) + stops[hi][0] * frac;
    const float g = stops[lo][1] * (1 - frac) + stops[hi][1] * frac;
    const float b = stops[lo][2] * (1 - frac) + stops[hi][2] * frac;
    return orgb::Color{
        static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
    };
}

/// Build a per-LED color buffer for a device of `led_count` LEDs from the
/// audio snapshot. Default mapping: spread freqbands across the LEDs as a
/// spectrum, modulated by volume_norm and beat, with the configured
/// brightness multiplier.
std::vector<orgb::Color> compose_frame(const AudioSnapshot& snap,
                                        size_t led_count,
                                        float brightness) {
    std::vector<orgb::Color> out(led_count);
    if (led_count == 0) return out;

    const size_t nb = std::min<size_t>(snap.freq_band_count, kMaxBands);
    const float  beat_flash = std::clamp(snap.beat, 0.0f, 1.0f);
    // volume_norm is AGC-normalized around 1.0 (clamped 0..~4); soften it.
    const float  vol = std::clamp(snap.volume_norm * 0.5f, 0.0f, 1.5f);
    const float  bright = std::clamp(brightness, 0.0f, 1.0f);

    for (size_t i = 0; i < led_count; ++i) {
        // Position in spectrum, 0..1.
        const float t = led_count <= 1 ? 0.5f
                       : static_cast<float>(i) / static_cast<float>(led_count - 1);

        // Sample freqbands at this position. If we have no bands, fall back
        // to a flat volume color.
        float band_val = 0.0f;
        if (nb > 0) {
            const float src_idx = t * static_cast<float>(nb - 1);
            const int   lo = static_cast<int>(std::floor(src_idx));
            const int   hi = std::min(lo + 1, static_cast<int>(nb) - 1);
            const float frac = src_idx - static_cast<float>(lo);
            band_val = snap.freq_bands[lo] * (1 - frac) + snap.freq_bands[hi] * frac;
        }

        // Color: ramp by spectrum position, intensity by band amplitude × volume.
        orgb::Color c = ramp_color(t);
        const float intensity = std::clamp(
            (band_val * 1.5f + vol * 0.3f + beat_flash * 0.4f) * bright,
            0.0f, 1.0f);
        c.r = static_cast<uint8_t>(c.r * intensity);
        c.g = static_cast<uint8_t>(c.g * intensity);
        c.b = static_cast<uint8_t>(c.b * intensity);
        out[i] = c;
    }
    return out;
}

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
        const auto cs = client.connect(cfg.host, static_cast<uint16_t>(cfg.port));
        if (cs != orgb::ConnectStatus::Success) {
            record_error(std::string("connect: ") + orgb::enumString(cs));
            return false;
        }
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
        devices_result = client.requestDeviceList();
        if (devices_result.status != orgb::RequestStatus::Success) {
            record_error(std::string("device list: ")
                          + orgb::enumString(devices_result.status));
            device_count_.store(0);
            return false;
        }
        // Switch each device to custom mode so direct LED writes take effect.
        for (const auto& dev : devices_result.devices) {
            const auto rs = client.switchToCustomMode(dev);
            if (rs != orgb::RequestStatus::Success) {
                // Some devices have a custom mode that's hard to reach;
                // log and continue (they'll just stay on whatever mode).
                std::lock_guard<std::mutex> g(status_mutex_);
                last_error_ = std::string("switchToCustomMode failed for ")
                              + dev.name + ": " + orgb::enumString(rs);
            }
        }
        device_count_.store(static_cast<int>(devices_result.devices.size()));
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

    uint64_t cached_settings_version = store_->version();
    auto next_tick = std::chrono::steady_clock::now();
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
            } else if (u == orgb::UpdateStatus::ConnectionClosed
                    || u == orgb::UpdateStatus::OtherSystemError) {
                record_error("server connection lost; will retry");
                client.disconnect();
                if (!try_connect()) {
                    // Keep running; back off until next tick. Status reflects error.
                    continue;
                }
                refresh_devices();
            }
        }

        const auto snap = system_->snapshot();
        bool any_failed = false;
        for (const auto& dev : devices_result.devices) {
            if (dev.leds.empty()) continue;
            auto frame = compose_frame(snap, dev.leds.size(), cfg.brightness);
            const auto rs = client.setDeviceColors(dev, frame);
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
            }
        }
    }

    if (client.isConnected()) client.disconnect();
}

}  // namespace lw::output
