#pragma once

#include "configuration/configuration_manager.h"
#include "audio/analysis/audio_analysis.h"
#include "audio/analysis/beat_profiles.h"

// Renders the beat settings UI, including profile selection and custom overrides.
// Returns true if settings changed.
bool DrawBeatSettingsPanel(const AudioAnalysisData& data, Listeningway::ConfigurationManager& cfgMgr);
