// ---------------------------------------------
// lw::App implementation — phased boot state machine.
//
// One file, one job: spell out the order subsystems come up and go down.
// `tick_boot()` advances exactly one phase per call; the outer boot
// scheduler calls it once per render frame so each frame's init cost
// stays sub-millisecond. The heaviest single phase is `StartAudio`
// (WASAPI Initialize + capture/DSP thread spawn) and it has the frame
// to itself.
//
// If you're adding a new source / DSP stage / consumer / output, this
// is where the wiring belongs.
// ---------------------------------------------
#include "app.h"

#include <filesystem>

#include "audio/pipeline/audio_system.h"
#include "audio/source/off_source.h"
#include "audio/source/process_audio_source.h"
#include "audio/source/wasapi_loopback_source.h"
#include "audio/dsp/stages/volume_stage.h"
#include "audio/dsp/stages/fft_stage.h"
#include "audio/dsp/stages/bands_stage.h"
#include "audio/dsp/stages/log_boost_stage.h"
#include "audio/dsp/stages/equalizer_stage.h"
#include "audio/dsp/stages/band_norm_stage.h"
#include "audio/dsp/stages/spectral_centroid_stage.h"
#include "audio/dsp/stages/flux_stage.h"
#include "audio/dsp/stages/beat_stage.h"
#include "audio/dsp/stages/chronotensity_stage.h"
#include "audio/dsp/stages/pan_stage.h"
#include "audio/dsp/stages/directional_stage.h"
#include "audio/dsp/stages/loudness_stage.h"
#include "config/store.h"
#include "output/consumer_registry.h"
#include "output/openrgb_consumer.h"
#include "output/osc_consumer.h"
#include "output/uniform_publisher_consumer.h"
#include "overlay/overlay_consumer.h"

namespace lw {

namespace {

std::filesystem::path settings_path_next_to_dll(HMODULE module) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(module, buf, MAX_PATH);
    std::filesystem::path p(buf);
    return p.parent_path() / "Listeningway.json";
}

void compose_pipeline(dsp::Pipeline& p) {
    using namespace lw::dsp;
    p.add(std::make_unique<VolumeStage>());
    p.add(std::make_unique<FftStage>());
    p.add(std::make_unique<BandsStage>());
    p.add(std::make_unique<LogBoostStage>());
    p.add(std::make_unique<EqualizerStage>());
    p.add(std::make_unique<BandNormStage>());
    p.add(std::make_unique<SpectralCentroidStage>());
    p.add(std::make_unique<FluxStage>());
    p.add(std::make_unique<BeatStage>());
    p.add(std::make_unique<ChronotensityStage>());
    p.add(std::make_unique<PanStage>());
    p.add(std::make_unique<DirectionalStage>());
    p.add(std::make_unique<LoudnessStage>());
}

}  // namespace

App::App(HMODULE addon_module) : module_(addon_module) {}

App::~App() {
    stop();
}

boot::StepResult App::tick_boot() {
    using boot::StepResult;

    switch (phase_) {
        case Phase::LoadSettings: {
            store_ = std::make_unique<config::Store>(settings_path_next_to_dll(module_));
            store_->load();   // missing file → defaults are kept
            phase_ = Phase::BuildAudioSystem;
            return StepResult::Continue;
        }

        case Phase::BuildAudioSystem: {
            if (!store_) { phase_ = Phase::Failed; return StepResult::Failed; }
            system_ = std::make_unique<AudioSystem>(store_.get());
            system_->register_source(std::make_unique<source::WasapiLoopbackSource>());
            system_->register_source(std::make_unique<source::ProcessAudioSource>());
            system_->register_source(std::make_unique<source::OffSource>());
            compose_pipeline(system_->pipeline());
#ifndef NDEBUG
            // Composition sanity check — verifies stage read/write fields are
            // consistent. Developer aid; release builds skip the cost.
            if (!system_->pipeline().validate().empty()) {
                phase_ = Phase::Failed;
                return StepResult::Failed;
            }
#endif
            phase_ = Phase::StartAudio;
            return StepResult::Continue;
        }

        case Phase::StartAudio: {
            // Heaviest single phase: WASAPI Initialize + spawn capture and
            // DSP threads. Has the frame to itself by design.
            if (!system_ || !system_->start()) {
                phase_ = Phase::Failed;
                return StepResult::Failed;
            }
            phase_ = Phase::BuildConsumers;
            return StepResult::Continue;
        }

        case Phase::BuildConsumers: {
            if (!store_) { phase_ = Phase::Failed; return StepResult::Failed; }
            consumers_ = std::make_unique<output::ConsumerRegistry>();
            consumers_->add(std::make_unique<output::UniformPublisherConsumer>());
            consumers_->add(std::make_unique<overlay::OverlayConsumer>(*store_, *consumers_));
            consumers_->add(std::make_unique<output::OscConsumer>(*store_));
            consumers_->add(std::make_unique<output::OpenRgbConsumer>(*store_));
            phase_ = Phase::StartConsumers;
            return StepResult::Continue;
        }

        case Phase::StartConsumers: {
            if (!consumers_ || !system_ || !store_) {
                phase_ = Phase::Failed;
                return StepResult::Failed;
            }
            consumers_->start_all(*system_, module_, store_->snapshot());
            phase_ = Phase::Running;
            return StepResult::Done;
        }

        case Phase::Running:
            return StepResult::Done;

        case Phase::Failed:
            return StepResult::Failed;
    }
    return StepResult::Failed;  // unreachable
}

void App::tick_running() {
    if (phase_ != Phase::Running) return;
    if (!consumers_ || !system_ || !store_) return;
    consumers_->poll_self_disarm(*system_, module_, *store_);
}

void App::stop() {
    // Reverse construction order. Each ptr is null if the corresponding
    // boot phase didn't run, so this is partial-init-safe.
    if (consumers_) {
        consumers_->stop_all();
        consumers_.reset();
    }
    if (system_) {
        system_->stop();
        system_.reset();
    }
    if (store_) {
        store_->save();
        store_.reset();
    }
}

}  // namespace lw
