#include <gtest/gtest.h>

#include "../src/audio/analysis/beat_profiles.h"
#include "../src/configuration/Configuration.h"

using Listeningway::Configuration;

TEST(BeatProfiles, ToStringRoundTrip) {
    BeatProfile parsed;
    ASSERT_TRUE(FromString("general", parsed));
    EXPECT_STREQ("general", ToString(parsed));

    ASSERT_TRUE(FromString("edm", parsed));
    EXPECT_STREQ("edm", ToString(parsed));

    ASSERT_TRUE(FromString("acoustic", parsed));
    EXPECT_STREQ("acoustic", ToString(parsed));

    ASSERT_TRUE(FromString("custom", parsed));
    EXPECT_STREQ("custom", ToString(parsed));
}

TEST(BeatProfiles, FromStringIsCaseInsensitiveAndAcceptsAliases) {
    BeatProfile p;
    EXPECT_TRUE(FromString("EDM", p));
    EXPECT_TRUE(FromString("Electronic", p));
    EXPECT_TRUE(FromString("Acoustic", p));
    EXPECT_TRUE(FromString("Organic", p));

    EXPECT_FALSE(FromString("not-a-profile", p));
}

TEST(BeatProfiles, ApplyGeneralMusicSetsExpectedValues) {
    Configuration cfg;
    cfg.beat.algorithm = 0;
    cfg.beat.spectralFluxThreshold = 0.0f;
    ApplyBeatProfile(BeatProfile::GeneralMusic, cfg);
    EXPECT_EQ(cfg.beat.profile, std::string("general"));
    EXPECT_FLOAT_EQ(cfg.beat.spectralFluxThreshold, 0.06f);
    EXPECT_FLOAT_EQ(cfg.beat.minFreq, 40.0f);
    EXPECT_FLOAT_EQ(cfg.beat.maxFreq, 250.0f);
}

TEST(BeatProfiles, ApplyCustomLeavesConfigUntouched) {
    Configuration cfg;
    cfg.beat.minFreq = 17.0f;
    cfg.beat.maxFreq = 999.0f;
    cfg.beat.spectralFluxThreshold = 0.001f;

    ApplyBeatProfile(BeatProfile::Custom, cfg);

    EXPECT_FLOAT_EQ(cfg.beat.minFreq, 17.0f);
    EXPECT_FLOAT_EQ(cfg.beat.maxFreq, 999.0f);
    EXPECT_FLOAT_EQ(cfg.beat.spectralFluxThreshold, 0.001f);
}
