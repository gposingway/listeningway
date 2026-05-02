#include "audio_system.h"

#include <algorithm>
#include <chrono>

#include "../dsp/analysis_frame.h"

namespace lw {

AudioSystem::AudioSystem(config::Store* store)
    : store_(store), ring_(64) {}

AudioSystem::~AudioSystem() {
    stop();
}

void AudioSystem::register_source(std::unique_ptr<source::IAudioSource> src) {
    if (src) sources_.push_back(std::move(src));
}

source::IAudioSource* AudioSystem::find_source(std::string_view code) const {
    for (const auto& s : sources_) {
        if (s->info().code == code) return s.get();
    }
    return nullptr;
}

std::vector<source::Info> AudioSystem::available_sources() const {
    std::vector<source::Info> infos;
    for (const auto& s : sources_) {
        if (s->available()) infos.push_back(s->info());
    }
    std::sort(infos.begin(), infos.end(),
              [](const auto& a, const auto& b) { return a.order < b.order; });
    return infos;
}

bool AudioSystem::start() {
    std::lock_guard<std::mutex> g(state_mutex_);
    if (state_.load() != State::Off) return state_.load() == State::Running;
    return start_locked();
}

bool AudioSystem::start_locked() {
    state_.store(State::Starting);

    const auto cfg = store_ ? store_->snapshot() : config::Settings{};
    current_ = find_source(cfg.audio.capture_source_code);
    if (!current_) {
        // Fallback to any default-marked source, then any available.
        for (const auto& s : sources_) {
            if (s->info().is_default && s->available()) { current_ = s.get(); break; }
        }
        if (!current_) {
            for (const auto& s : sources_) {
                if (s->available()) { current_ = s.get(); break; }
            }
        }
    }
    if (!current_) {
        state_.store(State::Error);
        return false;
    }

    auto caps = current_->open();
    if (!caps) {
        state_.store(State::Error);
        return false;
    }

    // Spin up DSP thread before starting source so no frame is dropped.
    dsp_running_.store(true);
    dsp_thread_ = std::thread(&AudioSystem::dsp_thread_main, this);

    // Start source. The capture thread inside the source pushes into the
    // ring via the lambda; we capture `this` by reference safely because
    // stop_locked() joins both threads before destruction.
    const bool ok = current_->start(
        [this](std::span<const float> samples, source::Format fmt) {
            ring_.push(samples, fmt);
        });
    if (!ok) {
        dsp_running_.store(false);
        if (dsp_thread_.joinable()) dsp_thread_.join();
        state_.store(State::Error);
        return false;
    }

    state_.store(State::Running);
    return true;
}

void AudioSystem::stop() {
    std::lock_guard<std::mutex> g(state_mutex_);
    stop_locked();
}

void AudioSystem::stop_locked() {
    if (state_.load() == State::Off) return;
    state_.store(State::Stopping);

    if (current_) {
        current_->stop();
    }
    dsp_running_.store(false);
    if (dsp_thread_.joinable()) dsp_thread_.join();
    pipeline_.reset();

    // Publish a zeroed snapshot so consumers don't see stale values.
    AudioSnapshot zero{};
    zero.captured_at = std::chrono::steady_clock::now();
    snapshot_.publish(zero);

    current_ = nullptr;
    state_.store(State::Off);
}

bool AudioSystem::switch_source(std::string_view code) {
    std::lock_guard<std::mutex> g(state_mutex_);
    stop_locked();
    if (store_) {
        store_->mutate([code](config::Settings& s) {
            s.audio.capture_source_code = std::string(code);
        });
        store_->save();  // persist the change
    }
    return start_locked();
}

void AudioSystem::dsp_thread_main() {
    if (!store_) return;
    config::Settings cached_settings = store_->snapshot();
    uint64_t cached_version = store_->version();

    std::unique_ptr<ring::FrameChunk> chunk;

    while (dsp_running_.load(std::memory_order_relaxed)) {
        // Refresh settings if the version counter advanced.
        const uint64_t current_version = store_->version();
        if (current_version != cached_version) {
            cached_settings = store_->snapshot();
            cached_version = current_version;
        }

        if (!ring_.pop(chunk) || !chunk) {
            // No data yet; sleep a small amount so we don't spin.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        dsp::AnalysisFrame frame;
        frame.samples = std::span<const float>(chunk->samples.data(),
                                                chunk->samples.size());
        frame.format = chunk->format;
        frame.captured_at = std::chrono::steady_clock::now();
        frame.frame_index = frame_index_.fetch_add(1, std::memory_order_relaxed);

        try {
            pipeline_.process(frame, cached_settings);
        } catch (...) {
            // Robustness: swallow any stage exception, recycle and continue.
            ring_.recycle(std::move(chunk));
            continue;
        }

        // Project AnalysisFrame → AudioSnapshot.
        AudioSnapshot snap{};
        snap.captured_at  = frame.captured_at;
        snap.frame_index  = frame.frame_index;
        snap.audio_format = static_cast<float>(frame.format.channels);

        snap.volume       = frame.volume.value_or(0.0f);
        snap.volume_left  = frame.volume_left.value_or(0.0f);
        snap.volume_right = frame.volume_right.value_or(0.0f);
        snap.volume_norm  = frame.volume_norm.value_or(1.0f);
        snap.volume_att   = frame.volume_att.value_or(1.0f);

        // Bands: post-EQ if present, else pre-EQ.
        const auto& src_bands = frame.bands.empty() ? frame.raw_bands : frame.bands;
        const size_t n = std::min(src_bands.size(), kMaxBands);
        for (size_t i = 0; i < n; ++i) snap.freq_bands[i] = src_bands[i];
        snap.freq_band_count = static_cast<uint32_t>(n);
        for (size_t i = 0; i < std::min(frame.raw_bands.size(), kMaxBands); ++i) {
            snap.freq_bands_raw[i] = frame.raw_bands[i];
        }
        snap.freq_bands_16 = frame.bands_16;
        snap.freq_bands_32 = frame.bands_32;

        snap.bass_norm = frame.bass_norm.value_or(1.0f);
        snap.mid_norm  = frame.mid_norm.value_or(1.0f);
        snap.treb_norm = frame.treb_norm.value_or(1.0f);
        snap.bass_att  = frame.bass_att.value_or(1.0f);
        snap.mid_att   = frame.mid_att.value_or(1.0f);
        snap.treb_att  = frame.treb_att.value_or(1.0f);

        snap.spectral_centroid = frame.spectral_centroid.value_or(0.0f);
        snap.loudness          = frame.loudness.value_or(0.0f);

        snap.beat             = frame.beat.value_or(0.0f);
        snap.beat_phase       = frame.beat_phase.value_or(0.0f);
        snap.tempo_bpm        = frame.tempo_bpm.value_or(0.0f);
        snap.tempo_confidence = frame.tempo_confidence.value_or(0.0f);
        snap.tempo_detected   = frame.tempo_detected.value_or(false);

        snap.phase_volume = frame.phase_volume.value_or(0.0f);
        snap.phase_bass   = frame.phase_bass.value_or(0.0f);
        snap.phase_treble = frame.phase_treble.value_or(0.0f);

        snap.audio_pan = frame.audio_pan.value_or(0.0f);
        if (frame.direction8) snap.direction8 = *frame.direction8;

        // Profiler: copy per-stage and total timings from the pipeline.
        snap.stage_timings   = pipeline_.last_timings();
        snap.stage_count     = static_cast<uint32_t>(
            std::min(pipeline_.last_timing_count(), dsp::kMaxStages));
        snap.pipeline_micros = pipeline_.last_total_micros();

        // Update volume history ring.
        volume_history_[volume_history_head_] = snap.volume;
        volume_history_head_ = (volume_history_head_ + 1) % kVolumeHistoryLength;
        snap.volume_history = volume_history_;
        snap.volume_history_head = volume_history_head_;

        // Per-band history ring (frame-major). Storage sized at kMaxBands
        // so band_count changes don't reshape the ring. Inactive columns are
        // left at the previous frame's value (or zero from default-init);
        // the publisher writes only `band_count` columns to the uniform.
        bands_history_[bands_history_head_] = snap.freq_bands;
        bands_history_head_ =
            (bands_history_head_ + 1) % AudioSnapshot::kBandsHistoryFrames;
        snap.freq_bands_history = bands_history_;
        snap.freq_bands_history_head = bands_history_head_;

        snapshot_.publish(snap);

        ring_.recycle(std::move(chunk));
    }
}

}  // namespace lw
