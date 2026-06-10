#pragma once

#include "assistant/LlmClient.h"
#include "learning/LearningSession.h"
#include "learning/TaskGenerator.h"
#include <functional>
#include <string>

// Learning panel: lessons (derivation arc) -> generated task -> prediction ->
// attempt -> verification -> explanation / Socratic critic. All gating is in
// LearningSession; this panel only reflects the gates.
class LearningPanel {
public:
    bool open = false;
    std::function<void(const Circuit&)> loadCircuitIntoSimulator;

    LearningPanel();
    void render();

private:
    void startTask(current_lab::learning::TaskFamily family);
    void startInterleavedTask();
    void renderLessonSection();
    void renderTaskSection();
    void renderAssistantSection();
    void renderAnkiSection();
    void renderMetricsSection();

    current_lab::learning::TaskGenerator m_generator;
    current_lab::learning::LearningSession m_session;

    int m_lessonIndex = 0;
    int m_difficulty = 1;
    bool m_toolFree = false;

    char m_predictionBuf[256] = "";
    double m_attemptInput = 0.0;
    std::string m_statusLine;
    std::string m_solutionText;

    char m_assistantQuestion[512] = "";
    std::string m_assistantReply;
    current_lab::assistant::LlmConfig m_llmConfig;
    char m_llmHost[64] = "127.0.0.1";
    int m_llmPort = 8080;
    char m_llmModel[64] = "local";
    char m_llmApiKey[128] = "";

    char m_ankiDeck[64] = "Current Lab";
    std::string m_ankiStatus;
};
