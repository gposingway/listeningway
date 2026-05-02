#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#include <imgui.h>

#include "overlay.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../audio/pipeline/audio_system.h"
#include "../config/store.h"

namespace lw {

namespace {

const char* state_label(State s) {
    switch (s) {
        case State::Off:      return "Off";
        case State::Starting: return "Starting";
        case State::Running:  return "Running";
        case State::Stopping: return "Stopping";
        case State::Error:    return "Error";
    }
    return "?";
}

}  // namespace

void draw_overlay(reshade::api::effect_runtime*,
                  AudioSystem& system,
                  config::Store& store) {
    const auto snap = system.snapshot();
    auto cfg = store.snapshot();

    if (ImGui::CollapsingHeader("Audio source", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("State: %s", state_label(system.state()));
        ImGui::Text("Format: %.0f ch @ frame %llu", snap.audio_format,
                    static_cast<unsigned long long>(snap.frame_index));

        const auto sources = system.available_sources();
        std::vector<std::string> names;
        names.reserve(sources.size());
        int current_idx = 0;
        for (size_t i = 0; i < sources.size(); ++i) {
            names.push_back(sources[i].display);
            if (sources[i].code == cfg.audio.capture_source_code) {
                current_idx = static_cast<int>(i);
            }
        }
        std::vector<const char*> name_ptrs;
        for (auto& n : names) name_ptrs.push_back(n.c_str());

        int sel = current_idx;
        if (ImGui::Combo("Source", &sel, name_ptrs.data(),
                         static_cast<int>(name_ptrs.size()))) {
            if (sel >= 0 && sel < static_cast<int>(sources.size())) {
                system.switch_source(sources[sel].code);
            }
        }
    }

    if (ImGui::CollapsingHeader("Levels", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ProgressBar(std::clamp(snap.volume, 0.0f, 1.0f),
                           ImVec2(-1, 0), "Volume");
        ImGui::ProgressBar(std::clamp(snap.volume_norm * 0.25f, 0.0f, 1.0f),
                           ImVec2(-1, 0), "Volume Norm (×4 = full bar)");
        ImGui::ProgressBar(std::clamp(snap.loudness, 0.0f, 1.0f),
                           ImVec2(-1, 0), "Loudness (K-weighted)");
        ImGui::Text("Pan: %+0.2f    L: %.2f  R: %.2f",
                    snap.audio_pan, snap.volume_left, snap.volume_right);
    }

    if (ImGui::CollapsingHeader("Beat / Tempo", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ProgressBar(std::clamp(snap.beat, 0.0f, 1.0f),
                           ImVec2(-1, 0), "Beat");
        ImGui::ProgressBar(std::clamp(snap.beat_phase, 0.0f, 1.0f),
                           ImVec2(-1, 0), "Beat Phase");
        ImGui::Text("Tempo: %.1f BPM (conf %.0f%%, %s)",
                    snap.tempo_bpm,
                    snap.tempo_confidence * 100.0f,
                    snap.tempo_detected ? "locked" : "unlocked");
        ImGui::Text("Spectral centroid: %.2f", snap.spectral_centroid);
        ImGui::Text("Phases: vol=%.2f bass=%.2f treble=%.2f",
                    snap.phase_volume, snap.phase_bass, snap.phase_treble);
    }

    if (ImGui::CollapsingHeader("Frequency bands", ImGuiTreeNodeFlags_DefaultOpen)) {
        const uint32_t n = std::min<uint32_t>(snap.freq_band_count, 64);
        ImGui::Text("Bands: %u (raw)", n);
        for (uint32_t i = 0; i < n; ++i) {
            ImGui::ProgressBar(std::clamp(snap.freq_bands[i] * cfg.frequency.amplifier_bands,
                                            0.0f, 1.0f),
                               ImVec2(-1, 8.0f), "");
        }
    }

    if (ImGui::CollapsingHeader("Directional intensity")) {
        const char* labels[8] = { "F", "FR", "R", "BR", "B", "BL", "L", "FL" };
        for (int i = 0; i < 8; ++i) {
            ImGui::Text("%-3s", labels[i]);
            ImGui::SameLine();
            ImGui::ProgressBar(std::clamp(snap.direction8[i] * cfg.frequency.amplifier_direction,
                                            0.0f, 1.0f),
                               ImVec2(-1, 0), "");
        }
    }

    if (ImGui::CollapsingHeader("Settings (basic)")) {
        bool dirty = false;
        dirty |= ImGui::SliderFloat("Volume amp",     &cfg.frequency.amplifier_volume,    1.0f, 11.0f);
        dirty |= ImGui::SliderFloat("Bands amp",      &cfg.frequency.amplifier_bands,     1.0f, 11.0f);
        dirty |= ImGui::SliderFloat("Direction amp",  &cfg.frequency.amplifier_direction, 1.0f, 11.0f);
        dirty |= ImGui::SliderFloat("Pan smoothing",  &cfg.audio.pan_smoothing,            0.0f, 1.0f);
        dirty |= ImGui::SliderFloat("Pan offset",     &cfg.audio.pan_offset,              -1.0f, 1.0f);
        dirty |= ImGui::SliderFloat("AGC window (s)", &cfg.agc.window_seconds,             0.5f, 30.0f);

        if (dirty) {
            store.publish(cfg);
        }
        if (ImGui::Button("Save settings to disk")) {
            store.save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload from disk")) {
            store.load();
        }
    }
}

}  // namespace lw
