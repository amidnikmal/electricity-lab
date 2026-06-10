#pragma once

#include "learning/TaskGenerator.h"
#include <string>
#include <vector>

// Export of generated tasks to Anki via AnkiConnect (HTTP localhost:8765,
// action "addNotes"). Scheduling is done BY ANKI with FSRS (default since
// Anki 23.10, difficulty/stability/retrievability model); this app never
// computes intervals itself — it only supplies material.
namespace current_lab::learning {

struct AnkiNote {
    std::string front;
    std::string back;
    std::vector<std::string> tags;
};

std::string escapeJson(const std::string& text);

AnkiNote noteFromTask(const GeneratedTask& task);

std::string buildAddNotesPayload(const std::string& deckName,
                                 const std::string& modelName,
                                 const std::vector<AnkiNote>& notes);

// Best-effort POST to a running AnkiConnect instance. Returns false with an
// error message when Anki is not reachable.
bool postToAnkiConnect(const std::string& payload, std::string* response,
                       std::string* error,
                       const std::string& host = "127.0.0.1", int port = 8765);

} // namespace current_lab::learning
