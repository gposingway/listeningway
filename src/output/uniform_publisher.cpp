#include "uniform_publisher.h"

#include <cmath>
#include <cstring>

#include "shader_contract.h"

namespace lw::output {

namespace sc = lw::shader_contract;

void publish_uniforms(reshade::api::effect_runtime* runtime,
                      const AudioSnapshot& snap,
                      std::chrono::steady_clock::time_point start_time,
                      std::chrono::steady_clock::time_point now) {
    if (!runtime) return;

    const float t           = std::chrono::duration<float>(now - start_time).count();
    const float phase_60   = std::fmod(t * 60.0f,  1.0f);
    const float phase_120  = std::fmod(t * 120.0f, 1.0f);
    const float total_60   = t * 60.0f;
    const float total_120  = t * 120.0f;

    // Build a "front_left" linear-from-history view for the volume_history
    // uniform that a shader can read in publication order (oldest..newest).
    std::array<float, kVolumeHistoryLength> hist_linear{};
    for (size_t i = 0; i < kVolumeHistoryLength; ++i) {
        const size_t src = (snap.volume_history_head + 1 + i) % kVolumeHistoryLength;
        hist_linear[i] = snap.volume_history[src];
    }

    runtime->enumerate_uniform_variables(nullptr, [&](
        reshade::api::effect_runtime*,
        reshade::api::effect_uniform_variable handle) {
        char source[64] = {};
        if (!runtime->get_annotation_string_from_uniform_variable(handle, "source", source)) {
            return;
        }
        const std::string_view src(source);

        auto write_f1 = [&](float v) {
            runtime->set_uniform_value_float(handle, &v, 1);
        };
        auto write_f = [&](const float* p, uint32_t n) {
            runtime->set_uniform_value_float(handle, p, n);
        };

        // --- v1 preserved ---
        if      (src == sc::kVolume)           write_f1(snap.volume);
        else if (src == sc::kVolumeLeft)       write_f1(snap.volume_left);
        else if (src == sc::kVolumeRight)      write_f1(snap.volume_right);
        else if (src == sc::kAudioPan)         write_f1(snap.audio_pan);
        else if (src == sc::kAudioFormat)      write_f1(snap.audio_format);
        else if (src == sc::kFreqBands)        write_f(snap.freq_bands.data(),
                                                       static_cast<uint32_t>(snap.freq_band_count));
        else if (src == sc::kNumBands)         write_f1(static_cast<float>(snap.freq_band_count));
        else if (src == sc::kBeat)             write_f1(snap.beat);
        else if (src == sc::kTimeSeconds)      write_f1(t);
        else if (src == sc::kTimePhase60Hz)    write_f1(phase_60);
        else if (src == sc::kTimePhase120Hz)   write_f1(phase_120);
        else if (src == sc::kTotalPhases60Hz)  write_f1(total_60);
        else if (src == sc::kTotalPhases120Hz) write_f1(total_120);
        else if (src == sc::kDirection8)       write_f(snap.direction8.data(), 8);
        else if (src == sc::kFront)            write_f1(snap.direction8[0]);
        else if (src == sc::kFrontRight)       write_f1(snap.direction8[1]);
        else if (src == sc::kRight)            write_f1(snap.direction8[2]);
        else if (src == sc::kBackRight)        write_f1(snap.direction8[3]);
        else if (src == sc::kBack)             write_f1(snap.direction8[4]);
        else if (src == sc::kBackLeft)         write_f1(snap.direction8[5]);
        else if (src == sc::kLeft)             write_f1(snap.direction8[6]);
        else if (src == sc::kFrontLeft)        write_f1(snap.direction8[7]);
        // --- v2 additive (Stable) ---
        else if (src == sc::kVolumeNorm)       write_f1(snap.volume_norm);
        else if (src == sc::kVolumeAtt)        write_f1(snap.volume_att);
        else if (src == sc::kBassNorm)         write_f1(snap.bass_norm);
        else if (src == sc::kMidNorm)          write_f1(snap.mid_norm);
        else if (src == sc::kTrebNorm)         write_f1(snap.treb_norm);
        else if (src == sc::kBassAtt)          write_f1(snap.bass_att);
        else if (src == sc::kMidAtt)           write_f1(snap.mid_att);
        else if (src == sc::kTrebAtt)          write_f1(snap.treb_att);
        else if (src == sc::kFreqBands16)      write_f(snap.freq_bands_16.data(), 16);
        else if (src == sc::kFreqBands32)      write_f(snap.freq_bands_32.data(), 32);
        else if (src == sc::kSpectralCentroid) write_f1(snap.spectral_centroid);
        // --- v2 additive (Experimental) ---
        else if (src == sc::kLoudness)         write_f1(snap.loudness);
        else if (src == sc::kBeatPhase)        write_f1(snap.beat_phase);
        else if (src == sc::kTempoBpm)         write_f1(snap.tempo_bpm);
        else if (src == sc::kTempoConfidence)  write_f1(snap.tempo_confidence);
        else if (src == sc::kPhaseVolume)      write_f1(snap.phase_volume);
        else if (src == sc::kPhaseBass)        write_f1(snap.phase_bass);
        else if (src == sc::kPhaseTreble)      write_f1(snap.phase_treble);
        else if (src == sc::kVolumeHistory)    write_f(hist_linear.data(),
                                                       kVolumeHistoryLength);
    });
}

}  // namespace lw::output
