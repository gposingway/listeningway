// ---------------------------------------------
// Store — settings persistence and live access (ADR-0004)
//
// Holds the canonical Settings, an atomic version counter for hot-reload
// detection by DSP stages, and load/save helpers backed by nlohmann/json.
// ---------------------------------------------
#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include "settings.h"

namespace lw::config {

class Store {
public:
    explicit Store(std::filesystem::path path);

    /// Load from disk. Missing file → defaults are kept; `false` returned.
    /// JSON parse error or unknown schema → defaults are kept; `false` returned.
    bool load();

    /// Save current settings to disk. Returns false on I/O error.
    bool save() const;

    /// Lock-free read of the version counter; bumped on every publish().
    /// Stages compare against their cached version to detect hot reloads.
    uint64_t version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }

    /// Take a thread-safe copy of the current settings.
    Settings snapshot() const;

    /// Replace the live settings and bump the version. Validates clamping
    /// at value-set time before publishing.
    void publish(Settings new_settings);

    /// Mutate the current settings via a functor; equivalent to
    /// `auto s = snapshot(); fn(s); publish(std::move(s));`.
    template <typename Fn>
    void mutate(Fn&& fn) {
        auto s = snapshot();
        fn(s);
        publish(std::move(s));
    }

    const std::filesystem::path& path() const { return path_; }

private:
    void clamp_in_place(Settings& s) const;

    mutable std::mutex mutex_;
    Settings           settings_;
    std::atomic<uint64_t> version_ {0};
    std::filesystem::path path_;
};

}  // namespace lw::config
