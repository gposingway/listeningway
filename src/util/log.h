// Tiny thread-safe logger.
//
//   lw::log::init(HMODULE)   — call once during DllMain or App::start;
//                               resolves log file path to next-to-DLL.
//   lw::log::set_enabled(b)  — runtime toggle from cfg.debug.debug_logging.
//   lw::log::info/warn/error(fmt, ...)  — printf-style; no-ops when off.
//
// Writes to `listeningway.log` next to the addon. Truncates on first
// enable per process so it doesn't grow forever between sessions.
// Per-line timestamps. Errors are always written when init() has been
// called, regardless of `enabled` — the assumption being that "debug
// logging off" means "spare me INFO/WARN noise", but if something
// genuinely went wrong you still want the trace.
#pragma once

#include <windows.h>

namespace lw::log {

void init(HMODULE addon_module);
void set_enabled(bool enabled);
bool is_enabled();

// printf-style. The format string is %-style (NOT std::format) so we can
// stay header-light and avoid pulling <format> into every translation
// unit that wants to log.
void info (const char* fmt, ...);
void warn (const char* fmt, ...);
void error(const char* fmt, ...);

}  // namespace lw::log
