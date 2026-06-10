#pragma once

#include "circuit/Circuit.h"
#include <cmath>

// Ready-made demo circuits: one per element plus a couple of combinations.
// Pure builders, used by the Demos menu and by tests.
namespace current_lab::demos {

enum class DemoCircuit {
    SeriesResistor,
    RcCapacitor,
    RlInductor,
    DiodeResistor,
    SwitchedRc,
    RlcSeries,
    PeakDetector,
    Count,
};

inline const char* demoName(DemoCircuit demo) {
    switch (demo) {
        case DemoCircuit::SeriesResistor: return "Demo: resistor loop";
        case DemoCircuit::RcCapacitor: return "Demo: RC charging";
        case DemoCircuit::RlInductor: return "Demo: RL current rise";
        case DemoCircuit::DiodeResistor: return "Demo: diode + resistor";
        case DemoCircuit::SwitchedRc: return "Demo: switched RC";
        case DemoCircuit::RlcSeries: return "Demo: RLC series";
        case DemoCircuit::PeakDetector: return "Demo: diode peak detector";
        case DemoCircuit::Count: break;
    }
    return "?";
}

// Closes the loop back to ground RECTANGULARLY: down from the last node,
// then left along the bottom rail (no diagonal hypotenuse wires).
inline void closeLoopRect(Circuit& c, int fromNode, Vec2 fromPos, int gnd, Vec2 gndPos) {
    if (std::abs(fromPos.x - gndPos.x) < 1.0 || std::abs(fromPos.y - gndPos.y) < 1.0) {
        c.addComponent(ComponentType::Wire, fromNode, gnd, 0.0);
        return;
    }
    int corner = c.addNode(Vec2(fromPos.x, gndPos.y));
    c.addComponent(ComponentType::Wire, fromNode, corner, 0.0);
    c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
}

inline Circuit buildDemo(DemoCircuit demo) {
    Circuit c;
    int gnd = c.addNode(Vec2(200, 320), "GND");
    int n1 = c.addNode(Vec2(200, 140), "N1");
    c.groundNodeId = gnd;
    c.addComponent(ComponentType::Ground, gnd, gnd, 0.0);
    c.addComponent(ComponentType::VoltageSource, n1, gnd, 5.0);

    switch (demo) {
        case DemoCircuit::SeriesResistor: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            closeLoopRect(c, n2, Vec2(480, 140), gnd, Vec2(200, 320));
            break;
        }
        case DemoCircuit::RcCapacitor: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            int corner = c.addNode(Vec2(480, 320));
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            c.addComponent(ComponentType::Capacitor, n2, corner, 1e-3); // tau = 1 s
            c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
            break;
        }
        case DemoCircuit::RlInductor: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            int corner = c.addNode(Vec2(480, 320));
            c.addComponent(ComponentType::Resistor, n1, n2, 10.0);
            c.addComponent(ComponentType::Inductor, n2, corner, 1.0); // tau = 0.1 s
            c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
            break;
        }
        case DemoCircuit::DiodeResistor: {
            int n2 = c.addNode(Vec2(420, 140), "N2");
            int n3 = c.addNode(Vec2(620, 140), "N3");
            c.addComponent(ComponentType::Diode, n1, n2, 0.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 1000.0);
            closeLoopRect(c, n3, Vec2(620, 140), gnd, Vec2(200, 320));
            break;
        }
        case DemoCircuit::SwitchedRc: {
            int n2 = c.addNode(Vec2(400, 140), "N2");
            int n3 = c.addNode(Vec2(600, 140), "N3");
            int corner = c.addNode(Vec2(600, 320));
            c.addComponent(ComponentType::Switch, n1, n2, 1.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 1000.0);
            c.addComponent(ComponentType::Capacitor, n3, corner, 1e-3);
            c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
            break;
        }
        case DemoCircuit::RlcSeries: {
            int n2 = c.addNode(Vec2(380, 140), "N2");
            int n3 = c.addNode(Vec2(560, 140), "N3");
            int n4 = c.addNode(Vec2(740, 140), "N4");
            c.addComponent(ComponentType::Resistor, n1, n2, 50.0);
            c.addComponent(ComponentType::Inductor, n2, n3, 1.0);
            c.addComponent(ComponentType::Capacitor, n3, n4, 1e-3);
            closeLoopRect(c, n4, Vec2(740, 140), gnd, Vec2(200, 320));
            break;
        }
        case DemoCircuit::PeakDetector: {
            int n2 = c.addNode(Vec2(480, 140), "N2");
            int corner = c.addNode(Vec2(480, 320));
            c.addComponent(ComponentType::Diode, n1, n2, 0.0);
            c.addComponent(ComponentType::Capacitor, n2, corner, 1e-3);
            c.addComponent(ComponentType::Resistor, n2, corner, 100000.0); // slow bleed
            c.addComponent(ComponentType::Wire, corner, gnd, 0.0);
            break;
        }
        case DemoCircuit::Count:
            break;
    }
    return c;
}

} // namespace current_lab::demos
