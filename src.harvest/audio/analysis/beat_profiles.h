#pragma once

#include <string>
#include "configuration/Configuration.h"
#include "core/constants.h"

enum class BeatProfile : int {
    GeneralMusic = 0,
    ElectronicEDM = 1,
    AcousticOrganic = 2,
    Custom = 3
};

// String conversions for JSON/UI
const char* ToString(BeatProfile profile);
bool FromString(const std::string& s, BeatProfile& out);

// Apply a profile's preset values into the given configuration.
// If profile == Custom, this function does nothing (user settings preserved).
void ApplyBeatProfile(BeatProfile profile, Listeningway::Configuration& cfg);
