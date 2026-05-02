cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(Listeningway VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- ReShade SDK ---------------------------------------------------------
option(RESHADE_SDK_PATH "Path to the ReShade SDK include directory"
       "${CMAKE_SOURCE_DIR}/third_party/reshade/include")
if(NOT RESHADE_SDK_PATH OR NOT EXISTS "${RESHADE_SDK_PATH}/reshade.hpp")
    message(FATAL_ERROR
        "ReShade SDK path not found or invalid. Run prepare.bat first. "
        "Current value: '${RESHADE_SDK_PATH}'")
endif()
message(STATUS "Using ReShade SDK from: ${RESHADE_SDK_PATH}")

# --- Dependencies (vcpkg manifest) --------------------------------------
find_package(KissFFT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(readerwriterqueue CONFIG REQUIRED)

# Vendored OpenRGB SDK (third_party/Youda008-OpenRGB-cppSDK).
add_subdirectory(third_party/Youda008-OpenRGB-cppSDK EXCLUDE_FROM_ALL)

# --- The addon target ----------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    # Top-level entry
    src/listeningway_addon.cpp
    src/listeningway_constants.h
    src/app.h
    src/app.cpp

    # Boot scheduler (defers heavy init off the loader lock)
    src/boot/scheduler.h
    src/boot/scheduler.cpp

    # Audio source layer
    src/audio/source/source_format.h
    src/audio/source/i_audio_source.h
    src/audio/source/off_source.h
    src/audio/source/wasapi_loopback_source.h
    src/audio/source/wasapi_loopback_source.cpp
    src/audio/source/process_audio_source.h
    src/audio/source/process_audio_source.cpp

    # Ring buffer layer
    src/audio/ring/frame_ring.h

    # DSP pipeline layer
    src/audio/dsp/analysis_frame.h
    src/audio/dsp/i_dsp_stage.h
    src/audio/dsp/pipeline.h
    src/audio/dsp/pipeline.cpp
    src/audio/dsp/stage_profile.h
    src/audio/dsp/stages/volume_stage.h
    src/audio/dsp/stages/fft_stage.h
    src/audio/dsp/stages/fft_stage.cpp
    src/audio/dsp/stages/bands_stage.h
    src/audio/dsp/stages/bands_stage.cpp
    src/audio/dsp/stages/log_boost_stage.h
    src/audio/dsp/stages/equalizer_stage.h
    src/audio/dsp/stages/band_norm_stage.h
    src/audio/dsp/stages/spectral_centroid_stage.h
    src/audio/dsp/stages/flux_stage.h
    src/audio/dsp/stages/beat_stage.h
    src/audio/dsp/stages/beat_stage.cpp
    src/audio/dsp/stages/chronotensity_stage.h
    src/audio/dsp/stages/pan_stage.h
    src/audio/dsp/stages/directional_stage.h
    src/audio/dsp/stages/loudness_stage.h

    # Snapshot publication
    src/audio/snapshot/audio_snapshot.h
    src/audio/snapshot/seqlock_snapshot.h

    # Pipeline orchestrator + state machine
    src/audio/pipeline/audio_system.h
    src/audio/pipeline/audio_system.cpp

    # Configuration
    src/config/setting.h
    src/config/settings.h
    src/config/settings_json.h
    src/config/store.h
    src/config/store.cpp

    # Output
    src/output/shader_contract.h
    src/output/uniform_publisher.h
    src/output/uniform_publisher.cpp
    src/output/i_output_consumer.h
    src/output/consumer_registry.h
    src/output/consumer_registry.cpp
    src/output/uniform_publisher_consumer.h
    src/output/uniform_publisher_consumer.cpp
    src/output/osc_consumer.h
    src/output/osc_consumer.cpp
    src/output/openrgb_consumer.h
    src/output/openrgb_consumer.cpp

    # Overlay (ImGui)
    src/overlay/overlay.h
    src/overlay/overlay.cpp
    src/overlay/overlay_consumer.h
    src/overlay/overlay_consumer.cpp

    # Vendored libraries (third_party)
    third_party/tinyosc/tinyosc.c
    third_party/tinyosc/tinyosc.h

    # Resource file (version info)
    assets/listeningway.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${RESHADE_SDK_PATH}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/reshade/deps/imgui
    ${CMAKE_SOURCE_DIR}/third_party/tinyosc
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    kissfft::kissfft-float
    nlohmann_json::nlohmann_json
    readerwriterqueue::readerwriterqueue
    orgbsdk
    Ole32
    Propsys
    Avrt
    Mmdevapi
    Ws2_32
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /permissive-
        /Zc:preprocessor
        /Zc:__cplusplus
        /MP
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/NODEFAULTLIB:LIBCMT")

    # Vendored libraries: relax /W4 because we don't control upstream warnings.
    set_source_files_properties(third_party/tinyosc/tinyosc.c
        PROPERTIES COMPILE_OPTIONS "/W3")
endif()

# --- Tests ---------------------------------------------------------------
option(LISTENINGWAY_BUILD_TESTS "Build unit and property tests" OFF)
if(LISTENINGWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Configuring Listeningway v${PROJECT_VERSION}")
