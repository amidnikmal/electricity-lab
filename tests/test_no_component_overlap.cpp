// Инвариант раскладки демо-схем: ДВА функциональных элемента (резистор,
// источник, конденсатор, катушка, диод, ключ) не лежат один на другом — их
// нарисованные ТЕЛА (глифы) не перекрываются на экране. Касание в общем
// узле-конце допускается: тела центрированы и укорочены, так что общий вывод
// им не принадлежит.
//
// Геометрия тел берётся РОВНО из тех же формул, что рисует ProjectionBuilder
// (projection/ElementGeometry.h + physics/ResistiveElementModel.h), поэтому
// тест проверяет именно то, что увидит студент, а не абстрактные отрезки узлов.
//
// Провода (Wire) и земля (Ground) структурные — их тел нет, в проверку не
// входят (но мы при желании могли бы добавить и их; сейчас задача — именно
// функциональные элементы палитры).

#include <gtest/gtest.h>

#include "circuit/Circuit.h"
#include "circuit/DemoCircuits.h"
#include "projection/ElementGeometry.h"
#include "physics/ResistiveElementModel.h"
#include "math/Vec2.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace current_lab;

// Та же толщина провода, что по умолчанию в ViewParams (см. ProjectionBuilder.h).
constexpr double kWireThickness = 8.0;

// Только функциональные элементы палитры имеют тело-глиф.
bool isFunctional(ComponentType t) {
    switch (t) {
        case ComponentType::Resistor:
        case ComponentType::VoltageSource:
        case ComponentType::AcVoltageSource:
        case ComponentType::Capacitor:
        case ComponentType::Inductor:
        case ComponentType::Diode:
        case ComponentType::Switch:
            return true;
        default:
            return false; // Wire, Ground
    }
}

struct Rect {
    Vec2 mn, mx;
    bool valid = false;
};

// Прямоугольник вокруг отрезка [s,e] с перпендикулярной полушириной hw.
// Все демо-элементы выложены по осям, поэтому AABB точно описывает глиф.
Rect rectFromSegment(Vec2 s, Vec2 e, double hw) {
    Vec2 ab = e - s;
    double len = ab.length();
    if (len < 1e-6) return {};
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);
    Vec2 corners[4] = {
        s + perp * hw, s - perp * hw,
        e + perp * hw, e - perp * hw,
    };
    Rect r;
    r.mn = r.mx = corners[0];
    for (int i = 1; i < 4; ++i) {
        r.mn.x = std::min(r.mn.x, corners[i].x);
        r.mn.y = std::min(r.mn.y, corners[i].y);
        r.mx.x = std::max(r.mx.x, corners[i].x);
        r.mx.y = std::max(r.mx.y, corners[i].y);
    }
    r.valid = true;
    return r;
}

// Ограничивающий прямоугольник ТЕЛА (глифа) функционального элемента a->b,
// вычисленный по тем же формулам, что использует ProjectionBuilder при отрисовке.
Rect bodyRect(const Component& comp, Vec2 a, Vec2 b) {
    using namespace current_lab::projection;
    using namespace current_lab::physics;

    switch (comp.type) {
        case ComponentType::Resistor: {
            // Тело резистора — центральная секция ResistiveBody.
            auto sections = resistorPathSections(a, b, 0.0, 0.0, kWireThickness);
            const ConductivePathSection* body = nullptr;
            for (const auto& s : sections)
                if (s.material == VisualMaterial::ResistiveBody) { body = &s; break; }
            if (!body) return {};
            return rectFromSegment(body->start, body->end, body->halfWidth);
        }
        case ComponentType::VoltageSource:
        case ComponentType::AcVoltageSource: {
            // Глиф — круг радиуса r=15 в середине (см. emitVoltageSource).
            Vec2 mid = (a + b) * 0.5;
            double r = 15.0;
            Rect rr;
            rr.mn = mid - Vec2(r, r);
            rr.mx = mid + Vec2(r, r);
            rr.valid = true;
            return rr;
        }
        case ComponentType::Capacitor: {
            // Тело — две пластины: интервал по оси [leadAEnd..leadBEnd],
            // ширина = 2*plateHalf по перпендикуляру (см. capacitorGeometry).
            auto g = capacitorGeometry(a, b, kWireThickness);
            if (!g.valid) return {};
            return rectFromSegment(g.leadAEnd, g.leadBEnd, g.plateHalf);
        }
        case ComponentType::Inductor: {
            // Тело — катушка [coilStart..coilEnd]; горбы выступают на bumpRadius
            // в одну сторону от оси (см. inductorBumpArc).
            auto g = inductorGeometry(a, b, kWireThickness);
            if (!g.valid) return {};
            return rectFromSegment(g.coilStart, g.coilEnd, g.bumpRadius);
        }
        case ComponentType::Diode: {
            // Глиф — треугольник + планка в центральном интервале длиной s
            // (см. emitDiodeSymbol).
            Vec2 ab = b - a;
            double len = ab.length();
            if (len < 1.0) return {};
            Vec2 unit = ab / len;
            double s = std::clamp(kWireThickness * 1.5, 10.0, len * 0.4);
            Vec2 mid = a + ab * 0.5;
            Vec2 leadAEnd = mid - unit * (s * 0.5);
            Vec2 leadBEnd = mid + unit * (s * 0.5);
            return rectFromSegment(leadAEnd, leadBEnd, s * 0.55);
        }
        case ComponentType::Switch: {
            // Глиф — рычаг в центральном интервале (см. switchGeometry):
            // [leadAEnd..leadBEnd] длиной s, рычаг выступает ~0.6*s по перпендикуляру.
            auto g = projection::switchGeometry(a, b);
            if (!g.valid) return {};
            return rectFromSegment(g.leadAEnd, g.leadBEnd, g.s * 0.6);
        }
        default:
            return {};
    }
}

// Площадь пересечения двух AABB > 0? (Касание границей не считается перекрытием.)
bool rectsOverlap(const Rect& p, const Rect& q) {
    if (!p.valid || !q.valid) return false;
    constexpr double eps = 1e-6; // касание в пределах eps не считаем перекрытием
    double ix = std::min(p.mx.x, q.mx.x) - std::max(p.mn.x, q.mn.x);
    double iy = std::min(p.mx.y, q.mx.y) - std::max(p.mn.y, q.mn.y);
    return ix > eps && iy > eps;
}

} // namespace

TEST(DemoLayout, FunctionalComponentBodiesDoNotOverlap) {
    using demos::DemoCircuit;

    for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
        auto demo = static_cast<DemoCircuit>(d);
        Circuit circuit = demos::buildDemo(demo);
        const char* name = demos::demoName(demo);

        // Соберём тела всех функциональных элементов.
        struct Body { int compId; ComponentType type; Rect rect; };
        std::vector<Body> bodies;
        for (const auto& comp : circuit.components) {
            if (!isFunctional(comp.type)) continue;
            const Node* na = circuit.findNode(comp.nodeA);
            const Node* nb = circuit.findNode(comp.nodeB);
            ASSERT_NE(na, nullptr) << name << ": компонент " << comp.id << " ссылается на отсутствующий узел A";
            ASSERT_NE(nb, nullptr) << name << ": компонент " << comp.id << " ссылается на отсутствующий узел B";
            Rect r = bodyRect(comp, na->position, nb->position);
            EXPECT_TRUE(r.valid) << name << ": не удалось построить тело компонента " << comp.id
                                 << " (тип " << static_cast<int>(comp.type) << ")";
            if (r.valid) bodies.push_back({comp.id, comp.type, r});
        }

        for (size_t i = 0; i < bodies.size(); ++i) {
            for (size_t j = i + 1; j < bodies.size(); ++j) {
                EXPECT_FALSE(rectsOverlap(bodies[i].rect, bodies[j].rect))
                    << name << ": тела компонентов перекрываются — "
                    << "id=" << bodies[i].compId << " (тип " << static_cast<int>(bodies[i].type) << ")"
                    << " и id=" << bodies[j].compId << " (тип " << static_cast<int>(bodies[j].type) << ")";
            }
        }
    }
}
