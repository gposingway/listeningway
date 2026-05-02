# Listeningway unit tests
#
# Tests run a small in-process subset of the codebase: the Configuration value
# object, validation, BeatProfile presets, AudioFormat utilities, and 7.1
# channel layout state. The full audio pipeline (WASAPI / ReShade) is not
# exercised here; those layers live behind the AudioPipeline boundary.

find_package(GTest CONFIG REQUIRED)

# Stub out the few addon-side symbols that the source-under-test references
# at link time but doesn't actually exercise from these tests.
add_library(listeningway_test_stubs STATIC test_stubs.cpp)
target_include_directories(listeningway_test_stubs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/audio
)
target_compile_definitions(listeningway_test_stubs PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

add_executable(listeningway_tests
    test_configuration.cpp
    test_beat_profiles.cpp
    test_audio_format_utils.cpp
    test_channel_layout.cpp
    # Sources under test (kept compact and self-contained where possible)
    ${CMAKE_SOURCE_DIR}/src/configuration/Configuration.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/analysis/beat_profiles.cpp
)

target_include_directories(listeningway_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/audio/analysis
    ${CMAKE_SOURCE_DIR}/src/audio/beat_detection
    ${CMAKE_SOURCE_DIR}/src/configuration
    ${CMAKE_SOURCE_DIR}/src/utils
)

target_compile_definitions(listeningway_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Match the static-runtime CRT used by vcpkg's x64-windows-static triplet so
# gtest's static archive links cleanly.
if(MSVC)
    set_target_properties(listeningway_tests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_target_properties(listeningway_test_stubs PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_property(TARGET listeningway_tests PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(listeningway_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    listeningway_test_stubs
)

include(GoogleTest)
gtest_discover_tests(listeningway_tests)
