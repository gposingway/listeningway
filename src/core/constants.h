#pragma once

// Enums for Magic Numbers - replacing hardcoded constants with named values
enum class AudioFormat : int {
    None = 0,
    Mono = 1,
    Stereo = 2,
    Surround51 = 6,
    Surround71 = 8
};

enum class EqualizerBand : int {
    Bass = 0,        // Low frequencies
    LowMid = 1,      // Low-mid frequencies  
    Mid = 2,         // Mid frequencies
    HighMid = 3,     // High-mid frequencies
    Treble = 4       // High frequencies
};

enum class BeatDetectionAlgorithm : int {
    SimpleEnergy = 0,
    SpectralFluxAuto = 1
};

enum class AudioCaptureProvider : int {
    SystemAudio = 0,
    ProcessAudio = 1
};

// Audio Analysis
constexpr size_t DEFAULT_NUM_BANDS = 32;
constexpr size_t DEFAULT_FFT_SIZE = 512;
constexpr float DEFAULT_FLUX_ALPHA = 0.1f;
constexpr float DEFAULT_FLUX_THRESHOLD_MULTIPLIER = 1.5f;

// Beat Detection
constexpr float DEFAULT_BEAT_MIN_FREQ = 0.0f;
constexpr float DEFAULT_BEAT_MAX_FREQ = 500.0f;
constexpr float DEFAULT_FLUX_LOW_ALPHA = 0.1f;
constexpr float DEFAULT_FLUX_LOW_THRESHOLD_MULTIPLIER = 2.0f;

// Spectral Flux with Autocorrelation
constexpr int DEFAULT_BEAT_DETECTION_ALGORITHM = 1;
constexpr float DEFAULT_SPECTRAL_FLUX_THRESHOLD = 0.079f;
constexpr float DEFAULT_TEMPO_CHANGE_THRESHOLD = 0.23f;
constexpr float DEFAULT_BEAT_INDUCTION_WINDOW = 0.12f;
constexpr float DEFAULT_OCTAVE_ERROR_WEIGHT = 0.73f;
constexpr float DEFAULT_SPECTRAL_FLUX_DECAY_MULTIPLIER = 2.94f;

// Audio Analysis Tunables
constexpr float DEFAULT_BEAT_FLUX_MIN = 0.01f;
constexpr float DEFAULT_BEAT_FALLOFF_DEFAULT = 2.0f;
constexpr float DEFAULT_BEAT_TIME_SCALE = 1e-9f;
constexpr float DEFAULT_BEAT_TIME_INITIAL = 0.5f;
constexpr float DEFAULT_BEAT_TIME_MIN = 0.1f;
constexpr float DEFAULT_BEAT_TIME_DIVISOR = 0.2f;
constexpr float DEFAULT_BAND_NORM = 0.1f;
constexpr float DEFAULT_BAND_MIN_FREQ = 183.0f;
constexpr float DEFAULT_BAND_MAX_FREQ = 22050.0f;
constexpr bool  DEFAULT_BAND_LOG_SCALE = true;
constexpr float DEFAULT_BAND_LOG_STRENGTH = 0.1f;

// 5-Band Equalizer
constexpr float DEFAULT_EQUALIZER_BAND1 = 1.11f;
constexpr float DEFAULT_EQUALIZER_BAND2 = 1.29f;
constexpr float DEFAULT_EQUALIZER_BAND3 = 2.11f;
constexpr float DEFAULT_EQUALIZER_BAND4 = 1.8f;
constexpr float DEFAULT_EQUALIZER_BAND5 = 1.63f;
constexpr float DEFAULT_EQUALIZER_WIDTH = 0.15f;

// Audio Capture
// Pan calculation constants
constexpr float DEFAULT_PAN_BALANCE_DEADZONE = 0.02f; // 2% deadzone around center for balanced audio
constexpr float DEFAULT_PAN_SILENCE_THRESHOLD = 0.00001f; // Very low threshold for "silence" detection
constexpr float DEFAULT_PAN_SIGNIFICANT_THRESHOLD = 0.05f; // Threshold for "significant" signal level
// Pan smoothing (0.0 = no smoothing, higher values = more smoothing)
constexpr float DEFAULT_PAN_SMOOTHING = 0.1f; // Default: no smoothing to preserve current behavior
constexpr float DEFAULT_AMPLIFIER = 1.0f;
constexpr float DEFAULT_PAN_OFFSET = 0.0f;

// UI/Overlay tunables
constexpr float DEFAULT_CAPTURE_STALE_TIMEOUT = 1.5f;

// Global feature flags
constexpr bool DEFAULT_AUDIO_ANALYSIS_ENABLED = true;  // Audio analysis enabled by default
constexpr bool DEFAULT_DEBUG_ENABLED = false;          // Debug logging disabled by default

// Overlay/ImGui UI element constants (bar heights, spacings, colors, etc.)
constexpr float OVERLAY_BAR_HEIGHT_THIN = 6.0f;           // Thin bar height for frequency bands and left/right bars
constexpr float OVERLAY_BAR_SPACING_SMALL = 2.0f;         // Small gap between left/right bars
constexpr float OVERLAY_BAR_SPACING_LARGE = 4.0f;         // Spacing after bars
constexpr float OVERLAY_BAR_ROUNDING = 0.0f;              // No rounding for bars
constexpr float OVERLAY_BAR_CENTER_MARKER_THICKNESS = 1.0f; // Center marker line thickness

// Overlay/ImGui color constants (use ImGui::GetColorU32 or IM_COL32)
// These must be defined in overlay.cpp after including ImGui headers.
// constexpr ImU32 OVERLAY_BAR_COLOR_BG = IM_COL32(40, 40, 40, 128);           // Dark background for bars
// constexpr ImU32 OVERLAY_BAR_COLOR_OUTLINE = IM_COL32(60, 60, 60, 128);      // Outline for frequency bars
// constexpr ImU32 OVERLAY_BAR_COLOR_CENTER_MARKER = IM_COL32(255, 255, 255, 180); // Center marker (white, semi-transparent)
// For frequency band fill, color is calculated dynamically (gradient)

// Overlay/ImGui slider ranges
constexpr float OVERLAY_MIN_FREQ_MIN = 10.0f;
constexpr float OVERLAY_MIN_FREQ_MAX = 500.0f;
constexpr float OVERLAY_MIN_FREQ_STEP = 1.0f;
constexpr float OVERLAY_MAX_FREQ_MIN = 2000.0f;
constexpr float OVERLAY_MAX_FREQ_MAX = 22050.0f;
constexpr float OVERLAY_MAX_FREQ_STEP = 10.0f;
constexpr float OVERLAY_LOG_STRENGTH_MIN = 0.01f;
constexpr float OVERLAY_LOG_STRENGTH_MAX = 1.5f;
constexpr float OVERLAY_LOG_STRENGTH_STEP = 0.01f;
constexpr float OVERLAY_EQ_BAND_MIN = 0.0f;
constexpr float OVERLAY_EQ_BAND_MAX = 4.0f;
constexpr float OVERLAY_EQ_BAND_STEP = 0.01f;
constexpr float OVERLAY_EQ_WIDTH_MIN = 0.05f;
constexpr float OVERLAY_EQ_WIDTH_MAX = 0.5f;
constexpr float OVERLAY_EQ_WIDTH_STEP = 0.01f;
constexpr float OVERLAY_AMPLIFIER_MIN = 1.0f;
constexpr float OVERLAY_AMPLIFIER_MAX = 11.0f;
constexpr float OVERLAY_AMPLIFIER_STEP = 0.01f;
constexpr float OVERLAY_BEAT_MIN_FREQ_MIN = 0.0f;
constexpr float OVERLAY_BEAT_MIN_FREQ_MAX = 100.0f;
constexpr float OVERLAY_BEAT_MIN_FREQ_STEP = 0.1f;
constexpr float OVERLAY_BEAT_MAX_FREQ_MIN = 100.0f;
constexpr float OVERLAY_BEAT_MAX_FREQ_MAX = 500.0f;
constexpr float OVERLAY_BEAT_MAX_FREQ_STEP = 0.1f;
constexpr float OVERLAY_FLUX_SMOOTH_MIN = 0.01f;
constexpr float OVERLAY_FLUX_SMOOTH_MAX = 0.5f;
constexpr float OVERLAY_FLUX_SMOOTH_STEP = 0.001f;
constexpr float OVERLAY_FLUX_THRESH_MIN = 1.0f;
constexpr float OVERLAY_FLUX_THRESH_MAX = 3.0f;
constexpr float OVERLAY_FLUX_THRESH_STEP = 0.01f;
constexpr float OVERLAY_PAN_OFFSET_MIN = -1.0f;
constexpr float OVERLAY_PAN_OFFSET_MAX = 1.0f;
constexpr float OVERLAY_PAN_OFFSET_STEP = 0.01f;
