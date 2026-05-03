#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace lw::log {

namespace {

std::filesystem::path  g_path;
std::ofstream          g_stream;
std::mutex             g_mutex;
std::atomic<bool>      g_enabled{false};
std::atomic<bool>      g_initialised{false};

void timestamp(std::ostream& os) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
    os << buf;
}

void emit(const char* level, const char* fmt, va_list ap) {
    if (!g_initialised.load(std::memory_order_acquire)) return;

    char body[1024];
    std::vsnprintf(body, sizeof(body), fmt, ap);

    std::lock_guard<std::mutex> g(g_mutex);
    if (!g_stream.is_open()) {
        g_stream.open(g_path, std::ios::out | std::ios::trunc);
        if (!g_stream.is_open()) return;
    }
    timestamp(g_stream);
    g_stream << " [" << level << "] " << body << '\n';
    g_stream.flush();
}

}  // namespace

void init(HMODULE addon_module) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(addon_module, buf, MAX_PATH);
    std::filesystem::path p(buf);
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_path = p.parent_path() / "listeningway.log";
    }
    g_initialised.store(true, std::memory_order_release);
}

void set_enabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_release);
}

bool is_enabled() {
    return g_enabled.load(std::memory_order_acquire);
}

void info(const char* fmt, ...) {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    va_list ap; va_start(ap, fmt);
    emit("INFO", fmt, ap);
    va_end(ap);
}

void warn(const char* fmt, ...) {
    // Warnings respect the enable flag (treated as "verbose"); raise to
    // error() if you want it to land regardless.
    if (!g_enabled.load(std::memory_order_acquire)) return;
    va_list ap; va_start(ap, fmt);
    emit("WARN", fmt, ap);
    va_end(ap);
}

void error(const char* fmt, ...) {
    // Errors are always written if init() has been called.
    va_list ap; va_start(ap, fmt);
    emit("ERROR", fmt, ap);
    va_end(ap);
}

}  // namespace lw::log
