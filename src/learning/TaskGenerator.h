#pragma once

#include "circuit/Circuit.h"
#include <random>
#include <string>

// Procedural circuit-task generation. The ground-truth answer is ALWAYS
// computed by the solver (DC MNA or transient stepping), never hard-coded.
namespace current_lab::learning {

enum class TaskFamily {
    OhmsLaw,               // I from V and R
    SeriesResistors,       // voltage across one resistor in a chain
    ParallelResistors,     // total current from the source
    PowerDissipation,      // P of a resistor
    RcTimeConstant,        // Vc at t = tau while charging
    RlTimeConstant,        // Il at t = tau while rising
    CurrentConservation,   // prediction: current is NOT consumed in series
    BatteryVoltageNotCurrent, // prediction: battery fixes voltage, not current
    BrightnessVsPosition,  // prediction: brightness does not depend on position in series
    Count,
};

const char* taskFamilyName(TaskFamily family);

struct GeneratedTask {
    TaskFamily family = TaskFamily::OhmsLaw;
    int difficulty = 1; // 1..3
    std::string prompt;            // situation -> action -> question
    std::string predictionPrompt;  // qualitative predict-then-verify question
    Circuit circuit;               // the actual model (loadable into the canvas)
    int targetComponentId = -1;
    std::string answerUnit;
    double groundTruth = 0.0; // from the solver
    double tolerance = 0.0;   // acceptance band around groundTruth
    std::string solutionExplanation; // arc: situation -> action -> words -> symbols -> formula
};

class TaskGenerator {
public:
    explicit TaskGenerator(unsigned seed = std::random_device{}());

    GeneratedTask generate(TaskFamily family, int difficulty);

    // Interleaved practice: never repeats the previous family, so consecutive
    // tasks are not reverse-hints of each other.
    GeneratedTask generateNext(int difficulty);

private:
    std::mt19937 m_rng;
    int m_lastFamily = -1;

    double pick(const double* values, int count);
    int pickInt(int lo, int hi);
};

} // namespace current_lab::learning
