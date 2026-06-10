#include "assistant/LlmClient.h"
#include "learning/AnkiExport.h" // escapeJson
#include "net/HttpClient.h"

#include <sstream>

namespace current_lab::assistant {

const char* socraticCriticSystemPrompt() {
    return
        "You are a physics tutor in CRITIC mode for a circuit simulator. "
        "You receive a task, the student's own prediction and attempt, and the "
        "simulator's measurement. Rules: never produce the final numeric answer "
        "or solve the task; comment on the student's attempt; point out where it "
        "contradicts the measurement; ask exactly one counter-question that moves "
        "the student one step forward; if the attempt is correct, ask a transfer "
        "question instead. Be brief and factual. No praise, no motivational talk.";
}

std::string buildChatRequest(const LlmConfig& config, const std::vector<ChatMessage>& messages) {
    using learning::escapeJson;
    std::ostringstream json;
    json << "{\"model\":\"" << escapeJson(config.model) << "\",\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i) json << ",";
        json << "{\"role\":\"" << escapeJson(messages[i].role)
             << "\",\"content\":\"" << escapeJson(messages[i].content) << "\"}";
    }
    json << "],\"temperature\":0.4,\"stream\":false}";
    return json.str();
}

namespace {

// Reads a JSON string literal starting at the opening quote; unescapes the
// common sequences a chat API emits.
bool readJsonString(const std::string& text, size_t quotePos, std::string* out) {
    if (quotePos >= text.size() || text[quotePos] != '"') return false;
    std::string result;
    for (size_t i = quotePos + 1; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == '"') {
            *out = std::move(result);
            return true;
        }
        if (ch == '\\' && i + 1 < text.size()) {
            char next = text[++i];
            switch (next) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'u':
                    // Keep it simple: skip the 4 hex digits, emit '?' for
                    // non-ASCII escapes (v1 limitation).
                    if (i + 4 < text.size()) i += 4;
                    result += '?';
                    break;
                default: result += next;
            }
        } else {
            result += ch;
        }
    }
    return false;
}

} // namespace

bool extractAssistantReply(const std::string& responseJson, std::string* reply) {
    size_t messagePos = responseJson.find("\"message\"");
    if (messagePos == std::string::npos) return false;
    size_t contentPos = responseJson.find("\"content\"", messagePos);
    if (contentPos == std::string::npos) return false;
    size_t colon = responseJson.find(':', contentPos + 9);
    if (colon == std::string::npos) return false;
    size_t quote = responseJson.find('"', colon);
    if (quote == std::string::npos) return false;
    return readJsonString(responseJson, quote, reply);
}

bool chatComplete(const LlmConfig& config, const std::vector<ChatMessage>& messages,
                  std::string* reply, std::string* error) {
    std::string headers;
    if (!config.apiKey.empty())
        headers = "Authorization: Bearer " + config.apiKey + "\r\n";

    net::HttpResponse response;
    if (!net::httpPost(config.host, config.port, config.path,
                       buildChatRequest(config, messages), "application/json",
                       headers, &response, error))
        return false;

    if (response.status != 200) {
        if (error) *error = "LLM endpoint returned HTTP " + std::to_string(response.status);
        return false;
    }
    if (!extractAssistantReply(response.body, reply)) {
        if (error) *error = "could not parse assistant reply";
        return false;
    }
    return true;
}

} // namespace current_lab::assistant
