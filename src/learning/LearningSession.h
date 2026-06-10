#pragma once

#include "learning/TaskGenerator.h"
#include <cmath>
#include <optional>
#include <string>

// Learning-session state machine. The AI-resilience safeguards live HERE as
// code, not in any prompt:
//   1. Attempt-first gating: the solution and the assistant stay locked until
//      an attempt is recorded. A prompt can be talked around; this cannot.
//   2. AI critic, not solver: the only assistant payload this session exposes
//      is (task statement + the user's recorded attempt + measurement) — see
//      assistantContext(); there is no path that sends a task without attempt.
//   3. Predict-then-verify: verification (running the solver against the task)
//      unlocks only after a prediction is recorded (generation effect).
//   4. Tool-free retrieval mode: assistant is unconditionally blocked.
//   5. Reliance metrics: counted facts, displayed without judgement.
//   6. The derivation arc in lesson content is never delegated; the assistant
//      context never includes the solution explanation before an attempt.
namespace current_lab::learning {

enum class SessionMode {
    Practice,          // assistant available after an attempt
    ToolFreeRetrieval, // no assistant, no hints: manual-flying practice
};

enum class AssistantGate {
    Allowed,
    BlockedNoAttempt,
    BlockedToolFree,
    BlockedNoTask,
};

struct RelianceMetrics {
    int tasksStarted = 0;
    int attemptsSubmitted = 0;
    int correctAttempts = 0;
    int helpRequestsBeforeAttempt = 0; // blocked and counted
    int helpRequestsAfterAttempt = 0;
    int solutionRevealsBlocked = 0;
    int toolFreeTasks = 0;
    int toolFreeCorrect = 0;
};

struct AttemptResult {
    bool accepted = false; // an attempt was recorded
    bool correct = false;
    double submittedValue = 0.0;
    double groundTruth = 0.0;
    double tolerance = 0.0;
};

class LearningSession {
public:
    void startTask(const GeneratedTask& task, SessionMode mode) {
        m_task = task;
        m_mode = mode;
        m_predictionRecorded = false;
        m_attemptRecorded = false;
        m_lastAttempt.reset();
        m_metrics.tasksStarted++;
        if (mode == SessionMode::ToolFreeRetrieval)
            m_metrics.toolFreeTasks++;
    }

    bool hasTask() const { return m_task.has_value(); }
    const GeneratedTask* task() const { return m_task ? &*m_task : nullptr; }
    SessionMode mode() const { return m_mode; }

    // --- safeguard 3: predict-then-verify -----------------------------------
    void recordPrediction(const std::string& text) {
        if (!m_task) return;
        m_prediction = text;
        m_predictionRecorded = !text.empty();
    }
    bool predictionRecorded() const { return m_predictionRecorded; }
    bool canRunVerification() const { return m_task && m_predictionRecorded; }
    const std::string& prediction() const { return m_prediction; }

    // --- safeguard 1: attempt-first gating -----------------------------------
    bool attemptRecorded() const { return m_attemptRecorded; }

    // Prediction-first by construction: a numeric attempt is only accepted
    // after the qualitative prediction is recorded.
    bool canSubmitAttempt() const { return m_task && m_predictionRecorded; }

    AttemptResult submitAttempt(double value) {
        AttemptResult result;
        if (!canSubmitAttempt()) return result;
        result.accepted = true;
        result.submittedValue = value;
        result.groundTruth = m_task->groundTruth;
        result.tolerance = m_task->tolerance;
        result.correct = std::abs(value - m_task->groundTruth) <= m_task->tolerance;

        m_attemptRecorded = true;
        m_metrics.attemptsSubmitted++;
        if (result.correct) {
            m_metrics.correctAttempts++;
            if (m_mode == SessionMode::ToolFreeRetrieval)
                m_metrics.toolFreeCorrect++;
        }
        m_lastAttempt = result;
        return result;
    }

    const AttemptResult* lastAttempt() const { return m_lastAttempt ? &*m_lastAttempt : nullptr; }

    bool canRevealSolution() const { return m_task && m_attemptRecorded; }

    // Returns the explanation only after an attempt; counts blocked tries.
    const std::string* revealSolution() {
        if (!canRevealSolution()) {
            if (m_task) m_metrics.solutionRevealsBlocked++;
            return nullptr;
        }
        return &m_task->solutionExplanation;
    }

    // --- safeguards 2 + 4: assistant gate ------------------------------------
    AssistantGate requestAssistant() {
        if (!m_task) return AssistantGate::BlockedNoTask;
        if (m_mode == SessionMode::ToolFreeRetrieval) {
            m_metrics.helpRequestsBeforeAttempt += m_attemptRecorded ? 0 : 1;
            return AssistantGate::BlockedToolFree;
        }
        if (!m_attemptRecorded) {
            m_metrics.helpRequestsBeforeAttempt++;
            return AssistantGate::BlockedNoAttempt;
        }
        m_metrics.helpRequestsAfterAttempt++;
        return AssistantGate::Allowed;
    }

    // The ONLY task context that may reach the assistant: statement + the
    // user's own attempt (+ prediction). Never the solution explanation.
    std::string assistantContext() const {
        if (!m_task || !m_attemptRecorded) return {};
        std::string context = "Task: " + m_task->prompt + "\n";
        if (m_predictionRecorded)
            context += "Student's prediction: " + m_prediction + "\n";
        if (m_lastAttempt) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Student's attempt: %.6g %s (%s)\n",
                          m_lastAttempt->submittedValue, m_task->answerUnit.c_str(),
                          m_lastAttempt->correct ? "matches the measurement"
                                                 : "does not match the measurement");
            context += buf;
        }
        return context;
    }

    // --- safeguard 5: reliance metrics (facts only) ---------------------------
    const RelianceMetrics& metrics() const { return m_metrics; }

private:
    std::optional<GeneratedTask> m_task;
    SessionMode m_mode = SessionMode::Practice;
    bool m_predictionRecorded = false;
    bool m_attemptRecorded = false;
    std::string m_prediction;
    std::optional<AttemptResult> m_lastAttempt;
    RelianceMetrics m_metrics;
};

} // namespace current_lab::learning
