#include "learning/AnkiExport.h"
#include "net/HttpClient.h"

#include <format>
#include <nlohmann/json.hpp>

namespace current_lab::learning {

// Экранирование через nlohmann::json — корректно обрабатывает все спецсимволы
// и управляющие коды.
std::string escapeJson(const std::string& text) {
    std::string dumped = nlohmann::json(text).dump();
    return dumped.substr(1, dumped.size() - 2);
}

AnkiNote noteFromTask(const GeneratedTask& task) {
    AnkiNote note;
    note.front = task.prompt;

    std::string answer = std::format("{:.4g} {}", task.groundTruth, task.answerUnit);
    note.back = answer + "\n\n" + task.solutionExplanation;
    if (!task.predictionPrompt.empty())
        note.back += "\n\n" + task.predictionPrompt;

    note.tags = {"current-lab", taskFamilyName(task.family),
                 std::format("d{}", task.difficulty)};
    for (auto& tag : note.tags)
        for (auto& ch : tag)
            if (ch == ' ' || ch == '\'') ch = '-';
    return note;
}

std::string buildAddNotesPayload(const std::string& deckName,
                                 const std::string& modelName,
                                 const std::vector<AnkiNote>& notes) {
    nlohmann::json j;
    j["action"] = "addNotes";
    j["version"] = 6;
    j["params"]["notes"] = nlohmann::json::array();
    for (const auto& note : notes) {
        nlohmann::json n;
        n["deckName"] = deckName;
        n["modelName"] = modelName;
        n["fields"]["Front"] = note.front;
        n["fields"]["Back"] = note.back;
        n["options"]["allowDuplicate"] = false;
        n["tags"] = note.tags;
        j["params"]["notes"].push_back(n);
    }
    return j.dump();
}

bool postToAnkiConnect(const std::string& payload, std::string* response,
                       std::string* error, const std::string& host, int port) {
    net::HttpResponse httpResponse;
    if (!net::httpPost(host, port, "/", payload, "application/json", "", &httpResponse, error))
        return false;
    if (response) *response = httpResponse.body;
    if (httpResponse.status != 200) {
        if (error) *error = "AnkiConnect returned HTTP " + std::to_string(httpResponse.status);
        return false;
    }
    return true;
}

} // namespace current_lab::learning
