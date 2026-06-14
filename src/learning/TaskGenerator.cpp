#include "learning/TaskGenerator.h"
#include "solver/CircuitSolver.h"

#include <cmath>
#include <cstdio>

namespace current_lab::learning {

namespace {

const BranchResult* branchFor(const CircuitSolution& solution, int componentId) {
    for (const auto& br : solution.branches)
        if (br.componentId == componentId) return &br;
    return nullptr;
}

// Функция сама определяет нужный размер буфера через snprintf(nullptr, 0, ...),
// чтобы длинные строки (например, многострочные solutionExplanation) не усекались.
std::string format(const char* fmt, double a = 0.0, double b = 0.0, double c = 0.0, double d = 0.0) {
    int needed = std::snprintf(nullptr, 0, fmt, a, b, c, d);
    if (needed < 0) return {};
    std::string out(static_cast<size_t>(needed) + 1, '\0');
    std::snprintf(out.data(), out.size(), fmt, a, b, c, d);
    out.resize(static_cast<size_t>(needed));
    return out;
}

double toleranceFor(double truth, double absFloor) {
    return std::max(std::abs(truth) * 0.02, absFloor);
}

} // namespace

const char* taskFamilyName(TaskFamily family) {
    switch (family) {
        case TaskFamily::OhmsLaw: return "Ohm's law";
        case TaskFamily::SeriesResistors: return "Series resistors";
        case TaskFamily::ParallelResistors: return "Parallel resistors";
        case TaskFamily::PowerDissipation: return "Power dissipation";
        case TaskFamily::RcTimeConstant: return "RC charging";
        case TaskFamily::RlTimeConstant: return "RL current rise";
        case TaskFamily::Count: break;
    }
    return "?";
}

TaskGenerator::TaskGenerator(unsigned seed) : m_rng(seed) {}

double TaskGenerator::pick(const double* values, int count) {
    std::uniform_int_distribution<int> dist(0, count - 1);
    return values[dist(m_rng)];
}

int TaskGenerator::pickInt(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(m_rng);
}

GeneratedTask TaskGenerator::generateNext(int difficulty) {
    std::uniform_int_distribution<int> dist(0, static_cast<int>(TaskFamily::Count) - 1);
    int family = dist(m_rng);
    if (family == m_lastFamily)
        family = (family + 1 + pickInt(0, static_cast<int>(TaskFamily::Count) - 2)) %
                 static_cast<int>(TaskFamily::Count);
    m_lastFamily = family;
    return generate(static_cast<TaskFamily>(family), difficulty);
}

GeneratedTask TaskGenerator::generate(TaskFamily family, int difficulty) {
    difficulty = std::max(1, std::min(3, difficulty));

    static const double kVoltagesEasy[] = {3.0, 5.0, 9.0, 12.0};
    static const double kVoltagesHard[] = {1.5, 4.5, 6.0, 7.5, 24.0};
    static const double kResistorsEasy[] = {100.0, 200.0, 500.0, 1000.0, 2000.0};
    static const double kResistorsHard[] = {220.0, 330.0, 470.0, 680.0, 1500.0, 4700.0};

    double V = difficulty >= 3 ? pick(kVoltagesHard, 5) : pick(kVoltagesEasy, 4);
    auto pickR = [&]() {
        return difficulty >= 2 ? pick(kResistorsHard, 6) : pick(kResistorsEasy, 5);
    };

    GeneratedTask task;
    task.family = family;
    task.difficulty = difficulty;

    CircuitSolver solver;
    Circuit& c = task.circuit;
    int gnd = c.addNode(Vec2(0, 200), "GND");
    int n1 = c.addNode(Vec2(0, 0), "N1");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, V);

    switch (family) {
        case TaskFamily::OhmsLaw: {
            double R = pickR();
            int rId = c.addComponent(ComponentType::Resistor, n1, gnd, R);
            task.targetComponentId = rId;

            auto solution = solver.solve(c);
            const BranchResult* br = branchFor(solution, rId);
            task.groundTruth = br ? std::abs(br->current) * 1000.0 : 0.0;
            task.answerUnit = "mA";
            task.tolerance = toleranceFor(task.groundTruth, 0.01);
            task.prompt = format(
                "A %.1f V source drives a single %.0f Ohm resistor.\n"
                "Find the current through the resistor, in mA.", V, R);
            task.predictionPrompt =
                "Before solving: if the resistance doubled, would the current rise or fall?";
            task.solutionExplanation = format(
                "Situation: one source, one resistor, one loop.\n"
                "Action: the source holds %.1f V across the resistor.\n"
                "Words: current is voltage pushed through resistance.\n"
                "Symbols: I = V / R = %.1f / %.0f.", V, V, R);
            break;
        }
        case TaskFamily::SeriesResistors: {
            int n2 = c.addNode(Vec2(200, 0), "N2");
            double R1 = pickR();
            double R2 = pickR();
            c.addComponent(ComponentType::Resistor, n1, n2, R1);
            int r2Id;
            if (difficulty >= 3) {
                int n3 = c.addNode(Vec2(400, 0), "N3");
                double R3 = pickR();
                r2Id = c.addComponent(ComponentType::Resistor, n2, n3, R2);
                c.addComponent(ComponentType::Resistor, n3, gnd, R3);
                // R3 теперь выводится в условии, чтобы студент мог решить задачу аналитически
                task.prompt = format(
                    "Three resistors in series across a source: R1 = %.0f Ohm, R2 = %.0f Ohm, R3 = %.0f Ohm.\n",
                    R1, R2, R3);
                task.prompt += format("Source voltage: %.1f V. Find the voltage across R2, in V.", V);
            } else {
                r2Id = c.addComponent(ComponentType::Resistor, n2, gnd, R2);
                task.prompt = format(
                    "Two resistors in series across a %.1f V source: R1 = %.0f Ohm, R2 = %.0f Ohm.\n",
                    V, R1, R2);
                task.prompt += "Find the voltage across R2, in V.";
            }
            task.targetComponentId = r2Id;

            auto solution = solver.solve(c);
            const BranchResult* br = branchFor(solution, r2Id);
            task.groundTruth = br ? std::abs(br->voltageDrop) : 0.0;
            task.answerUnit = "V";
            task.tolerance = toleranceFor(task.groundTruth, 0.005);
            task.predictionPrompt =
                "Before solving: is the current through R1 larger than, smaller than, or equal to the current through R2?";
            task.solutionExplanation = format(
                "Situation: resistors share one current path.\n"
                "Action: the same current flows through every series element.\n"
                "Words: voltage divides in proportion to resistance.\n"
                "Symbols: V2 = Vsrc * R2 / Rtotal = %.1f * %.0f / Rtotal.", V, R2);
            break;
        }
        case TaskFamily::ParallelResistors: {
            double R1 = pickR();
            double R2 = pickR();
            int srcId = c.components[1].id; // the voltage source added above
            c.addComponent(ComponentType::Resistor, n1, gnd, R1);
            c.addComponent(ComponentType::Resistor, n1, gnd, R2);
            task.targetComponentId = srcId;

            auto solution = solver.solve(c);
            const BranchResult* br = branchFor(solution, srcId);
            task.groundTruth = br ? std::abs(br->current) * 1000.0 : 0.0;
            task.answerUnit = "mA";
            task.tolerance = toleranceFor(task.groundTruth, 0.01);
            task.prompt = format(
                "Two resistors in parallel across a %.1f V source: R1 = %.0f Ohm, R2 = %.0f Ohm.\n",
                V, R1, R2);
            task.prompt += "Find the total current delivered by the source, in mA.";
            task.predictionPrompt =
                "Before solving: if we remove R2, will the current through R1 change?";
            task.solutionExplanation = format(
                "Situation: both resistors see the full source voltage.\n"
                "Action: each draws its own current; the source supplies the sum.\n"
                "Words: parallel paths add currents.\n"
                "Symbols: I = V/R1 + V/R2 = %.1f/%.0f + %.1f/%.0f.", V, R1, V, R2);
            break;
        }
        case TaskFamily::PowerDissipation: {
            double R = pickR();
            int rId = c.addComponent(ComponentType::Resistor, n1, gnd, R);
            task.targetComponentId = rId;

            auto solution = solver.solve(c);
            const BranchResult* br = branchFor(solution, rId);
            task.groundTruth = br ? std::abs(br->power) * 1000.0 : 0.0;
            task.answerUnit = "mW";
            task.tolerance = toleranceFor(task.groundTruth, 0.05);
            task.prompt = format(
                "A %.0f Ohm resistor is connected across a %.1f V source.\n"
                "Find the power it dissipates as heat, in mW.", R, V);
            task.predictionPrompt =
                "Before solving: if the voltage doubled, by what factor would the heat grow?";
            task.solutionExplanation = format(
                "Situation: the resistor turns electrical energy into heat.\n"
                "Action: voltage drives current; both act on the same element.\n"
                "Words: power is voltage times current.\n"
                "Symbols: P = V*I = V^2/R = %.1f^2 / %.0f.", V, R);
            break;
        }
        case TaskFamily::RcTimeConstant: {
            int n2 = c.addNode(Vec2(200, 0), "N2");
            double R = pickR();
            static const double kCaps[] = {1e-4, 2e-4, 5e-4, 1e-3};
            double C = pick(kCaps, 4);
            c.addComponent(ComponentType::Resistor, n1, n2, R);
            int capId = c.addComponent(ComponentType::Capacitor, n2, gnd, C);
            task.targetComponentId = capId;

            // Ground truth by transient stepping, not by the analytic formula.
            double tau = R * C;
            double dt = tau / 1000.0;
            TransientState state;
            CircuitSolution solution;
            for (int i = 0; i < 1000; ++i)
                solution = solver.stepTransient(c, state, dt);
            const BranchResult* br = branchFor(solution, capId);
            task.groundTruth = br ? std::abs(br->voltageDrop) : 0.0;
            task.answerUnit = "V";
            task.tolerance = std::max(task.groundTruth * 0.03, 0.02);
            task.prompt = format(
                "An uncharged capacitor C = %.0f uF charges through R = %.0f Ohm from a %.1f V source.\n",
                C * 1e6, R, V);
            task.prompt += format(
                "Find the capacitor voltage at t = tau = R*C = %.3f s, in V.", tau);
            task.predictionPrompt =
                "Before running: at t = tau, is Vc above or below half the source voltage?";
            task.solutionExplanation = format(
                "Situation: the source pushes charge onto the plates through R.\n"
                "Action: as Vc rises, the remaining push (V - Vc) shrinks, so charging slows.\n"
                "Words: after one time constant the capacitor reaches about 63%% of the source.\n"
                "Symbols: Vc(t) = V*(1 - e^(-t/RC)); Vc(tau) = %.1f * 0.632.", V);
            break;
        }
        case TaskFamily::RlTimeConstant: {
            int n2 = c.addNode(Vec2(200, 0), "N2");
            static const double kRs[] = {5.0, 10.0, 20.0, 50.0};
            static const double kLs[] = {0.5, 1.0, 2.0};
            double R = pick(kRs, 4);
            double L = pick(kLs, 3);
            c.addComponent(ComponentType::Resistor, n1, n2, R);
            int indId = c.addComponent(ComponentType::Inductor, n2, gnd, L);
            task.targetComponentId = indId;

            double tau = L / R;
            double dt = tau / 1000.0;
            TransientState state;
            CircuitSolution solution;
            for (int i = 0; i < 1000; ++i)
                solution = solver.stepTransient(c, state, dt);
            const BranchResult* br = branchFor(solution, indId);
            task.groundTruth = br ? std::abs(br->current) * 1000.0 : 0.0;
            task.answerUnit = "mA";
            task.tolerance = std::max(task.groundTruth * 0.03, 0.5);
            task.prompt = format(
                "An inductor L = %.1f H in series with R = %.0f Ohm connects to a %.1f V source at t = 0.\n",
                L, R, V);
            task.prompt += format(
                "Find the current at t = tau = L/R = %.3f s, in mA.", tau);
            task.predictionPrompt =
                "Before running: right after switch-on, is the current zero or maximal?";
            task.solutionExplanation = format(
                "Situation: the inductor resists changes of current.\n"
                "Action: current starts at zero and climbs as the inductor yields.\n"
                "Words: after one time constant it reaches about 63%% of the final V/R.\n"
                "Symbols: I(t) = (V/R)*(1 - e^(-t*R/L)); I(tau) = (%.1f/%.0f) * 0.632.", V, R);
            break;
        }
        case TaskFamily::Count:
            break;
    }

    return task;
}

} // namespace current_lab::learning
