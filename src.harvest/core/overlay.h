#pragma once
#include <vector>
#include <mutex>
#include "audio/analysis/audio_analysis.h"

// Draws the Listeningway debug overlay with real-time audio data
void DrawListeningwayDebugOverlay(const AudioAnalysisData& data);
