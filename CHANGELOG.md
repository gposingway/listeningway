# Changelog

All notable changes to Listeningway will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
- Added: Directional intensity uniforms (Listeningway_Direction8 and FRBL aliases) with mono/stereo/surround mapping, plus overlay rose diagram.
- Added: Split Amplifier controls (Volume/Bands/Direction) with distinct UI colors; boosts apply to uniforms and overlay visuals only.

### Fixed
- Resolved freezes when switching Audio Analysis provider from System → Off → System.
  - Off now clears all analysis data immediately (no frozen visuals).
  - Provider dropdown remains interactive during switching; no UI lockout.
  - Cold-start logic added when returning from Off to ensure analyzer/capture restarts.
- Default provider selection now respects the provider marked `is_default` (System Audio).

### Added
- Runtime toggle for SIMD-accelerated analysis (SSE path with scalar fallback).
- Performance modules and caching:
  - SPSC ring buffer between capture and analysis.
  - Hann window and FFT/band mapping cache reuse.
  - Background TempoWorker for BPM estimation.
- Beat Profiles with a Custom option; removed legacy per-parameter beat UI clutter.

### Changed
- Overlay provider selection now binds to `audio.captureProviderCode` and stays enabled during transitions.
- “Off” provider explicitly zeros tempo/beat/pan/format/volumes to avoid stale display.
- Documentation refreshed: provider model, SIMD toggle, configuration fields, and module READMEs.

## [1.2.0.3] - 2025-06-07

### New Features
- Added documentation files clarifying core, audio, configuration, and utility module responsibilities.
- Introduced scaffolding for threading utilities.
- Added `audio.panOffset` configuration parameter for user panning adjustment with smoothing support.

### Refactor
- Restructured audio and provider modules for better organization and clarity.
- Updated include paths across the project to reflect new directory structure.
- Removed process-specific audio capture providers and related code.
- Removed several UI renderer components and associated files.
- Removed the `tools/reshade` subproject and all related references.
- Renamed and clarified audio capture provider classes for consistency.
- Introduced `AudioCaptureManager` class to manage audio capture providers and selection.
- Improved error handling in system audio capture provider.
- Refined audio capture manager logic to track provider state and transitions.

### Documentation
- Updated documentation to reflect new paths for configuration files and third-party tools.

### Chores
- Updated version information to 1.2.0.3.
- Adjusted build and deployment scripts to align with new directory structure and third-party dependencies.

### Style
- Improved and clarified file header comments across multiple files.
- Replaced hardcoded UI constants with named constants for overlay elements and sliders.
- Updated beat detector class names for clarity and consistency.

### Bug Fixes
- Refined audio analysis frequency band calculations with improved logarithmic gain application.
- Enhanced pan calculation logic with deadzone and surround channel handling.

## [1.2.0.2] - 2025-06-05

### New Features
- Centralized configuration management introduced for unified settings handling.
- Live application of configuration changes to audio capture and analysis systems.
- Added new configuration options for frequency bands, FFT size, normalization, and sample rate.
- Audio system lifecycle controls added: restart, stop, and apply configuration dynamically.

### Improvements
- Audio analysis and capture now use thread-safe, centralized configuration snapshots.
- Configuration persistence switched to JSON format for better portability.
- Enhanced validation and fallback for audio capture providers.
- Audio overlay shader redesigned with comprehensive visual audio data display.
- Refined audio capture and analysis logic to remove legacy global settings dependencies.

### Bug Fixes
- Improved verification of ImGui dependency to prevent partial or corrupted setups.

### Chores
- Version number updated to 1.2.0.2.
- Resource file version info synchronized with the new version.
- Deployment script updated to ensure shader directory exists and copy shader files with warnings.

### Refactor
- Legacy INI-based settings replaced with modern configuration manager.
- Updated audio module function signatures to adopt new configuration system.
- Streamlined internal code structure for maintainability and thread safety.
- Removed redundant configuration change notifications from UI handlers.
- Consolidated and renamed configuration manager files to lowercase for consistency.

## [1.2.0.0] - 2025-06-04

### Added
- **Amplifier Setting for Overlay and Uniforms**
  - New "Amplifier" slider in the overlay UI (range: 1.0–11.0) under Pan Smooth, both full width.
  - Multiplies all overlay visualizations and Listeningway_* uniforms (volume, beat, frequency bands, left/right volume) for enhanced visual feedback.
  - Setting is persistent, validated, and does not affect underlying audio analysis.

### Fixed
- Overlay and uniforms now always match for all visualized values, including main volume bar.
- Refined overlay UI: Removed duplicate/incorrect sliders, ensured proper alignment and labeling.

### Technical
- Added `amplifier` field to configuration, with validation and persistence.
- Amplifier is only applied to overlay and uniforms, not to the analysis data.
- Refactored overlay and uniform update logic for consistency and maintainability.

## [1.1.0] - 2025-06-02

### Added
- **Stereo Spatialization Support**: New uniforms for enhanced stereo audio effects
  - `Listeningway_VolumeLeft`: Volume level for left audio channels (0.0 to 1.0)
  - `Listeningway_VolumeRight`: Volume level for right audio channels (0.0 to 1.0)  
  - `Listeningway_AudioPan`: Stereo pan position (-1.0 = full left, 0.0 = center, +1.0 = full right)
- **Audio Format Detection**: New uniform `Listeningway_AudioFormat` 
  - Detects audio format: 0.0=none, 1.0=mono, 2.0=stereo, 6.0=5.1 surround, 8.0=7.1 surround
  - Enables format-specific shader effects and optimizations
- **Pan Smoothing Control**: Configurable smoothing to reduce pan calculation jitter
  - Setting: `PanSmoothing` in Listeningway.ini (0.0 = no smoothing, higher = more smoothing)
  - Default: 0.0 (preserves current behavior)
  - Accessible via overlay UI for real-time adjustment

### Enhanced
- **Surround Sound Support**: Improved pan calculation for 5.1 and 7.1 audio
  - Smart detection of effectively stereo content in surround formats
  - Full [-1, +1] pan range for stereo content, even in surround sound modes
- **Overlay Interface**: Added real-time display of all spatialization metrics
  - Shows left/right volumes, pan position, and detected audio format
  - Aligned display with existing volume and beat indicators

### Technical
- Extended `AudioAnalysisData` structure with new spatialization fields
- Improved pan calculation algorithm with surround sound awareness
- Added exponential moving average smoothing for pan stability
- Updated uniform manager to expose all new audio spatialization uniforms

## [1.1.0.2] - 2025-06-04

### Fixed
- Build system now enforces ReShade v6.3.3 (API 14+) and correct ImGui docking version for compatibility and stability.
- `prepare.bat` script updated to always check out the correct ReShade tag and reset `deps/imgui` to the commit referenced by ReShade, preventing mismatched API or ImGui errors.
- Documentation updated to clarify minimum ReShade version and AuroraShade compatibility.

### Technical
- Automated dependency management for ReShade and ImGui to match project requirements.

## [1.0.x] - Previous Versions

### Features
- Real-time audio analysis with FFT processing
- Beat detection with multiple algorithms (Simple Energy, Spectral Flux + Autocorrelation)
- 32-band frequency analysis with logarithmic/linear mapping options
- 5-band equalizer system for enhanced frequency visualization
- Time-based uniforms for continuous animations
- Comprehensive settings system with overlay UI
- Support for System Audio and Process Audio capture
- Thread-safe audio provider switching
