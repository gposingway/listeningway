cmake_minimum_required(VERSION 3.15) # vcpkg recommends at least 3.15

project(Listeningway LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # Prefer standard features

# --- ReShade SDK ---
# Option 1: Set this variable via CMake command line or GUI
# cmake .. -DRESHADE_SDK_PATH=third_party/reshade/include
option(RESHADE_SDK_PATH "Path to the ReShade SDK include directory" "${CMAKE_SOURCE_DIR}/third_party/reshade/include")
if(NOT RESHADE_SDK_PATH OR NOT EXISTS "${RESHADE_SDK_PATH}/reshade.hpp")
    message(FATAL_ERROR "ReShade SDK path not found or invalid. Please set RESHADE_SDK_PATH correctly (e.g., path to the 'include' directory containing reshade.hpp). Current value: '${RESHADE_SDK_PATH}'")
endif()
message(STATUS "Using ReShade SDK from: ${RESHADE_SDK_PATH}")
include_directories(${RESHADE_SDK_PATH})

# --- Project Configuration ---
# Add project include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/audio/beat_detection
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/configuration
    ${CMAKE_SOURCE_DIR}/src/utils
)

# Add source file(s) including the resource file
add_library(${PROJECT_NAME} SHARED
    src/core/listeningway_addon.cpp
    src/audio/capture/audio_capture.cpp # moved from src/audio/audio_capture.cpp
    src/audio/capture/audio_capture.h
    src/audio/capture/audio_capture_manager.cpp
    src/audio/capture/audio_capture_manager.h
    src/audio/analysis/audio_analysis.cpp
    src/audio/analysis/audio_analysis.h
    src/audio/analysis/fft_engine.cpp src/audio/analysis/fft_engine.h
    src/audio/analysis/tempo_worker.cpp src/audio/analysis/tempo_worker.h
    src/audio/analysis/ring_buffer.h
    src/core/overlay.cpp src/core/overlay.h
    src/core/beat_settings_panel.cpp src/core/beat_settings_panel.h
    src/audio/analysis/beat_profiles.cpp src/audio/analysis/beat_profiles.h
    src/core/thread_safety_manager.cpp src/core/thread_safety_manager.h
    src/utils/logging.cpp src/utils/logging.h
    src/core/uniform_manager.cpp src/core/uniform_manager.h
    src/core/settings.cpp src/core/settings.h    src/configuration/configuration_manager.cpp src/configuration/configuration_manager.h
    src/configuration/Configuration.cpp src/configuration/Configuration.h
    src/configuration/config_value.cpp src/configuration/config_value.h
    src/audio/capture/providers/audio_capture_provider.h
    src/audio/capture/providers/audio_capture_provider_system.h
    src/audio/capture/providers/audio_capture_provider_system.cpp
    src/audio/capture/providers/audio_capture_provider_off.h
    src/audio/capture/providers/audio_capture_provider_off.cpp
    src/audio/beat_detection/beat_detector.cpp
    src/audio/beat_detection/beat_detector.h
    src/audio/beat_detection/beat_detector_simple_energy.cpp
    src/audio/beat_detection/beat_detector_simple_energy.h
    src/audio/beat_detection/beat_detector_spectral_flux_auto.cpp
    src/audio/beat_detection/beat_detector_spectral_flux_auto.h
    assets/listeningway.rc
)

# Add ImGui include directory specifically for the Listeningway target
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/reshade/deps/imgui
)

# --- Preprocessor Definitions ---
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN # Exclude rarely-used stuff from Windows headers
    NOMINMAX          # Prevent Windows headers from defining min/max macros
    # Add other necessary definitions here
)

# --- Dependencies ---
# Find vcpkg packages (requires CMAKE_TOOLCHAIN_FILE to be set)
find_package(KissFFT CONFIG REQUIRED) # Find package without components

# Link Windows Libraries needed for WASAPI/COM
target_link_libraries(${PROJECT_NAME} PRIVATE
    Ole32           # For COM
    Propsys         # For IMMDeviceEnumerator property store access (safer)
    Avrt            # For audio thread priority management (optional but good)
    Psapi           # For process enumeration and information
    # Add other Windows libs if needed (e.g., Winmm)
)

# Link vcpkg libraries
# Use kissfft-float linkage
target_link_libraries(${PROJECT_NAME} PRIVATE
    kissfft::kissfft-float
)

# Fix LIBCMT conflict warning
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT")
endif()

# --- Output Configuration ---
# Ensure the output DLL is named correctly (e.g., Listeningway.dll)
# CMake usually handles this based on the project name, but you can force it:
# set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Listeningway")

# Optional: Set output directory for the DLL if desired
# set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "Configuring Listeningway ReShade Addon")
