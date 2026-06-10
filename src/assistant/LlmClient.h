#pragma once

#include <string>
#include <vector>

// Provider-agnostic LLM client speaking the OpenAI-compatible
// /v1/chat/completions protocol. The same code works against:
//   (a) a local llama.cpp server with a small GGUF model (offline/portable),
//   (b) a local vLLM box,
//   (c) any external OpenAI-compatible API (through a local http proxy; this
//       client is plain-HTTP by design to stay dependency-free).
//
// Honest limitation: a small local model is a weak tutor; quality scales with
// the model. The UI states this. Gating (attempt-first, critic-not-solver) is
// enforced by LearningSession in code — the system prompt below is only
// defense in depth, not the safeguard itself.
namespace current_lab::assistant {

struct LlmConfig {
    std::string host = "127.0.0.1";
    int port = 8080; // llama.cpp server default
    std::string path = "/v1/chat/completions";
    std::string model = "local";
    std::string apiKey; // optional bearer token
};

struct ChatMessage {
    std::string role;    // "system" | "user" | "assistant"
    std::string content;
};

const char* socraticCriticSystemPrompt();

std::string buildChatRequest(const LlmConfig& config, const std::vector<ChatMessage>& messages);

// Targeted extraction of choices[0].message.content from the response JSON.
// Not a general JSON parser; documented v1 limitation, covered by tests.
bool extractAssistantReply(const std::string& responseJson, std::string* reply);

bool chatComplete(const LlmConfig& config, const std::vector<ChatMessage>& messages,
                  std::string* reply, std::string* error);

} // namespace current_lab::assistant
