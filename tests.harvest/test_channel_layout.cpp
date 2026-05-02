#include <gtest/gtest.h>

#include "../src/audio/analysis/channel_layout.h"

using lw::audio::analysis::Layout71;
using lw::audio::analysis::SetLayout71;
using lw::audio::analysis::GetLayout71;

TEST(ChannelLayout, DefaultIsUnknown) {
    SetLayout71(Layout71::Unknown);
    EXPECT_EQ(GetLayout71(), Layout71::Unknown);
}

TEST(ChannelLayout, RoundTripsAllVariants) {
    SetLayout71(Layout71::Classic);
    EXPECT_EQ(GetLayout71(), Layout71::Classic);

    SetLayout71(Layout71::Surround);
    EXPECT_EQ(GetLayout71(), Layout71::Surround);

    SetLayout71(Layout71::Unknown);
    EXPECT_EQ(GetLayout71(), Layout71::Unknown);
}
