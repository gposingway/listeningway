cmake_minimum_required(VERSION 3.21)

# Honor MSVC_RUNTIME_LIBRARY target property so subprojects can pick their CRT.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(Listeningway VERSION 2.0.0 LANGUAGES CXX)

# --- Language ------------------------------------------------------------
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

# --- The addon target ----------------------------------------------------
# Day 0: minimal stub. Subsequent days expand src/ per ADR-0002:
#   src/audio/source/      IAudioSource implementations
#   src/audio/ring/        FrameRing (moodycamel-based)
#   src/audio/dsp/         IDspStage implementations
#   src/audio/snapshot/    SeqlockSnapshot, AudioSnapshot type
#   src/audio/pipeline/    Pipeline + AudioSystem orchestrator
#   src/config/            Setting<T>, Settings, Store
#   src/output/            shader_contract.h, UniformPublisher
#   src/overlay/           ImGui panels
add_library(${PROJECT_NAME} SHARED
    src/listeningway_addon.cpp
    src/listeningway_constants.h
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
# Re-enabled on Day 2+ when the first DSP stages land alongside their tests.
# The legacy v1 test suite lives in tests.harvest/ as read-only reference.
option(LISTENINGWAY_BUILD_TESTS "Build unit and property tests" OFF)
if(LISTENINGWAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Configuring Listeningway v${PROJECT_VERSION} (Day 0 stub)")
