# Contributing to Listeningway

Thanks for your interest! This document gets you oriented in the v2 codebase. Architectural rationale lives in [`docs/adr/`](docs/adr/) — read those first if you're touching anything cross-cutting.

## Project overview

Listeningway is a ReShade addon that captures audio, runs a fixed DSP pipeline against it, and publishes the results to ReShade shaders as annotation-bound uniforms. v2 is built on a five-layer pipeline (ADR-0002):

```
IAudioSource → FrameRing → DSP Pipeline → AudioSnapshot → Consumers
                                                          ├─ UniformPublisher
                                                          └─ Overlay
```

Each layer has a single responsibility and a narrow contract. Three layers expose adapter interfaces (`IAudioSource`, `IDspStage`, `IBeatDetector`) — that's it. Everything else is a concrete type.

## Source layout

```
src/
├── audio/
│   ├── source/    IAudioSource implementations
│   │   ├── i_audio_source.h
│   │   ├── source_format.h           (Format / Capabilities / ChannelLayout)
│   │   ├── wasapi_loopback_source.{h,cpp}
│   │   ├── process_audio_source.{h,cpp}
│   │   └── off_source.h
│   ├── ring/      Lock-free SPSC frame queue
│   │   └── frame_ring.h
│   ├── dsp/
│   │   ├── i_dsp_stage.h, analysis_frame.h, pipeline.{h,cpp}
│   │   ├── stage_profile.h           (per-stage EMA-smoothed timings)
│   │   └── stages/   13 stages: volume, FFT, bands, log_boost, equalizer,
│   │                 band_norm, spectral_centroid, flux, beat,
│   │                 chronotensity, pan, directional, loudness
│   ├── snapshot/  AudioSnapshot POD + seqlock
│   │   ├── audio_snapshot.h
│   │   └── seqlock_snapshot.h
│   └── pipeline/  Orchestrator + state machine
│       └── audio_system.{h,cpp}
├── config/        Setting<T>, Settings, Store (nlohmann::json)
├── output/        shader_contract.h, uniform_publisher.{h,cpp}
├── overlay/       ImGui overlay (visual + settings panels)
└── listeningway_addon.cpp   DllMain + ReShade hooks
docs/adr/         Architecture decisions
templates/        Build-time substituted templates (.fxh, .rc)
tests/            GoogleTest unit + replay tests (rapidcheck for properties)
```

## Build

```
prepare.bat   # clones ReShade SDK + vcpkg, installs deps, configures CMake
build.bat     # builds Release, renames to .addon, copies to dist/, deploys
```

Set `LISTENINGWAY_DEPLOY_DIR=...` in your environment to retarget the auto-deploy step (defaults to FFXIV path).

Manual build:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=tools/vcpkg/scripts/buildsystems/vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

Tests:

```
cmake -S . -B build -DLISTENINGWAY_BUILD_TESTS=ON ...
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

Toolchain: MSVC 2022 (17.x+), C++20, vcpkg manifest mode, `x64-windows-static` triplet. Compile flags: `/W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /MP` (warnings-as-errors enforced in CI). See [ADR-0008](docs/adr/0008-language-and-dependencies.md) for pinning rationale.

## How to add things

### A new audio source

1. Implement `lw::source::IAudioSource` in `src/audio/source/<my_source>.{h,cpp}`.
2. Provide a unique `Info::code` string (persisted in settings — pick something stable).
3. Implement `available()` honestly — false on platforms / OS versions where the source can't work; the dropdown will gray it.
4. `open()` reports `Capabilities` (format / typical & worst-case frame counts / latency hint) or `nullopt` on failure.
5. `start()` spins up the capture thread and pushes interleaved float samples to the supplied `FrameSink`.
6. Register in `compose_pipeline` / `init` in [`src/listeningway_addon.cpp`](src/listeningway_addon.cpp) alongside the existing sources.
7. Add the file to [`CMakeLists.txt`](CMakeLists.txt).

Reference implementations: [`wasapi_loopback_source.cpp`](src/audio/source/wasapi_loopback_source.cpp) and [`process_audio_source.cpp`](src/audio/source/process_audio_source.cpp).

### A new DSP stage

1. Implement `lw::dsp::IDspStage` in `src/audio/dsp/stages/<my_stage>.h` (header-only is fine; many existing stages are).
2. `name()` returns a short label (shows in the profiler).
3. `process(AnalysisFrame&, const Settings&)` — read inputs from the frame, write outputs back. Stages downstream of you see your output.
4. Add the stage to `compose_pipeline()` in [`src/listeningway_addon.cpp`](src/listeningway_addon.cpp) at the right ordinal (before consumers, after producers).
5. Add to [`CMakeLists.txt`](CMakeLists.txt).

Stage purity matters: don't read or write global state. The pipeline runs serially on the DSP thread; `Settings` is the only sanctioned cross-cut.

### A new shader uniform

This is a public API surface — coordinate via an issue or PR description first if it isn't already in [STABILITY.md](STABILITY.md).

1. Add a `kFoo = "listeningway_foo"` constant to [`src/output/shader_contract.h`](src/output/shader_contract.h).
2. Add the source field to [`AudioSnapshot`](src/audio/snapshot/audio_snapshot.h). It must remain trivially copyable.
3. Add a `value_or` projection in `AudioSystem::dsp_thread_main` ([`audio_system.cpp`](src/audio/pipeline/audio_system.cpp)) so the field gets populated each frame.
4. Add a publish step to [`src/output/uniform_publisher.cpp`](src/output/uniform_publisher.cpp).
5. Document the uniform in [STABILITY.md](STABILITY.md) (Stable vs Experimental).
6. Add the declaration to [`templates/ListeningwayUniforms.fxh.template`](templates/ListeningwayUniforms.fxh.template) so shader authors get it from the include.

The compile-time `string_view` match in `uniform_publisher.cpp` will fail with a missing-symbol error if step 1 is forgotten.

### A new setting

1. Add a field to the appropriate sub-struct in [`src/config/settings.h`](src/config/settings.h) with a sensible default.
2. Add it to the `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` line for that sub-struct in [`src/config/settings_json.h`](src/config/settings_json.h).
3. Surface it in the overlay ([`src/overlay/overlay.cpp`](src/overlay/overlay.cpp)) using `slider_row` / `combo_row` / `checkbox_row` helpers under the appropriate section.
4. Read it in your DSP stage via `const Settings& s` passed to `process`.

The overlay change persists atomically through the `Store`'s mutate-and-bump-version path; the DSP thread picks up the new value on the next frame.

## Code style

- C++20. Concepts / `std::span` / `std::jthread` / designated initializers are encouraged where they earn their keep.
- Naming: types `PascalCase`, functions `snake_case` (project-wide), constants `kPascalCase`, namespaces `lowercase`, member variables `name_` (trailing underscore).
- RAII everywhere; `std::unique_ptr` for ownership transfer; no raw `new`/`delete` in normal code paths.
- Prefer small, focused files. Most stages are < 200 lines.
- Comment only the non-obvious *why*. Don't narrate what the code already says.
- See [ADR-0008](docs/adr/0008-language-and-dependencies.md) for the toolchain rationale.

## Testing

Unit tests use GoogleTest; property tests use rapidcheck (ADR-0006). Run with `ctest --test-dir build`. Aim:

- New stages: at least one unit test covering the impulse / steady-state behavior.
- Cross-cutting changes (snapshot layout, seqlock): property tests that the writer / reader can't observe a torn snapshot.
- Bug fixes: a regression test in `tests/` that fails before the fix, passes after.

Coverage isn't gated, but `tests/replay/` accepts WAV-driven golden traces if you have one for an issue you're fixing.

## Pull requests

1. Fork, branch from `main`, push.
2. Build green (`build.bat`); tests green (`ctest`).
3. Touched a public API surface (uniforms, settings JSON, source codes)? Update [STABILITY.md](STABILITY.md) and [CHANGELOG.md](CHANGELOG.md). For a new architectural decision, add an ADR in `docs/adr/`.
4. PR description: what changed, why, and (for behavior changes) how you verified it in-game.

For broader / fuzzier work, open an issue first to align on the approach — saves both of us a round trip.

---

Happy hacking.
