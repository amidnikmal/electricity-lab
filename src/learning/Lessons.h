#pragma once

#include "circuit/DemoCircuits.h"
#include "learning/TaskGenerator.h"
#include <vector>

// Lesson content. Every unit follows the derivation arc
// situation -> action -> words -> symbols -> formula, and is tied to a
// generated simulator task. No isolated formula cards, no motivational text.
// The arc itself is never delegated to the assistant (safeguard 6).
namespace current_lab::learning {

struct LessonStep {
    const char* situation;
    const char* action;
    const char* words;
    const char* symbols;
    const char* formula;
};

struct Lesson {
    const char* id;
    const char* title;
    LessonStep arc;
    TaskFamily practiceFamily;
};

// Canonical demo circuit for a lesson family: loadable into the simulator as
// a preset (fixed values, tidy layout). Independent from the randomized tasks.
inline Circuit lessonPresetCircuit(TaskFamily family) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 320), "GND");
    int n1 = c.addNode(Vec2(200, 140), "N1");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);

    switch (family) {
        case TaskFamily::OhmsLaw: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            demos::closeLoopRect(c, n2, Vec2(480, 140), gnd, Vec2(200, 320));
            break;
        }
        case TaskFamily::SeriesResistors: {
            int n2 = c.addNode(Vec2(420, 140), "N2");
            int n3 = c.addNode(Vec2(620, 140), "N3");
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 2000.0);
            demos::closeLoopRect(c, n3, Vec2(620, 140), gnd, Vec2(200, 320));
            break;
        }
        case TaskFamily::ParallelResistors: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            int n3 = c.addNode(Vec2(480, 320), "N3");
            c.addComponent(ComponentType::Wire, n1, n2, 0.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 1000.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 2000.0);
            demos::closeLoopRect(c, n3, Vec2(480, 320), gnd, Vec2(200, 320));
            break;
        }
        case TaskFamily::PowerDissipation: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            c.addComponent(ComponentType::Resistor, n1, n2, 500.0);
            demos::closeLoopRect(c, n2, Vec2(480, 140), gnd, Vec2(200, 320));
            break;
        }
        case TaskFamily::RcTimeConstant: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            int corner = c.addNode(Vec2(480, 320));
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            c.addComponent(ComponentType::Capacitor, n2, corner, 1e-3); // tau = 1 s
            c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
            break;
        }
        case TaskFamily::RlTimeConstant: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            int corner = c.addNode(Vec2(480, 320));
            c.addComponent(ComponentType::Resistor, n1, n2, 10.0);
            c.addComponent(ComponentType::Inductor, n2, corner, 1.0); // tau = 0.1 s
            c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
            break;
        }
        case TaskFamily::Count:
            break;
    }
    return c;
}

inline const std::vector<Lesson>& lessons() {
    static const std::vector<Lesson> kLessons = {
        {"ohm", "Current from voltage and resistance",
         {"A battery is wired to a single resistor; charge flows around the loop.",
          "Raise the source voltage in the simulator and watch the current readout.",
          "More push gives proportionally more flow; more resistance gives less flow.",
          "I is proportional to V and inversely proportional to R.",
          "I = \\frac{V}{R}"},
         TaskFamily::OhmsLaw},

        {"series", "Voltage division in a series chain",
         {"Two resistors share one path: every coulomb passes through both.",
          "Select each resistor and compare the voltage drops in the inspector.",
          "The same current flows everywhere; the bigger resistor takes the bigger share of voltage.",
          "V1 : V2 = R1 : R2, with V1 + V2 equal to the source voltage.",
          "V_k = V \\cdot \\frac{R_k}{R_1 + R_2}"},
         TaskFamily::SeriesResistors},

        {"parallel", "Currents add in parallel branches",
         {"Two resistors connect the same pair of nodes, so each sees the full source voltage.",
          "Watch the drift animation split between branches in the Physics view.",
          "Each branch draws its own current independently; the source supplies the sum.",
          "Itotal = I1 + I2, each from its own Ohm's law.",
          "I = \\frac{V}{R_1} + \\frac{V}{R_2}"},
         TaskFamily::ParallelResistors},

        {"power", "Where the heat comes from",
         {"A resistor warms up while current flows through it.",
          "Enable the Heat layer and raise the voltage; the glow follows the dissipated power.",
          "Power is how much energy the charge delivers per second: voltage times current.",
          "P = V * I, and with Ohm's law P = V^2 / R = I^2 R.",
          "P = V \\cdot I = \\frac{V^2}{R}"},
         TaskFamily::PowerDissipation},

        {"rc", "Charging a capacitor takes time",
         {"A capacitor charges through a resistor after the switch closes.",
          "Switch to Transient mode, press Run, and watch Vc climb and flatten.",
          "Charging slows as the capacitor fills: the remaining push shrinks with every volt gained.",
          "The voltage approaches the source exponentially with time constant tau = R*C.",
          "V_C(t) = V(1 - e^{-t/\\tau}),   \\tau = RC"},
         TaskFamily::RcTimeConstant},

        {"continuity", "Why series current is the same everywhere",
         {"Squeeze the brake in the Mechanics view: the whole chain slows at once, not just one link.",
          "Raise the resistor value and watch the speed drop everywhere in the loop simultaneously.",
          "The chain cannot stretch, and the electron gas cannot pile up: the wire stays neutral, so one loop carries one current.",
          "Charge conservation + quasi-neutrality: what flows in must flow out at every cross-section.",
          "I_1 = I_2 = I"},
         TaskFamily::SeriesResistors},

        {"rl", "An inductor resists change of current",
         {"An inductor in series with a resistor connects to a source at t = 0.",
          "Run the transient and watch the flywheel in the Mechanics view spin up gradually.",
          "Current cannot jump: the inductor stores energy in its field and yields slowly.",
          "The current approaches V/R exponentially with time constant tau = L/R.",
          "I(t) = \\frac{V}{R}(1 - e^{-t/\\tau}),   \\tau = \\frac{L}{R}"},
         TaskFamily::RlTimeConstant},
    };
    return kLessons;
}

} // namespace current_lab::learning
