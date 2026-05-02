#include <gtest/gtest.h>

#include "../src/core/audio_format_utils.h"

using AudioFormatUtils::FormatToString;
using AudioFormatUtils::IntToFormat;
using AudioFormatUtils::GetChannelCount;
using AudioFormatUtils::IsLeftChannel;
using AudioFormatUtils::IsRightChannel;
using AudioFormatUtils::IsCenterChannel;
using AudioFormatUtils::IsRearChannel;
using AudioFormatUtils::IsSideChannel;

TEST(AudioFormatUtils, IntToFormatRecognizesKnownChannelCounts) {
    EXPECT_EQ(IntToFormat(0), AudioFormat::None);
    EXPECT_EQ(IntToFormat(1), AudioFormat::Mono);
    EXPECT_EQ(IntToFormat(2), AudioFormat::Stereo);
    EXPECT_EQ(IntToFormat(6), AudioFormat::Surround51);
    EXPECT_EQ(IntToFormat(8), AudioFormat::Surround71);
}

TEST(AudioFormatUtils, IntToFormatFallsBackToNoneOnUnknown) {
    EXPECT_EQ(IntToFormat(3), AudioFormat::None);
    EXPECT_EQ(IntToFormat(7), AudioFormat::None);
    EXPECT_EQ(IntToFormat(9), AudioFormat::None);
    EXPECT_EQ(IntToFormat(-1), AudioFormat::None);
}

TEST(AudioFormatUtils, GetChannelCountMatchesFormat) {
    EXPECT_EQ(GetChannelCount(AudioFormat::None), 0);
    EXPECT_EQ(GetChannelCount(AudioFormat::Mono), 1);
    EXPECT_EQ(GetChannelCount(AudioFormat::Stereo), 2);
    EXPECT_EQ(GetChannelCount(AudioFormat::Surround51), 6);
    EXPECT_EQ(GetChannelCount(AudioFormat::Surround71), 8);
}

TEST(AudioFormatUtils, FormatToStringRoundTrip) {
    EXPECT_STREQ(FormatToString(AudioFormat::None), "None");
    EXPECT_STREQ(FormatToString(AudioFormat::Mono), "Mono");
    EXPECT_STREQ(FormatToString(AudioFormat::Stereo), "Stereo");
    EXPECT_STREQ(FormatToString(AudioFormat::Surround51), "5.1");
    EXPECT_STREQ(FormatToString(AudioFormat::Surround71), "7.1");
}

TEST(AudioFormatUtils, StereoChannelClassification) {
    EXPECT_TRUE(IsLeftChannel(AudioFormat::Stereo, 0));
    EXPECT_FALSE(IsLeftChannel(AudioFormat::Stereo, 1));
    EXPECT_TRUE(IsRightChannel(AudioFormat::Stereo, 1));
    EXPECT_FALSE(IsRightChannel(AudioFormat::Stereo, 0));
    EXPECT_FALSE(IsCenterChannel(AudioFormat::Stereo, 0));
    EXPECT_FALSE(IsCenterChannel(AudioFormat::Stereo, 1));
    EXPECT_FALSE(IsRearChannel(AudioFormat::Stereo, 0));
    EXPECT_FALSE(IsSideChannel(AudioFormat::Stereo, 1));
}

TEST(AudioFormatUtils, MonoCountsAsBothLeftAndRight) {
    EXPECT_TRUE(IsLeftChannel(AudioFormat::Mono, 0));
    EXPECT_TRUE(IsRightChannel(AudioFormat::Mono, 0));
}

TEST(AudioFormatUtils, Surround51ChannelLayout) {
    // ITU-R BS.775: FL=0, FR=1, C=2, LFE=3, SL=4, SR=5
    EXPECT_TRUE(IsLeftChannel(AudioFormat::Surround51, 0));   // FL
    EXPECT_TRUE(IsLeftChannel(AudioFormat::Surround51, 4));   // SL
    EXPECT_TRUE(IsRightChannel(AudioFormat::Surround51, 1));  // FR
    EXPECT_TRUE(IsRightChannel(AudioFormat::Surround51, 5));  // SR
    EXPECT_TRUE(IsCenterChannel(AudioFormat::Surround51, 2)); // C
    EXPECT_TRUE(IsSideChannel(AudioFormat::Surround51, 4));   // SL
    EXPECT_TRUE(IsSideChannel(AudioFormat::Surround51, 5));   // SR
    EXPECT_FALSE(IsRearChannel(AudioFormat::Surround51, 4));
}

TEST(AudioFormatUtils, Surround71ChannelLayout) {
    // 7.1: FL=0, FR=1, C=2, LFE=3, RL=4, RR=5, SL=6, SR=7
    EXPECT_TRUE(IsLeftChannel(AudioFormat::Surround71, 0));
    EXPECT_TRUE(IsRightChannel(AudioFormat::Surround71, 1));
    EXPECT_TRUE(IsCenterChannel(AudioFormat::Surround71, 2));
    EXPECT_TRUE(IsRearChannel(AudioFormat::Surround71, 4));
    EXPECT_TRUE(IsRearChannel(AudioFormat::Surround71, 5));
    EXPECT_TRUE(IsSideChannel(AudioFormat::Surround71, 6));
    EXPECT_TRUE(IsSideChannel(AudioFormat::Surround71, 7));
    EXPECT_FALSE(IsRearChannel(AudioFormat::Surround71, 6));
    EXPECT_FALSE(IsSideChannel(AudioFormat::Surround71, 4));
}
