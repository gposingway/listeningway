// ---------------------------------------------
// OscConsumer implementation. See ADR-0011.
//
// Wire layer comes from vendored tinyosc (third_party/tinyosc/). We own the
// Winsock socket and the worker thread. tinyosc is stateless: each message
// composes into a caller-owned buffer; we just blit it out via sendto().
// ---------------------------------------------
#include "osc_consumer.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "tinyosc.h"
}

#include "../audio/pipeline/audio_system.h"
#include "../audio/snapshot/audio_snapshot.h"
#include "../config/settings.h"
#include "../config/store.h"

namespace lw::output {

namespace {

constexpr size_t kMaxPacketBytes = 2048;

/// Process-wide refcount for WSAStartup/WSACleanup. ProcessAudioSource and
/// other capture sources also touch Winsock indirectly via MMDevAPI; we
/// add our own startup so we don't depend on someone else having called it.
struct WinsockGuard {
    bool initialized = false;
    bool init() {
        WSADATA wsa{};
        const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
        initialized = (rc == 0);
        return initialized;
    }
    ~WinsockGuard() {
        if (initialized) WSACleanup();
    }
    WinsockGuard() = default;
    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;
};

bool resolve_host(const std::string& host, int port, sockaddr_in& out, std::string& err) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result  = nullptr;

    char port_buf[16] = {};
    std::snprintf(port_buf, sizeof(port_buf), "%d", port);

    const int rc = ::getaddrinfo(host.c_str(), port_buf, &hints, &result);
    if (rc != 0 || !result) {
        err = "getaddrinfo: " + std::to_string(rc);
        if (result) ::freeaddrinfo(result);
        return false;
    }
    std::memcpy(&out, result->ai_addr, sizeof(sockaddr_in));
    ::freeaddrinfo(result);
    return true;
}

/// Compose and send a single-float OSC message. Returns true on send success.
bool send_f1(SOCKET sock, const sockaddr_in& dst, const char* address, float v) {
    char buf[256];
    const uint32_t n = ::tosc_writeMessage(buf, sizeof(buf), address, "f", static_cast<double>(v));
    if (n == 0) return false;
    return ::sendto(sock, buf, static_cast<int>(n), 0,
                    reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)) != SOCKET_ERROR;
}

/// Compose and send an OSC message carrying an array of floats. tinyosc
/// uses repeated 'f' tags rather than the optional [...] bracket syntax;
/// receivers (TouchDesigner / Resolume / VRChat) parse both equivalently.
/// Returns true on success.
bool send_floats(SOCKET sock, const sockaddr_in& dst, const char* address,
                  const float* values, size_t count, char* scratch, size_t scratch_size) {
    if (count == 0) return false;

    // Compose the OSC message manually since tosc_writeMessage takes varargs
    // and we need a runtime-determined argument list. We replicate its layout:
    //   [address, padded to 4]
    //   [",fff..." format, padded to 4]
    //   [count * 4 bytes of big-endian floats]
    const size_t addr_len = std::strlen(address) + 1;
    const size_t addr_padded = (addr_len + 3) & ~size_t{3};

    const size_t fmt_len = 1 /*comma*/ + count + 1 /*null*/;
    const size_t fmt_padded = (fmt_len + 3) & ~size_t{3};

    const size_t total = addr_padded + fmt_padded + count * 4;
    if (total > scratch_size) return false;

    std::memset(scratch, 0, total);
    std::memcpy(scratch, address, addr_len);

    char* fmt = scratch + addr_padded;
    fmt[0] = ',';
    for (size_t i = 0; i < count; ++i) fmt[1 + i] = 'f';

    char* data = scratch + addr_padded + fmt_padded;
    for (size_t i = 0; i < count; ++i) {
        union { float f; uint32_t u; } cvt{};
        cvt.f = values[i];
        // OSC is big-endian on the wire.
        const uint32_t be = htonl(cvt.u);
        std::memcpy(data + i * 4, &be, 4);
    }

    return ::sendto(sock, scratch, static_cast<int>(total), 0,
                    reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)) != SOCKET_ERROR;
}

}  // namespace

bool OscConsumer::is_enabled(const config::Settings& s) const {
    return s.network.osc.enabled;
}

void OscConsumer::disarm_in_settings(config::Settings& s) const {
    s.network.osc.enabled = false;
}

bool OscConsumer::start(AudioSystem& system, HMODULE) {
    if (running_.load()) return true;
    if (!store_) return false;

    system_ = &system;
    wants_disarm_.store(false);
    {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_.clear();
        const auto cfg = store_->snapshot().network.osc;
        current_dest_ = cfg.host + ":" + std::to_string(cfg.port);
        current_rate_hz_ = std::clamp(cfg.rate_hz, 1, 120);
        packets_sent_.store(0);
    }

    running_.store(true);
    worker_ = std::thread(&OscConsumer::worker_main, this);
    return true;
}

void OscConsumer::stop() {
    running_.store(false);
    if (worker_.joinable()) worker_.join();
    wants_disarm_.store(false);
    system_ = nullptr;
}

std::string OscConsumer::status_line() const {
    std::lock_guard<std::mutex> g(status_mutex_);
    if (!running_.load()) return "off";

    char buf[256];
    if (!last_error_.empty()) {
        std::snprintf(buf, sizeof(buf), "%s @ %d Hz — error: %s",
                       current_dest_.c_str(), current_rate_hz_, last_error_.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%s @ %d Hz — %llu packets sent",
                       current_dest_.c_str(), current_rate_hz_,
                       static_cast<unsigned long long>(packets_sent_.load()));
    }
    return buf;
}

bool OscConsumer::send_test_packet() {
    if (!running_.load() || !store_) return false;
    // We can't reach the worker's socket from here; instead, open a
    // short-lived socket for the test send. That keeps the worker thread's
    // socket strictly thread-local.
    const auto cfg = store_->snapshot().network.osc;

    WinsockGuard wsa;
    if (!wsa.init()) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = "WSAStartup failed";
        return false;
    }

    sockaddr_in dst{};
    std::string err;
    if (!resolve_host(cfg.host, cfg.port, dst, err)) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = err;
        return false;
    }

    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = "socket() failed";
        return false;
    }
    const bool ok = send_f1(s, dst, "/listeningway/test", 1.0f);
    ::closesocket(s);
    if (ok) {
        ++packets_sent_;
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_.clear();
    } else {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = "test sendto failed";
    }
    return ok;
}

void OscConsumer::worker_main() {
    if (!store_ || !system_) {
        running_.store(false);
        return;
    }

    WinsockGuard wsa;
    if (!wsa.init()) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = "WSAStartup failed";
        wants_disarm_.store(true);
        running_.store(false);
        return;
    }

    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::lock_guard<std::mutex> g(status_mutex_);
        last_error_ = "socket() failed";
        wants_disarm_.store(true);
        running_.store(false);
        return;
    }

    auto cfg = store_->snapshot().network.osc;
    sockaddr_in dst{};
    {
        std::string err;
        if (!resolve_host(cfg.host, cfg.port, dst, err)) {
            std::lock_guard<std::mutex> g(status_mutex_);
            last_error_ = err;
            ::closesocket(sock);
            wants_disarm_.store(true);
            running_.store(false);
            return;
        }
    }

    uint64_t cached_version = store_->version();
    int rate_hz = std::clamp(cfg.rate_hz, 1, 120);

    char scratch[kMaxPacketBytes];
    auto next_tick = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        // Check for live settings changes (rate_hz only — host/port require restart).
        const uint64_t v = store_->version();
        if (v != cached_version) {
            cached_version = v;
            const auto new_cfg = store_->snapshot().network.osc;
            const int new_rate = std::clamp(new_cfg.rate_hz, 1, 120);
            if (new_rate != rate_hz) {
                std::lock_guard<std::mutex> g(status_mutex_);
                rate_hz = new_rate;
                current_rate_hz_ = rate_hz;
            }
            // host/port changes ignored until next start cycle; documented behavior.
        }

        const auto interval = std::chrono::milliseconds(1000 / std::max(rate_hz, 1));
        next_tick += interval;
        std::this_thread::sleep_until(next_tick);
        if (!running_.load(std::memory_order_relaxed)) break;

        // Snapshot pull (wait-free seqlock read).
        const auto snap = system_->snapshot();

        bool ok = true;
        ok &= send_f1(sock, dst, "/listeningway/volume",          snap.volume);
        ok &= send_f1(sock, dst, "/listeningway/volumeleft",      snap.volume_left);
        ok &= send_f1(sock, dst, "/listeningway/volumeright",     snap.volume_right);
        ok &= send_f1(sock, dst, "/listeningway/volume_norm",     snap.volume_norm);
        ok &= send_f1(sock, dst, "/listeningway/volume_att",      snap.volume_att);
        ok &= send_f1(sock, dst, "/listeningway/bass_norm",       snap.bass_norm);
        ok &= send_f1(sock, dst, "/listeningway/mid_norm",        snap.mid_norm);
        ok &= send_f1(sock, dst, "/listeningway/treb_norm",       snap.treb_norm);
        ok &= send_f1(sock, dst, "/listeningway/bass_att",        snap.bass_att);
        ok &= send_f1(sock, dst, "/listeningway/mid_att",         snap.mid_att);
        ok &= send_f1(sock, dst, "/listeningway/treb_att",        snap.treb_att);
        ok &= send_f1(sock, dst, "/listeningway/audiopan",        snap.audio_pan);
        ok &= send_f1(sock, dst, "/listeningway/audioformat",     snap.audio_format);
        ok &= send_f1(sock, dst, "/listeningway/numbands",        static_cast<float>(snap.freq_band_count));
        ok &= send_f1(sock, dst, "/listeningway/beat",            snap.beat);
        ok &= send_f1(sock, dst, "/listeningway/beat_phase",      snap.beat_phase);
        ok &= send_f1(sock, dst, "/listeningway/tempo_bpm",       snap.tempo_bpm);
        ok &= send_f1(sock, dst, "/listeningway/tempo_confidence",snap.tempo_confidence);
        ok &= send_f1(sock, dst, "/listeningway/phase_volume",    snap.phase_volume);
        ok &= send_f1(sock, dst, "/listeningway/phase_bass",      snap.phase_bass);
        ok &= send_f1(sock, dst, "/listeningway/phase_treble",    snap.phase_treble);
        ok &= send_f1(sock, dst, "/listeningway/spectral_centroid", snap.spectral_centroid);
        ok &= send_f1(sock, dst, "/listeningway/loudness",        snap.loudness);

        // Frequency-band array (variable length).
        const size_t nb = std::min<size_t>(snap.freq_band_count, kMaxBands);
        if (nb > 0) {
            ok &= send_floats(sock, dst, "/listeningway/freqbands",
                              snap.freq_bands.data(), nb, scratch, sizeof(scratch));
        }

        // Directional rose (always 8).
        ok &= send_floats(sock, dst, "/listeningway/direction8",
                          snap.direction8.data(), 8, scratch, sizeof(scratch));

        if (ok) {
            packets_sent_.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> g(status_mutex_);
            if (!last_error_.empty()) last_error_.clear();
        } else {
            std::lock_guard<std::mutex> g(status_mutex_);
            const int err = WSAGetLastError();
            last_error_ = "sendto failed (" + std::to_string(err) + ")";
        }
    }

    ::closesocket(sock);
}

}  // namespace lw::output
