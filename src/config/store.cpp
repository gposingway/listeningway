#include "store.h"
#include "settings_json.h"

#include <algorithm>
#include <fstream>

namespace lw::config {

Store::Store(std::filesystem::path path) : path_(std::move(path)) {
    clamp_in_place(settings_);
    version_.store(1, std::memory_order_release);
}

bool Store::load() {
    std::ifstream f(path_);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j;
        f >> j;
        Settings loaded = j.get<Settings>();
        // Schema gate: future-self ignores unknown versions.
        if (loaded.schema_version != kCurrentSchemaVersion) return false;
        clamp_in_place(loaded);
        {
            std::lock_guard<std::mutex> g(mutex_);
            settings_ = std::move(loaded);
        }
        version_.fetch_add(1, std::memory_order_release);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Store::save() const {
    Settings copy;
    {
        std::lock_guard<std::mutex> g(mutex_);
        copy = settings_;
    }
    try {
        std::ofstream f(path_);
        if (!f.is_open()) return false;
        nlohmann::json j = copy;
        f << j.dump(2);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Settings Store::snapshot() const {
    std::lock_guard<std::mutex> g(mutex_);
    return settings_;
}

void Store::publish(Settings new_settings) {
    clamp_in_place(new_settings);
    {
        std::lock_guard<std::mutex> g(mutex_);
        settings_ = std::move(new_settings);
    }
    version_.fetch_add(1, std::memory_order_release);
}

void Store::clamp_in_place(Settings& s) const {
    s.audio.pan_smoothing = std::clamp(s.audio.pan_smoothing, 0.0f, 1.0f);
    s.audio.pan_offset    = std::clamp(s.audio.pan_offset,   -1.0f, 1.0f);

    s.beat.pulse_strength = std::clamp(s.beat.pulse_strength, 0.0f, 3.0f);

    s.frequency.band_count    = std::clamp(s.frequency.band_count, 8, 128);
    s.frequency.fft_size      = std::clamp(s.frequency.fft_size, 128, 16384);
    s.frequency.log_strength  = std::clamp(s.frequency.log_strength, 0.01f, 1.5f);
    s.frequency.band_norm     = std::clamp(s.frequency.band_norm, 0.001f, 1.0f);
    s.frequency.min_freq      = std::clamp(s.frequency.min_freq, 10.0f, 500.0f);
    s.frequency.max_freq      = std::clamp(s.frequency.max_freq, 2000.0f, 22050.0f);
    if (s.frequency.min_freq >= s.frequency.max_freq) {
        s.frequency.max_freq = s.frequency.min_freq + 1000.0f;
    }
    for (auto& b : s.frequency.equalizer_bands) {
        b = std::clamp(b, 0.0f, 4.0f);
    }
    s.frequency.equalizer_width    = std::clamp(s.frequency.equalizer_width, 0.05f, 0.5f);
    s.frequency.amplifier_volume   = std::clamp(s.frequency.amplifier_volume, 1.0f, 11.0f);
    s.frequency.amplifier_bands    = std::clamp(s.frequency.amplifier_bands, 1.0f, 11.0f);
    s.frequency.amplifier_direction = std::clamp(s.frequency.amplifier_direction, 1.0f, 11.0f);

    s.agc.window_seconds = std::clamp(s.agc.window_seconds, 0.5f, 30.0f);
    s.agc.clamp_max      = std::clamp(s.agc.clamp_max, 1.5f, 8.0f);
    s.agc.att_attack_ms  = std::clamp(s.agc.att_attack_ms, 1.0f, 1000.0f);
    s.agc.att_release_ms = std::clamp(s.agc.att_release_ms, 1.0f, 5000.0f);

    s.chronotensity.gain_volume = std::clamp(s.chronotensity.gain_volume, 0.0f, 5.0f);
    s.chronotensity.gain_bass   = std::clamp(s.chronotensity.gain_bass,   0.0f, 5.0f);
    s.chronotensity.gain_treble = std::clamp(s.chronotensity.gain_treble, 0.0f, 5.0f);

    s.loudness.window_ms = std::clamp(s.loudness.window_ms, 50.0f, 3000.0f);
}

}  // namespace lw::config
