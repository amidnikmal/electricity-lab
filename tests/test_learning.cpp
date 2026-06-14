#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include "learning/AnkiExport.h"
#include "learning/LearningSession.h"
#include "learning/Lessons.h"
#include "learning/TaskGenerator.h"
#include "assistant/LlmClient.h"
#include "solver/CircuitSolver.h"

using namespace current_lab::learning;

namespace {

const BranchResult* branchFor(const CircuitSolution& solution, int componentId) {
    for (const auto& br : solution.branches)
        if (br.componentId == componentId) return &br;
    return nullptr;
}

} // namespace

// --- task generation: ground truth always from the solver --------------------

TEST(TaskGenerator, GroundTruthMatchesIndependentSolverRun) {
    TaskGenerator generator(42);
    for (int f = 0; f < static_cast<int>(TaskFamily::Count); ++f) {
        for (int difficulty = 1; difficulty <= 3; ++difficulty) {
            auto task = generator.generate(static_cast<TaskFamily>(f), difficulty);
            ASSERT_GE(task.targetComponentId, 0) << taskFamilyName(task.family);
            ASSERT_GT(task.tolerance, 0.0);
            ASSERT_FALSE(task.prompt.empty());
            ASSERT_FALSE(task.predictionPrompt.empty());
            ASSERT_FALSE(task.solutionExplanation.empty());

            CircuitSolver solver;
            double recomputed = 0.0;
            if (task.family == TaskFamily::RcTimeConstant ||
                task.family == TaskFamily::RlTimeConstant) {
                // Recompute by independent transient stepping.
                const Component* target = task.circuit.findComponent(task.targetComponentId);
                ASSERT_NE(target, nullptr);
                double tau = 0.0;
                for (const auto& comp : task.circuit.components) {
                    if (comp.type == ComponentType::Resistor) {
                        if (target->type == ComponentType::Capacitor)
                            tau = comp.value * target->value;
                        else
                            tau = target->value / comp.value;
                    }
                }
                ASSERT_GT(tau, 0.0);
                TransientState state;
                CircuitSolution solution;
                double dt = tau / 1000.0;
                for (int i = 0; i < 1000; ++i)
                    solution = solver.stepTransient(task.circuit, state, dt);
                const BranchResult* br = branchFor(solution, task.targetComponentId);
                ASSERT_NE(br, nullptr);
                recomputed = target->type == ComponentType::Capacitor
                                 ? std::abs(br->voltageDrop)
                                 : std::abs(br->current) * 1000.0;
            } else {
                auto solution = solver.solve(task.circuit);
                const BranchResult* br = branchFor(solution, task.targetComponentId);
                ASSERT_NE(br, nullptr);
                if (task.answerUnit == "mA") recomputed = std::abs(br->current) * 1000.0;
                else if (task.answerUnit == "V") recomputed = std::abs(br->voltageDrop);
                else recomputed = std::abs(br->power) * 1000.0;
            }
            EXPECT_NEAR(task.groundTruth, recomputed, std::abs(recomputed) * 1e-6 + 1e-9)
                << taskFamilyName(task.family) << " d" << difficulty;
        }
    }
}

TEST(TaskGenerator, InterleavingNeverRepeatsFamilyBackToBack) {
    TaskGenerator generator(7);
    auto previous = generator.generateNext(1).family;
    for (int i = 0; i < 50; ++i) {
        auto task = generator.generateNext(1);
        EXPECT_NE(task.family, previous);
        previous = task.family;
    }
}

TEST(TaskGenerator, DifferentSeedsProduceDifferentParameters) {
    TaskGenerator a(1), b(2);
    bool anyDifferent = false;
    for (int i = 0; i < 5; ++i) {
        auto ta = a.generate(TaskFamily::OhmsLaw, 2);
        auto tb = b.generate(TaskFamily::OhmsLaw, 2);
        if (std::abs(ta.groundTruth - tb.groundTruth) > 1e-12) anyDifferent = true;
    }
    EXPECT_TRUE(anyDifferent);
}

// --- session gating: the safeguards are code, not convention ------------------

TEST(LearningSession, SolutionLockedUntilAttempt) {
    TaskGenerator generator(3);
    LearningSession session;
    session.startTask(generator.generate(TaskFamily::OhmsLaw, 1), SessionMode::Practice);

    EXPECT_FALSE(session.canRevealSolution());
    EXPECT_EQ(session.revealSolution(), nullptr);
    EXPECT_EQ(session.metrics().solutionRevealsBlocked, 1);

    session.recordPrediction("current falls if R doubles");
    auto result = session.submitAttempt(1.0);
    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(session.canRevealSolution());
    EXPECT_NE(session.revealSolution(), nullptr);
}

TEST(LearningSession, AttemptRequiresPredictionFirst) {
    TaskGenerator generator(3);
    LearningSession session;
    session.startTask(generator.generate(TaskFamily::OhmsLaw, 1), SessionMode::Practice);

    EXPECT_FALSE(session.canSubmitAttempt());
    auto rejected = session.submitAttempt(5.0);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(session.metrics().attemptsSubmitted, 0);

    session.recordPrediction("falls");
    EXPECT_TRUE(session.canSubmitAttempt());
    auto accepted = session.submitAttempt(5.0);
    EXPECT_TRUE(accepted.accepted);
}

TEST(LearningSession, AssistantBlockedBeforeAttemptAndCounted) {
    TaskGenerator generator(3);
    LearningSession session;
    session.startTask(generator.generate(TaskFamily::OhmsLaw, 1), SessionMode::Practice);

    EXPECT_EQ(session.requestAssistant(), AssistantGate::BlockedNoAttempt);
    EXPECT_EQ(session.requestAssistant(), AssistantGate::BlockedNoAttempt);
    EXPECT_EQ(session.metrics().helpRequestsBeforeAttempt, 2);

    session.recordPrediction("falls");
    session.submitAttempt(1.0);
    EXPECT_EQ(session.requestAssistant(), AssistantGate::Allowed);
    EXPECT_EQ(session.metrics().helpRequestsAfterAttempt, 1);
}

TEST(LearningSession, ToolFreeModeAlwaysBlocksAssistant) {
    TaskGenerator generator(3);
    LearningSession session;
    session.startTask(generator.generate(TaskFamily::OhmsLaw, 1), SessionMode::ToolFreeRetrieval);

    session.recordPrediction("falls");
    session.submitAttempt(1.0);
    EXPECT_EQ(session.requestAssistant(), AssistantGate::BlockedToolFree);
    EXPECT_EQ(session.metrics().toolFreeTasks, 1);
}

TEST(LearningSession, AssistantContextEmptyBeforeAttemptAndNeverHasSolution) {
    TaskGenerator generator(3);
    LearningSession session;
    auto task = generator.generate(TaskFamily::OhmsLaw, 1);
    session.startTask(task, SessionMode::Practice);

    EXPECT_TRUE(session.assistantContext().empty()); // nothing leaves before an attempt

    session.recordPrediction("falls");
    session.submitAttempt(1.0);
    std::string context = session.assistantContext();
    EXPECT_NE(context.find("Task:"), std::string::npos);
    EXPECT_NE(context.find("attempt"), std::string::npos);
    // The derivation/solution text is never part of the assistant payload.
    EXPECT_EQ(context.find(task.solutionExplanation), std::string::npos);
}

TEST(LearningSession, GradesAgainstSolverTruthWithTolerance) {
    TaskGenerator generator(3);
    LearningSession session;
    auto task = generator.generate(TaskFamily::OhmsLaw, 1);
    session.startTask(task, SessionMode::Practice);
    session.recordPrediction("falls");

    auto correct = session.submitAttempt(task.groundTruth + task.tolerance * 0.5);
    EXPECT_TRUE(correct.correct);

    LearningSession session2;
    session2.startTask(task, SessionMode::Practice);
    session2.recordPrediction("falls");
    auto wrong = session2.submitAttempt(task.groundTruth + task.tolerance * 3.0);
    EXPECT_FALSE(wrong.correct);
}

// --- lessons: derivation arc is complete --------------------------------------

TEST(Lessons, EveryLessonHasFullDerivationArc) {
    for (const auto& lesson : lessons()) {
        EXPECT_GT(std::strlen(lesson.arc.situation), 10u) << lesson.id;
        EXPECT_GT(std::strlen(lesson.arc.action), 10u) << lesson.id;
        EXPECT_GT(std::strlen(lesson.arc.words), 10u) << lesson.id;
        EXPECT_GT(std::strlen(lesson.arc.symbols), 5u) << lesson.id;
        EXPECT_GT(std::strlen(lesson.arc.formula), 3u) << lesson.id;
    }
    EXPECT_GE(lessons().size(), 6u);
}

// --- Anki export ---------------------------------------------------------------

TEST(AnkiExport, EscapesJsonSpecials) {
    EXPECT_EQ(escapeJson("a\"b"), "a\\\"b");
    EXPECT_EQ(escapeJson("line1\nline2"), "line1\\nline2");
    EXPECT_EQ(escapeJson("back\\slash"), "back\\\\slash");
}

TEST(AnkiExport, PayloadIsValidAddNotesRequest) {
    TaskGenerator generator(9);
    auto task = generator.generate(TaskFamily::SeriesResistors, 1);
    auto note = noteFromTask(task);

    EXPECT_FALSE(note.front.empty());
    EXPECT_NE(note.back.find(task.answerUnit), std::string::npos);
    EXPECT_NE(note.back.find("Situation"), std::string::npos); // arc, not a bare formula

    std::string payload = buildAddNotesPayload("Current Lab", "Basic", {note});
    EXPECT_NE(payload.find("\"action\":\"addNotes\""), std::string::npos);
    EXPECT_NE(payload.find("\"deckName\":\"Current Lab\""), std::string::npos);
    EXPECT_NE(payload.find("\"modelName\":\"Basic\""), std::string::npos);
    EXPECT_EQ(payload.find('\n'), std::string::npos); // escaped newlines only
}

// --- assistant client -----------------------------------------------------------

TEST(LlmClient, BuildsOpenAiCompatibleRequest) {
    current_lab::assistant::LlmConfig config;
    config.model = "qwen2.5-3b";
    std::vector<current_lab::assistant::ChatMessage> messages = {
        {"system", "be a critic"},
        {"user", "Task: find I.\nStudent's attempt: 5 mA"},
    };
    std::string request = current_lab::assistant::buildChatRequest(config, messages);
    EXPECT_NE(request.find("\"model\":\"qwen2.5-3b\""), std::string::npos);
    EXPECT_NE(request.find("\"role\":\"system\""), std::string::npos);
    EXPECT_NE(request.find("Student's attempt: 5 mA"), std::string::npos);
    EXPECT_EQ(request.find('\n'), std::string::npos);
}

TEST(LlmClient, ExtractsReplyFromChatCompletionResponse) {
    std::string response =
        "{\"id\":\"chatcmpl-1\",\"object\":\"chat.completion\",\"choices\":[{"
        "\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":"
        "\"Your attempt ignores R2.\\nWhat current flows through R2?\"},"
        "\"finish_reason\":\"stop\"}],\"usage\":{}}";
    std::string reply;
    ASSERT_TRUE(current_lab::assistant::extractAssistantReply(response, &reply));
    EXPECT_EQ(reply, "Your attempt ignores R2.\nWhat current flows through R2?");
}

TEST(LlmClient, RejectsMalformedResponse) {
    std::string reply;
    EXPECT_FALSE(current_lab::assistant::extractAssistantReply("{\"error\":\"x\"}", &reply));
}

TEST(LlmClient, CriticPromptForbidsSolving) {
    std::string prompt = current_lab::assistant::socraticCriticSystemPrompt();
    EXPECT_NE(prompt.find("never produce the final numeric answer"), std::string::npos);
    EXPECT_NE(prompt.find("counter-question"), std::string::npos);
}

TEST(TaskGenerator, ThreeSeriesResistorsPromptIsSolvable) {
    TaskGenerator generator(7);
    auto task = generator.generate(TaskFamily::SeriesResistors, 3);

    EXPECT_EQ(task.prompt.find("(third)"), std::string::npos);
    EXPECT_NE(task.prompt.find("R3"), std::string::npos);
    EXPECT_NE(task.prompt.find("R3 ="), std::string::npos);
}
