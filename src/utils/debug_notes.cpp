#include "debug_notes.h"
#include <mutex>
#include <deque>

namespace DebugNotes {

static std::mutex g_mutex;
static std::deque<std::string> g_notes;
static const size_t kMaxNotes = 200; // cap to avoid growth

void Add(const std::string& note) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_notes.size() >= kMaxNotes) g_notes.pop_front();
    g_notes.push_back(note);
}

std::string GetAll() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string out;
    out.reserve(4096);
    for (size_t i = 0; i < g_notes.size(); ++i) {
        out += g_notes[i];
        if (i + 1 < g_notes.size()) out += "\n";
    }
    return out;
}

void Clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notes.clear();
}

}
