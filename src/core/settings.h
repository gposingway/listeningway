#pragma once
#include <string>

// Settings persistence: thin facade over ConfigurationManager (JSON-only).
// The legacy ListeningwaySettings struct and Listeningway.ini codepath were
// removed; this header now exposes just the bootstrap and toggle helpers
// the rest of the addon expects.

void LoadAllTunables();
void SaveAllTunables();
void ResetAllTunablesToDefaults();

bool GetAudioAnalysisEnabled();
void SetAudioAnalysisEnabled(bool enabled);
bool GetDebugEnabled();
void SetDebugEnabled(bool enabled);

// Returns the path to the JSON config file (Listeningway.json next to the DLL).
std::string GetSettingsPath();
// Returns the path to the log file (listeningway.log next to the DLL).
std::string GetLogFilePath();
