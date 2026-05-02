// Stubs for symbols that Configuration.cpp references but tests don't need to
// drive: settings.h's GetSettingsPath() (the JSON config path), and the
// LOG_* helpers from logging.h.

#include <string>
#include <filesystem>
#include "../src/utils/logging.h"

// Minimal logging implementation used by tests: no-op (errors are not fatal).
void LogToFile(const std::string& /*msg*/, LogLevel /*level*/) {}
void OpenLogFile(const std::string& /*filename*/) {}
void CloseLogFile() {}
void SetLogDebugEnabled(bool /*enabled*/) {}

// Tests put the JSON file in a temp directory.
std::string GetSettingsPath() {
    auto tmp = std::filesystem::temp_directory_path() / "listeningway_test.json";
    return tmp.string();
}

std::string GetLogFilePath() {
    return std::string{};
}
