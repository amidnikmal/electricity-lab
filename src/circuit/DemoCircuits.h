#pragma once

#include "circuit/Circuit.h"
#include <cmath>

// Ready-made demo circuits: one per element plus a couple of combinations.
// Pure builders, used by the Demos menu and by tests.
namespace current_lab::demos {

enum class DemoCircuit {
    // Минимальный контур на КАЖДЫЙ функциональный элемент палитры
    // (Wire/Ground структурные — присутствуют в каждом демо, отдельных нет):
    SourceResistor,   // Source: источник + один резистор (простейший замкнутый контур)
    ResistorDivider,  // Resistor: делитель из двух резисторов (сложение сопротивлений)
    RcCapacitor,      // Capacitor: RC-заряд
    RlInductor,       // Inductor: RL-нарастание тока
    DiodeResistor,    // Diode: диод + резистор (односторонняя проводимость)
    SwitchResistor,   // Switch: ключ + резистор (ток включается щелчком)
    // Комбинации нескольких элементов:
    SwitchedRc,       // ключ + RC (переходный процесс запускает щелчок)
    RlcSeries,        // R + L + C ПОСЛЕДОВАТЕЛЬНО: C блокирует DC -> в стационаре
                      // ток=0, вода (в Water-виде) правильно ОСТАНАВЛИВАЕТСЯ
    RlcCirculating,   // RLC с ШУНТОВЫМ C: путь DC через R-L сохранён -> вода
                      // ЦИРКУЛИРУЕТ по контуру и после устаканивания тока
    PeakDetector,     // диод + RC (пик-детектор)
    Count,
};

inline const char* demoName(DemoCircuit demo) {
    switch (demo) {
        case DemoCircuit::SourceResistor: return "Demo: source + resistor";
        case DemoCircuit::ResistorDivider: return "Demo: resistor divider";
        case DemoCircuit::RcCapacitor: return "Demo: RC charging";
        case DemoCircuit::RlInductor: return "Demo: RL current rise";
        case DemoCircuit::DiodeResistor: return "Demo: diode + resistor";
        case DemoCircuit::SwitchResistor: return "Demo: switch + resistor";
        case DemoCircuit::SwitchedRc: return "Demo: switched RC";
        case DemoCircuit::RlcSeries: return "Demo: RLC series";
        case DemoCircuit::RlcCirculating: return "Demo: RLC (circulating water)";
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
        case DemoCircuit::SourceResistor: {
            // Source: ЭДС гонит ток через ЕДИНСТВЕННЫЙ резистор — простейший
            // замкнутый контур, фундамент всех остальных демо.
            int n2 = c.addNode(Vec2(480, 140), "N2");
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            closeLoopRect(c, n2, Vec2(480, 140), gnd, Vec2(200, 320));
            break;
        }
        case DemoCircuit::ResistorDivider: {
            // Resistor: два резистора последовательно образуют делитель
            // напряжения (V(N2) = 5В * 2k/(1k+2k) ≈ 3.33В) — наглядное
            // сложение сопротивлений и деление напряжения.
            int n2 = c.addNode(Vec2(400, 140), "N2");
            int n3 = c.addNode(Vec2(600, 140), "N3");
            c.addComponent(ComponentType::Resistor, n1, n2, 1000.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 2000.0);
            closeLoopRect(c, n3, Vec2(600, 140), gnd, Vec2(200, 320));
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
        case DemoCircuit::SwitchResistor: {
            // Switch: ключ + резистор — ток включается/выключается щелчком по
            // ключу. Стартует РАЗОМКНУТЫМ (как SwitchedRc): в живом режиме
            // замыкание цепи студентом и есть начало процесса.
            int n2 = c.addNode(Vec2(400, 140), "N2");
            int n3 = c.addNode(Vec2(600, 140), "N3");
            c.addComponent(ComponentType::Switch, n1, n2, 0.0);
            c.addComponent(ComponentType::Resistor, n2, n3, 1000.0);
            closeLoopRect(c, n3, Vec2(600, 140), gnd, Vec2(200, 320));
            break;
        }
        case DemoCircuit::SwitchedRc: {
            int n2 = c.addNode(Vec2(400, 140), "N2");
            int n3 = c.addNode(Vec2(600, 140), "N3");
            int corner = c.addNode(Vec2(600, 320));
            // Открыт: историю запускает САМ студент щелчком по ключу
            // (живой режим: переходный процесс = момент замыкания цепи).
            c.addComponent(ComponentType::Switch, n1, n2, 0.0);
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
        case DemoCircuit::RlcCirculating: {
            // RLC, где C стоит ШУНТОМ (а не последовательно): путь постоянного
            // тока через R1-L-R2 сохранён, поэтому ток I = V/(R1+R2) течёт и в
            // стационаре -> в Water-виде вода ЦИРКУЛИРУЕТ по всему контуру даже
            // после устаканивания. C тапнут в среднюю точку (узел n2) и заряжается
            // как «бак» до напряжения делителя (~2.5 В), не неся постоянного тока
            // и НЕ разрывая контур (в отличие от RlcSeries, где C его рвёт и
            // поток правильно встаёт). Прямоугольная раскладка без диагоналей:
            //   gnd-(источник)-n1-(R1)-n2-(L)-n3-(R2)-corner-(низ)-mid-(низ)-gnd,
            //   C: n2 -> mid (вертикальный шунт в среднюю точку нижней шины).
            int n2 = c.addNode(Vec2(400, 140), "N2");
            int n3 = c.addNode(Vec2(600, 140), "N3");
            int corner = c.addNode(Vec2(600, 320));
            int mid = c.addNode(Vec2(400, 320));
            c.addComponent(ComponentType::Resistor, n1, n2, 50.0);
            c.addComponent(ComponentType::Inductor, n2, n3, 1.0);
            c.addComponent(ComponentType::Resistor, n3, corner, 50.0);
            c.addComponent(ComponentType::Capacitor, n2, mid, 1e-3); // шунт
            c.addComponent(ComponentType::Wire, corner, mid, 0.0);
            c.addComponent(ComponentType::Wire, mid, gnd, 0.0);
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
