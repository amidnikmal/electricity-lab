#include "learning/AnkiExport.h"
#include "net/HttpClient.h"

#include <cstdio>
#include <sstream>

namespace current_lab::learning {

std::string escapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

AnkiNote noteFromTask(const GeneratedTask& task) {
    AnkiNote note;
    note.front = task.prompt;

    char answer[128];
    std::snprintf(answer, sizeof(answer), "%.4g %s", task.groundTruth, task.answerUnit.c_str());
    note.back = std::string(answer) + "\n\n" + task.solutionExplanation;

    note.tags = {"current-lab", taskFamilyName(task.family)};
    for (auto& tag : note.tags)
        for (auto& ch : tag)
            if (ch == ' ' || ch == '\'') ch = '-';
    return note;
}

std::string buildAddNotesPayload(const std::string& deckName,
                                 const std::string& modelName,
                                 const std::vector<AnkiNote>& notes) {
    std::ostringstream json;
    json << "{\"action\":\"addNotes\",\"version\":6,\"params\":{\"notes\":[";
    for (size_t i = 0; i < notes.size(); ++i) {
        const AnkiNote& note = notes[i];
        if (i) json << ",";
        json << "{\"deckName\":\"" << escapeJson(deckName) << "\","
             << "\"modelName\":\"" << escapeJson(modelName) << "\","
             << "\"fields\":{\"Front\":\"" << escapeJson(note.front)
             << "\",\"Back\":\"" << escapeJson(note.back) << "\"},"
             << "\"options\":{\"allowDuplicate\":false},"
             << "\"tags\":[";
        for (size_t t = 0; t < note.tags.size(); ++t) {
            if (t) json << ",";
            json << "\"" << escapeJson(note.tags[t]) << "\"";
        }
        json << "]}";
    }
    json << "]}}";
    return json.str();
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
