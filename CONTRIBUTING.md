# Contributing to Listeningway

Thank you for your interest in contributing to Listeningway! This project is a modular audio analysis and visualization addon for ReShade, designed for clarity, extensibility, and real-time performance.

## Project Overview
- **Purpose:** Capture system audio, analyze it in real time, and expose results to ReShade shaders via uniforms.
- **Key Modules:**
  - `audio_capture.*` — WASAPI audio capture (uses `AudioAnalysisConfig` for all tunables)
  - `audio_analysis.*` — Real-time audio feature extraction (uses `AudioAnalysisConfig`)
  - `uniform_manager.*` — Uniform caching, update logic, and extensibility for all Listeningway_* uniforms
  - `overlay.*` — ImGui debug overlay
  - `logging.*` — Thread-safe logging
  - `listeningway_addon.cpp` — Main entry point and integration
  - `settings.cpp/settings.h` — All tunable parameters are grouped in the `ListeningwaySettings` struct and loaded/saved atomically

## Build Instructions
1. **Clone the repository** and initialize submodules if any.
2. **Install [vcpkg](https://github.com/microsoft/vcpkg)** and run `vcpkg install` in the repo root (see `vcpkg.json`).
3. **Configure with CMake:**
   ```
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
4. **Build the solution** using Visual Studio or `cmake --build . --config Release`.

## Code Style & Documentation
- Use modern C++ (C++17 or later).
- Group code by module and responsibility.
- Use Doxygen-style comments for all public classes, structs, and functions (see existing modules for examples).
- Add high-level comments to each file and function.
- Use `// ...existing code...` in code reviews to indicate unchanged regions.
- **To generate API documentation:**
  - Run Doxygen with the provided `Doxyfile` in `third_party/reshade`.

## How to Extend
- **Add new analysis features:** Implement in `audio_analysis.*` and update `AudioAnalysisData`. Use the `AudioAnalysisConfig` struct for all tunables.
- **Expose new uniforms:** Add to `uniform_manager.*` and update the uniform update logic. All uniform extensibility is now managed in this module.
- **Add overlay elements:** Extend `overlay.*` for new debug visualizations.
- **Log for debugging:** Use `LogToFile()` for diagnostics.
- **Settings:** All tunables are now grouped in `ListeningwaySettings` and accessed via struct fields, not legacy globals.

## Key Extension Points
- Audio analysis: `AnalyzeAudioBuffer()` in `audio_analysis.cpp` (uses `AudioAnalysisConfig`)
- Beat detection: Two implementations in `simple_energy_beat_detector.cpp` and `spectral_flux_auto_beat_detector.cpp`
- Frequency enhancement: Bell curve multipliers for mid and high frequencies in `audio_analysis.cpp`
- Uniform export: `UniformManager` in `uniform_manager.*`
- Overlay: `DrawListeningwayDebugOverlay()` in `overlay.cpp`
- Settings: All tunables are grouped in `ListeningwaySettings` and loaded/saved atomically

## Submitting Pull Requests
- Fork the repo and create a feature branch.
- Ensure your code builds and passes any tests.
- Document your changes with Doxygen comments.
- Open a pull request with a clear description of your changes.

---
For questions or suggestions, please open an issue or discussion on GitHub. Happy coding!