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
            } else if (task.answerUnit == "ratio") {
                auto solution = solver.solve(task.circuit);
                if (task.family == TaskFamily::CurrentConservation ||
                    task.family == TaskFamily::BrightnessVsPosition) {
                    const Component* r1 = nullptr;
                    const Component* r2 = nullptr;
                    for (const auto& comp : task.circuit.components) {
                        if (comp.type == ComponentType::Resistor) {
                            if (!r1) r1 = &comp;
                            else if (!r2) { r2 = &comp; break; }
                        }
                    }
                    ASSERT_NE(r1, nullptr);
                    ASSERT_NE(r2, nullptr);
                    const BranchResult* br1 = branchFor(solution, r1->id);
                    const BranchResult* br2 = branchFor(solution, r2->id);
                    ASSERT_NE(br1, nullptr);
                    ASSERT_NE(br2, nullptr);
                    if (task.family == TaskFamily::CurrentConservation) {
                        double I1 = std::abs(br1->current);
                        double I2 = std::abs(br2->current);
                        recomputed = (I2 > 1e-12) ? (I1 / I2) : 1.0;
                    } else {
                        double P1 = std::abs(br1->power);
                        double P2 = std::abs(br2->power);
                        recomputed = (P2 > 1e-12) ? (P1 / P2) : 1.0;
                    }
                } else if (task.family == TaskFamily::BatteryVoltageNotCurrent) {
                    double Rorig = 0.0, Vsrc = 0.0;
                    for (const auto& comp : task.circuit.components) {
                        if (comp.type == ComponentType::Resistor) Rorig = comp.value;
                        if (comp.type == ComponentType::VoltageSource) Vsrc = comp.value;
                    }
                    ASSERT_GT(Rorig, 0.0);
                    ASSERT_GT(Vsrc, 0.0);
                    auto pos = task.prompt.find("with a ");
                    ASSERT_NE(pos, std::string::npos);
                    pos += 7;
                    double Rnew = std::stod(task.prompt.substr(pos));
                    ASSERT_GT(Rnew, 0.0);
                    Circuit c2;
                    int gnd2 = c2.addNode(Vec2(0, 200), "GND");
                    int n12 = c2.addNode(Vec2(0, 0), "N1");
                    c2.groundNodeId = gnd2;
                    c2.addComponent(ComponentType::Ground, gnd2, gnd2, 0.0);
                    c2.addComponent(ComponentType::VoltageSource, n12, gnd2, Vsrc);
                    c2.addComponent(ComponentType::Resistor, n12, gnd2, Rnew);
                    CircuitSolver solver2;
                    auto sol2 = solver2.solve(c2);
                    const BranchResult* br = branchFor(solution, task.targetComponentId);
                    ASSERT_NE(br, nullptr);
                    double Iorig = std::abs(br->current);
                    double Inew = 0.0;
                    for (const auto& b : sol2.branches) {
                        const Component* c = c2.findComponent(b.componentId);
                        if (c && c->type == ComponentType::Resistor) {
                            Inew = std::abs(b.current);
                            break;
                        }
                    }
                    recomputed = (Iorig > 1e-12) ? (Inew / Iorig) : 0.0;
                }
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

// --- prediction questions against common misconceptions -----------------------

TEST(TaskGenerator, CurrentConservationQuestionAddressesMisconception) {
    TaskGenerator generator(1);
    auto task = generator.generate(TaskFamily::CurrentConservation, 2);

    EXPECT_FALSE(task.predictionPrompt.empty());
    EXPECT_NE(task.solutionExplanation.find("NOT consumed"), std::string::npos);
    EXPECT_NE(task.solutionExplanation.find("charge conservation"), std::string::npos);
    EXPECT_NEAR(task.groundTruth, 1.0, 0.001);
    EXPECT_EQ(task.answerUnit, "ratio");
}

TEST(TaskGenerator, BatteryVoltageNotCurrentQuestionAddressesMisconception) {
    TaskGenerator generator(1);
    auto task = generator.generate(TaskFamily::BatteryVoltageNotCurrent, 2);

    EXPECT_FALSE(task.predictionPrompt.empty());
    EXPECT_NE(task.solutionExplanation.find("voltage source"), std::string::npos);
    EXPECT_NE(task.solutionExplanation.find("not a current source"), std::string::npos);
    EXPECT_GT(task.groundTruth, 0.0);
    EXPECT_LT(task.groundTruth, 1.0);
    EXPECT_EQ(task.answerUnit, "ratio");
}

TEST(TaskGenerator, BrightnessVsPositionQuestionAddressesMisconception) {
    TaskGenerator generator(1);
    auto task = generator.generate(TaskFamily::BrightnessVsPosition, 2);

    EXPECT_FALSE(task.predictionPrompt.empty());
    EXPECT_NE(task.solutionExplanation.find("does NOT depend"), std::string::npos);
    EXPECT_NE(task.solutionExplanation.find("identical"), std::string::npos);
    EXPECT_NEAR(task.groundTruth, 1.0, 0.001);
    EXPECT_EQ(task.answerUnit, "ratio");
}

TEST(TaskGenerator, PredictionQuestionsHaveCorrectAnswerAndExplanation) {
    TaskGenerator generator(42);
    for (auto family : {TaskFamily::CurrentConservation,
                        TaskFamily::BatteryVoltageNotCurrent,
                        TaskFamily::BrightnessVsPosition}) {
        for (int d = 1; d <= 3; ++d) {
            auto task = generator.generate(family, d);
            ASSERT_FALSE(task.prompt.empty()) << taskFamilyName(family);
            ASSERT_FALSE(task.predictionPrompt.empty()) << taskFamilyName(family);
            ASSERT_FALSE(task.solutionExplanation.empty()) << taskFamilyName(family);
            ASSERT_GT(task.solutionExplanation.size(), 50u) << taskFamilyName(family);
            ASSERT_GE(task.targetComponentId, 0);
            ASSERT_GT(task.tolerance, 0.0);
        }
    }
}
