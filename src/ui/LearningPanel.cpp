#include "ui/LearningPanel.h"
#include "ui/I18n.h"
#include "ui/MathText.h"
#include "ui/UiHelpers.h"
#include "learning/AnkiExport.h"
#include "learning/Lessons.h"
#include "imgui.h"

#include <format>
#include <cstring>

using namespace current_lab::learning;
using current_lab::i18n::tr;

LearningPanel::LearningPanel() : m_generator() {}

void LearningPanel::startTask(TaskFamily family) {
    auto task = m_generator.generate(family, m_difficulty);
    m_session.startTask(task, m_toolFree ? SessionMode::ToolFreeRetrieval : SessionMode::Practice);
    m_predictionBuf[0] = '\0';
    m_attemptInput = 0.0;
    m_statusLine.clear();
    m_solutionText.clear();
    m_assistantReply.clear();
    m_ankiStatus.clear();
}

void LearningPanel::startInterleavedTask() {
    auto task = m_generator.generateNext(m_difficulty);
    m_session.startTask(task, m_toolFree ? SessionMode::ToolFreeRetrieval : SessionMode::Practice);
    m_predictionBuf[0] = '\0';
    m_attemptInput = 0.0;
    m_statusLine.clear();
    m_solutionText.clear();
    m_assistantReply.clear();
    m_ankiStatus.clear();
}

void LearningPanel::render() {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Learning"), &open)) {
        ImGui::End();
        return;
    }

    renderLessonSection();
    renderTaskSection();
    renderAssistantSection();
    renderAnkiSection();
    renderMetricsSection();

    ImGui::End();
}

void LearningPanel::renderLessonSection() {
    ImGui::SeparatorText(tr("Lesson"));

    const auto& all = lessons();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##Lesson", tr(all[m_lessonIndex].title))) {
        for (int i = 0; i < static_cast<int>(all.size()); ++i) {
            if (ImGui::Selectable(tr(all[i].title), i == m_lessonIndex))
                m_lessonIndex = i;
        }
        ImGui::EndCombo();
    }

    const Lesson& lesson = all[m_lessonIndex];
    ImGui::TextDisabled("%s", tr("Situation"));
    ImGui::TextWrapped("%s", tr(lesson.arc.situation));
    ImGui::TextDisabled("%s", tr("Action"));
    ImGui::TextWrapped("%s", tr(lesson.arc.action));
    ImGui::TextDisabled("%s", tr("In words"));
    ImGui::TextWrapped("%s", tr(lesson.arc.words));
    ImGui::TextDisabled("%s", tr("In symbols"));
    ImGui::TextWrapped("%s", tr(lesson.arc.symbols));
    ImGui::TextDisabled("%s", tr("Formula"));
    current_lab::ui::renderMathText(lesson.arc.formula);

    ImGui::SliderInt(tr("Difficulty"), &m_difficulty, 1, 3);
    ImGui::Checkbox(tr("Tool-free retrieval (no assistant, no hints)"), &m_toolFree);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Schedule these through Anki/FSRS reviews, not by mood."));

    if (ImGui::Button(tr("Practice this lesson")))
        startTask(lesson.practiceFamily);
    ImGui::SameLine();
    if (ImGui::Button(tr("Mixed practice (interleaved)")))
        startInterleavedTask();
    ImGui::SameLine();
    if (ImGui::Button(tr("Open lesson circuit")) && loadCircuitIntoSimulator)
        loadCircuitIntoSimulator(lessonPresetCircuit(lesson.practiceFamily));
}

void LearningPanel::renderTaskSection() {
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Task"));

    const GeneratedTask* task = m_session.task();
    if (!task) {
        ImGui::TextDisabled("%s", tr("No task yet. Pick a lesson and press Practice."));
        return;
    }

    ImGui::TextDisabled("%s, %d%s", tr(taskFamilyName(task->family)), task->difficulty,
                        m_session.mode() == SessionMode::ToolFreeRetrieval ? tr(", tool-free") : "");
    ImGui::TextWrapped("%s", task->prompt.c_str());

    // 1) Prediction (required before the attempt is accepted).
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Prediction (required first)"));
    ImGui::TextWrapped("%s", task->predictionPrompt.c_str());
    ImGui::SetNextItemWidth(-110.0f);
    ImGui::InputText("##Prediction", m_predictionBuf, sizeof(m_predictionBuf),
                     m_session.predictionRecorded() ? ImGuiInputTextFlags_ReadOnly : 0);
    ImGui::SameLine();
    if (m_session.predictionRecorded()) {
        ImGui::TextDisabled("%s", tr("recorded"));
    } else if (ImGui::Button(tr("Record"))) {
        m_session.recordPrediction(m_predictionBuf);
    }

    // 2) Numeric attempt (unlocks measurement, solution and assistant).
    ImGui::Spacing();
    ImGui::TextDisabled(tr("Your answer (%s)"), tr(task->answerUnit.c_str()));
    ImGui::BeginDisabled(!m_session.canSubmitAttempt() || m_session.attemptRecorded());
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputDouble("##Attempt", &m_attemptInput, 0.0, 0.0, "%.4f");
    ImGui::SameLine();
    if (ImGui::Button(tr("Submit attempt"))) {
        auto result = m_session.submitAttempt(m_attemptInput);
        if (result.accepted) {
            std::string buf = std::format(
                          tr("Measured: {:.4g} {}. Your attempt is {} (tolerance {:.3g})."),
                          result.groundTruth, tr(task->answerUnit.c_str()),
                          result.correct ? tr("within tolerance") : tr("outside tolerance"),
                          result.tolerance);
            m_statusLine = buf;
        }
    }
    ImGui::EndDisabled();
    if (!m_session.canSubmitAttempt() && !m_session.attemptRecorded())
        ImGui::TextDisabled("%s", tr("Locked until a prediction is recorded."));

    if (!m_statusLine.empty())
        ImGui::TextWrapped("%s", m_statusLine.c_str());

    // 3) Post-attempt actions.
    ImGui::BeginDisabled(!m_session.canRevealSolution());
    if (ImGui::Button(tr("Show solution"))) {
        if (const std::string* text = m_session.revealSolution())
            m_solutionText = *text;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Open in simulator")) && loadCircuitIntoSimulator)
        loadCircuitIntoSimulator(task->circuit);
    ImGui::EndDisabled();
    if (!m_session.canRevealSolution())
        ImGui::TextDisabled("%s", tr("Solution and simulator unlock after your attempt."));

    if (!m_solutionText.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_solutionText.c_str());
    }

    if (ImGui::Button(tr("Next task (interleaved)")))
        startInterleavedTask();
}

void LearningPanel::renderAssistantSection() {
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Assistant (critic, not solver)"));
    ImGui::TextDisabled("%s", tr("Comments on YOUR attempt and asks a counter-question. It does not solve."));
    ImGui::TextDisabled("%s", tr("Small local models are weak tutors; quality scales with the model."));

    if (ImGui::CollapsingHeader(tr("Endpoint (OpenAI-compatible)"))) {
        ImGui::InputText(tr("Host"), m_llmHost, sizeof(m_llmHost));
        ImGui::InputInt(tr("Port"), &m_llmPort);
        ImGui::InputText(tr("Model"), m_llmModel, sizeof(m_llmModel));
        ImGui::InputText(tr("API key (optional)"), m_llmApiKey, sizeof(m_llmApiKey),
                         ImGuiInputTextFlags_Password);
        ImGui::TextDisabled("%s", tr("Works with llama.cpp / vLLM on localhost (plain HTTP)."));
    }

    ImGui::SetNextItemWidth(-130.0f);
    ImGui::InputText("##AssistantQ", m_assistantQuestion, sizeof(m_assistantQuestion));
    ImGui::SameLine();
    if (ImGui::Button(tr("Ask critic"))) {
        // Gate decision lives in LearningSession (code, not prompt).
        auto gate = m_session.requestAssistant();
        switch (gate) {
            case current_lab::learning::AssistantGate::BlockedNoTask:
                m_assistantReply = tr("[blocked] No active task.");
                break;
            case current_lab::learning::AssistantGate::BlockedToolFree:
                m_assistantReply = tr("[blocked] Tool-free session: no assistant by design.");
                break;
            case current_lab::learning::AssistantGate::BlockedNoAttempt:
                m_assistantReply = tr("[blocked] Submit your own attempt first. (Counted.)");
                break;
            case current_lab::learning::AssistantGate::Allowed: {
                m_llmConfig.host = m_llmHost;
                m_llmConfig.port = m_llmPort;
                m_llmConfig.model = m_llmModel;
                m_llmConfig.apiKey = m_llmApiKey;

                std::vector<current_lab::assistant::ChatMessage> messages;
                messages.push_back({"system", current_lab::assistant::socraticCriticSystemPrompt()});
                std::string userContent = m_session.assistantContext();
                if (m_assistantQuestion[0] != '\0') {
                    userContent += "Student's question: ";
                    userContent += m_assistantQuestion;
                }
                messages.push_back({"user", userContent});

                std::string reply, error;
                if (current_lab::assistant::chatComplete(m_llmConfig, messages, &reply, &error))
                    m_assistantReply = reply;
                else
                    m_assistantReply = "[error] " + error;
                break;
            }
        }
    }

    if (!m_assistantReply.empty())
        ImGui::TextWrapped("%s", m_assistantReply.c_str());
}

void LearningPanel::renderAnkiSection() {
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Anki export"));
    ImGui::TextDisabled("%s", tr("Cards go to Anki via AnkiConnect; scheduling is Anki's FSRS, not this app."));
    ImGui::InputText(tr("Deck"), m_ankiDeck, sizeof(m_ankiDeck));

    ImGui::BeginDisabled(!m_session.canRevealSolution());
    if (ImGui::Button(tr("Export current task"))) {
        const GeneratedTask* task = m_session.task();
        if (task) {
            auto note = noteFromTask(*task);
            std::string payload = buildAddNotesPayload(m_ankiDeck, "Basic", {note});
            std::string response, error;
            if (postToAnkiConnect(payload, &response, &error))
                m_ankiStatus = tr("Exported.");
            else
                m_ankiStatus = "Failed: " + error + " (is Anki with AnkiConnect running?)";
        }
    }
    ImGui::EndDisabled();
    if (!m_session.canRevealSolution())
        ImGui::TextDisabled("%s", tr("Export unlocks after your attempt."));
    if (!m_ankiStatus.empty())
        ImGui::TextWrapped("%s", m_ankiStatus.c_str());
}

void LearningPanel::renderMetricsSection() {
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Reliance readout (facts only)"));
    const auto& m = m_session.metrics();
    ImGui::Text(tr("Tasks started: %d, attempts: %d, within tolerance: %d"),
                m.tasksStarted, m.attemptsSubmitted, m.correctAttempts);
    ImGui::Text(tr("Help requested before own attempt: %d (blocked)"), m.helpRequestsBeforeAttempt);
    ImGui::Text(tr("Help requested after own attempt: %d"), m.helpRequestsAfterAttempt);
    ImGui::Text(tr("Solution reveals blocked: %d"), m.solutionRevealsBlocked);
    ImGui::Text(tr("Tool-free tasks: %d, tool-free within tolerance: %d"),
                m.toolFreeTasks, m.toolFreeCorrect);
}
