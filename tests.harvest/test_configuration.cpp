#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../src/configuration/Configuration.h"

using Listeningway::Configuration;

namespace {

std::string TempJsonPath() {
    auto p = std::filesystem::temp_directory_path() / "listeningway_test.json";
    return p.string();
}

void RemoveIfExists(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

// Defaults must round-trip. This is the regression test for the original
// hand-rolled JSON parser bug, where frequency.minFreq / frequency.maxFreq
// silently reverted to defaults on every Load() because the parser used a
// flat content.find() and never even called getValue() for those fields.
TEST(Configuration, RoundTripPreservesAllFields) {
    const std::string path = TempJsonPath();
    RemoveIfExists(path);

    Configuration original;
    // Edit fields in every sub-struct, including the keys that previously
    // collided across scopes (minFreq/maxFreq exist in both `beat` and
    // `frequency`).
    original.audio.analysisEnabled = true;
    original.audio.captureProviderCode = "system";
    original.audio.panSmoothing = 0.42f;
    original.audio.panOffset = -0.12f;
    original.audio.simdEnabled = false;

    original.beat.algorithm = 0;
    original.beat.profile = "edm";
    original.beat.minFreq = 30.0f;
    original.beat.maxFreq = 180.0f;
    original.beat.spectralFluxThreshold = 0.077f;
    original.beat.tempoChangeThreshold = 0.31f;

    original.frequency.logScaleEnabled = false;
    original.frequency.logStrength = 0.51f;
    original.frequency.minFreq = 220.0f;   // distinct from beat.minFreq
    original.frequency.maxFreq = 18000.0f; // distinct from beat.maxFreq
    original.frequency.equalizerBands = {0.5f, 1.5f, 2.0f, 1.25f, 0.75f};
    original.frequency.equalizerWidth = 0.22f;
    original.frequency.amplifier = 1.5f;
    original.frequency.amplifierVolume = 2.5f;
    original.frequency.amplifierBands = 3.5f;
    original.frequency.amplifierDirection = 4.5f;
    original.frequency.bands = 32;
    original.frequency.fftSize = 1024;
    original.frequency.bandNorm = 0.05f;

    original.debug.debugEnabled = true;
    original.debug.overlayEnabled = true;

    ASSERT_TRUE(original.Save());

    Configuration loaded;
    ASSERT_TRUE(loaded.Load());

    EXPECT_EQ(loaded.audio.analysisEnabled, original.audio.analysisEnabled);
    EXPECT_EQ(loaded.audio.captureProviderCode, original.audio.captureProviderCode);
    EXPECT_FLOAT_EQ(loaded.audio.panSmoothing, original.audio.panSmoothing);
    EXPECT_FLOAT_EQ(loaded.audio.panOffset, original.audio.panOffset);
    EXPECT_EQ(loaded.audio.simdEnabled, original.audio.simdEnabled);

    EXPECT_EQ(loaded.beat.algorithm, original.beat.algorithm);
    EXPECT_EQ(loaded.beat.profile, original.beat.profile);
    EXPECT_FLOAT_EQ(loaded.beat.minFreq, original.beat.minFreq);
    EXPECT_FLOAT_EQ(loaded.beat.maxFreq, original.beat.maxFreq);
    EXPECT_FLOAT_EQ(loaded.beat.spectralFluxThreshold, original.beat.spectralFluxThreshold);
    EXPECT_FLOAT_EQ(loaded.beat.tempoChangeThreshold, original.beat.tempoChangeThreshold);

    EXPECT_EQ(loaded.frequency.logScaleEnabled, original.frequency.logScaleEnabled);
    EXPECT_FLOAT_EQ(loaded.frequency.logStrength, original.frequency.logStrength);
    EXPECT_FLOAT_EQ(loaded.frequency.minFreq, original.frequency.minFreq);
    EXPECT_FLOAT_EQ(loaded.frequency.maxFreq, original.frequency.maxFreq);
    for (size_t i = 0; i < original.frequency.equalizerBands.size(); ++i) {
        EXPECT_FLOAT_EQ(loaded.frequency.equalizerBands[i], original.frequency.equalizerBands[i]);
    }
    EXPECT_FLOAT_EQ(loaded.frequency.equalizerWidth, original.frequency.equalizerWidth);
    EXPECT_FLOAT_EQ(loaded.frequency.amplifier, original.frequency.amplifier);
    EXPECT_FLOAT_EQ(loaded.frequency.amplifierVolume, original.frequency.amplifierVolume);
    EXPECT_FLOAT_EQ(loaded.frequency.amplifierBands, original.frequency.amplifierBands);
    EXPECT_FLOAT_EQ(loaded.frequency.amplifierDirection, original.frequency.amplifierDirection);
    EXPECT_EQ(loaded.frequency.bands, original.frequency.bands);
    EXPECT_EQ(loaded.frequency.fftSize, original.frequency.fftSize);
    EXPECT_FLOAT_EQ(loaded.frequency.bandNorm, original.frequency.bandNorm);

    EXPECT_EQ(loaded.debug.debugEnabled, original.debug.debugEnabled);
    EXPECT_EQ(loaded.debug.overlayEnabled, original.debug.overlayEnabled);

    RemoveIfExists(path);
}

// frequency.minFreq and beat.minFreq share a JSON key name within their own
// sub-objects. Verify the loader keeps them distinct.
TEST(Configuration, FrequencyMinFreqDoesNotCollideWithBeatMinFreq) {
    const std::string path = TempJsonPath();
    RemoveIfExists(path);

    Configuration original;
    original.beat.minFreq = 42.0f;
    original.beat.maxFreq = 250.0f;
    original.frequency.minFreq = 480.0f;
    original.frequency.maxFreq = 16500.0f;

    ASSERT_TRUE(original.Save());

    Configuration loaded;
    ASSERT_TRUE(loaded.Load());

    EXPECT_FLOAT_EQ(loaded.beat.minFreq, 42.0f);
    EXPECT_FLOAT_EQ(loaded.beat.maxFreq, 250.0f);
    EXPECT_FLOAT_EQ(loaded.frequency.minFreq, 480.0f);
    EXPECT_FLOAT_EQ(loaded.frequency.maxFreq, 16500.0f);

    RemoveIfExists(path);
}

TEST(Configuration, ResetToDefaultsRestoresInitialState) {
    Configuration cfg;
    cfg.audio.analysisEnabled = true;
    cfg.beat.algorithm = 0;
    cfg.frequency.amplifier = 7.0f;

    cfg.ResetToDefaults();

    Configuration fresh;
    EXPECT_EQ(cfg.audio.analysisEnabled, fresh.audio.analysisEnabled);
    EXPECT_EQ(cfg.beat.algorithm, fresh.beat.algorithm);
    EXPECT_FLOAT_EQ(cfg.frequency.amplifier, fresh.frequency.amplifier);
}

TEST(Configuration, ValidateClampsOutOfRangeFields) {
    Configuration cfg;
    cfg.audio.panSmoothing = 5.0f;     // beyond [0,1]
    cfg.audio.panOffset = -2.0f;       // beyond [-1,1]
    cfg.beat.algorithm = 99;           // beyond [0,1]
    cfg.frequency.amplifier = 100.0f;  // beyond OVERLAY_AMPLIFIER_MAX
    cfg.frequency.minFreq = 5.0f;      // below 10
    cfg.frequency.maxFreq = 100.0f;    // below 2000

    cfg.Validate();

    EXPECT_GE(cfg.audio.panSmoothing, 0.0f);
    EXPECT_LE(cfg.audio.panSmoothing, 1.0f);
    EXPECT_GE(cfg.audio.panOffset, -1.0f);
    EXPECT_LE(cfg.audio.panOffset, 1.0f);
    EXPECT_GE(cfg.beat.algorithm, 0);
    EXPECT_LE(cfg.beat.algorithm, 1);
    EXPECT_LT(cfg.frequency.minFreq, cfg.frequency.maxFreq);
}

TEST(Configuration, LoadOnMissingFileReturnsFalseAndLeavesDefaults) {
    const std::string path = TempJsonPath();
    RemoveIfExists(path);

    Configuration cfg;
    EXPECT_FALSE(cfg.Load());
    Configuration fresh;
    EXPECT_FLOAT_EQ(cfg.frequency.amplifier, fresh.frequency.amplifier);
}
