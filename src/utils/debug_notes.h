#pragma once
#include <string>

namespace DebugNotes {

// Append a critical note (thread-safe). Keep short, one-line per note.
void Add(const std::string& note);

// Get all notes joined by newlines (thread-safe). For UI display/copy.
std::string GetAll();

// Clear all notes (thread-safe).
void Clear();

}
