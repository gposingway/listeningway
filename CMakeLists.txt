cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(Listeningway VERSION 2.0.0 LANGUAGES CXX)

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

# --- The addon target ----------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    # Top-level entry
    src/listeningway_addon.cpp
    src/listeningway_constants.h

    # Audio source layer (ADR-0002 layer 1)
    src/audio/source/source_format.h
    src/audio/source/i_audio_source.h
    src/audio/source/off_source.h
    src/audio/source/wasapi_loopback_source.h
    src/audio/source/wasapi_loopback_source.cpp

    # Ring buffer layer (ADR-0002 layer 2)
    src/audio/ring/frame_ring.h

    # DSP pipeline layer (ADR-0002 layer 3)
    src/audio/dsp/analysis_frame.h
    src/audio/dsp/i_dsp_stage.h
    src/audio/dsp/pipeline.h
    src/audio/dsp/pipeline.cpp

    # Snapshot publication (ADR-0002 layer 4)
    src/audio/snapshot/audio_snapshot.h
    src/audio/snapshot/seqlock_snapshot.h

    # Configuration (ADR-0004)
    src/config/setting.h
    src/config/settings.h

    # Resource file (version info)
    assets/listeningway.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${RESHADE_SDK_PATH}
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    kissfft::kissfft-float
    nlohmann_json::nlohmann_json
    readerwriterqueue::readerwriterqueue
    Ole32
    Propsys
    Avrt
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
endif()

# --- Tests ---------------------------------------------------------------
option(LISTENINGWAY_BUILD_TESTS "Build unit and property tests" OFF)
if(LISTENINGWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Configuring Listeningway v${PROJECT_VERSION}")
