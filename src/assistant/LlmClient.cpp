#include "assistant/LlmClient.h"
#include "net/HttpClient.h"

#include <nlohmann/json.hpp>

namespace current_lab::assistant {

const char* socraticCriticSystemPrompt() {
    return
        "You are a physics tutor in CRITIC mode for a circuit simulator. "
        "You receive a task, the student's own prediction and attempt, and the "
        "simulator's measurement. Rules: never produce the final numeric answer "
        "or solve the task; comment on the student's attempt; point out where it "
        "contradicts the measurement; ask exactly one counter-question that moves "
        "the student one step forward; if the attempt is correct, briefly confirm "
        "it is correct and ask a transfer question. Do not praise the student "
        "personally; no motivational talk; be brief and factual.";
}

std::string buildChatRequest(const LlmConfig& config, const std::vector<ChatMessage>& messages) {
    // ordered_json сохраняет порядок вставки ключей (обычный nlohmann::json
    // сортирует их алфавитно) — так wire-формат остаётся байт-в-байт прежним:
    // {"model":...,"messages":[...],"temperature":0.4,"stream":false}
    nlohmann::ordered_json j;
    j["model"] = config.model;
    j["messages"] = nlohmann::ordered_json::array();
    for (const auto& m : messages)
        j["messages"].push_back({{"role", m.role}, {"content", m.content}});
    j["temperature"] = 0.4;
    j["stream"] = false;
    return j.dump();
}

// Разбор через nlohmann::json — \uXXXX теперь декодируется в корректный UTF-8
// (кириллица сохраняется), а разбор валидирует всю структуру целиком.
bool extractAssistantReply(const std::string& responseJson, std::string* reply) {
    nlohmann::json j = nlohmann::json::parse(responseJson, nullptr, false);
    if (j.is_discarded()) return false;

    nlohmann::json content;
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty())
        content = j["choices"][0]["message"]["content"];
    else
        content = j["message"]["content"];

    if (!content.is_string()) return false;
    *reply = content.get<std::string>();
    return true;
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
