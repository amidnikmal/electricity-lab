#include "projection/ProjectionBuilder.h"
#include "projection/ElementGeometry.h"
#include "projection/HydraulicMapping.h"
#include "projection/MechanicsMapping.h"
#include "projection/MechanicsCapacitor.h"
#include "render/ColorMaps.h"
#include "ui/I18n.h"
#include "physics/ChainGeometry.h"
#include "physics/ChannelSpecs.h"
#include "physics/DriftModel.h"
#include "physics/FieldModel.h"
#include "physics/MagneticFieldModel.h"
#include "physics/PowerModel.h"
#include "physics/ResistiveElementModel.h"
#include "physics/SurfaceChargeModel.h"
#include "physics/WirePhysics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace current_lab::projection {

namespace {

using render::packColor;
using render::withAlpha;
using render::potentialColor;
using render::currentColor;
using i18n::tr;

constexpr double kPi = 3.14159265358979323846;

double potentialFor(const CircuitSolution* solution, int nodeId) {
    if (!solution) return 0.0;
    for (const auto& np : solution->nodePotentials) {
        if (np.nodeId == nodeId) return np.potential;
    }
    return 0.0;
}

const BranchResult* branchFor(const CircuitSolution* solution, int componentId) {
    if (!solution) return nullptr;
    for (const auto& br : solution->branches) {
        if (br.componentId == componentId) return &br;
    }
    return nullptr;
}

struct FieldSource {
    Vec2 position;
    double strength = 0.0;
};

Vec2 qualitativeFieldAt(Vec2 p, const std::vector<FieldSource>& sources) {
    Vec2 e;
    for (const auto& source : sources) {
        Vec2 r = p - source.position;
        double r2 = r.x * r.x + r.y * r.y + 450.0;
        double inv = 1.0 / (r2 * std::sqrt(r2));
        e = e + r * (source.strength * inv);
    }
    return e;
}

uint32_t fieldGlowColor(double strength, unsigned alpha) {
    if (strength >= 0.0) return packColor(255, 188, 55, alpha);
    return packColor(72, 166, 255, alpha);
}

struct BuildContext {
    const Circuit& circuit;
    const CircuitSolution* solution;
    const ViewParams& p;
    render::RenderPrimitives& out;

    double vMin = 0.0, vMax = 0.0;
    double maxI = 0.0, maxE = 0.0, maxP = 0.0;

    double safeScale() const { return std::max(0.05, p.cameraScale); }
    bool hasPotentialRange() const { return solution && std::abs(vMax - vMin) > 1e-12; }
};

void computeRanges(BuildContext& ctx) {
    if (!ctx.solution) return;
    const auto& solution = *ctx.solution;

    if (!solution.nodePotentials.empty()) {
        ctx.vMin = ctx.vMax = solution.nodePotentials.front().potential;
        for (const auto& np : solution.nodePotentials) {
            ctx.vMin = std::min(ctx.vMin, np.potential);
            ctx.vMax = std::max(ctx.vMax, np.potential);
        }
    }

    for (const auto& br : solution.branches) {
        ctx.maxI = std::max(ctx.maxI, std::abs(br.current));
        const Component* c = ctx.circuit.findComponent(br.componentId);
        if (!c || c->type == ComponentType::Ground) continue;

        ctx.maxP = std::max(ctx.maxP, physics::dissipatedPowerOnly(c->type, br.power));

        const Node* nodeA = ctx.circuit.findNode(c->nodeA);
        const Node* nodeB = ctx.circuit.findNode(c->nodeB);
        if (!nodeA || !nodeB) continue;
        double eMagnitude;
        if (c->type == ComponentType::Resistor) {
            eMagnitude = physics::resistorBodyElectricFieldMagnitude(
                nodeA->position, nodeB->position,
                potentialFor(ctx.solution, c->nodeA),
                potentialFor(ctx.solution, c->nodeB),
                ctx.p.wireThickness);
        } else if (c->type == ComponentType::Capacitor) {
            auto geom = capacitorGeometry(nodeA->position, nodeB->position, ctx.p.wireThickness);
            eMagnitude = geom.valid
                ? physics::electricFieldMagnitude(br.voltageDrop, geom.gap)
                : 0.0;
        } else {
            eMagnitude = physics::electricFieldMagnitude(
                br.voltageDrop, (nodeB->position - nodeA->position).length());
        }
        ctx.maxE = std::max(ctx.maxE, eMagnitude);
    }
}

// --- shared shape emitters -------------------------------------------------

// Straight conductor body: base quad, potential gradient (or idle core), outline, round caps.
void emitConductor(BuildContext& ctx, Vec2 a, Vec2 b, double va, double vb,
                   double halfWidth, bool isResistiveBody = false) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 0.5) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    Vec2 c1 = a + perp * halfWidth;
    Vec2 c2 = a - perp * halfWidth;
    Vec2 c3 = b - perp * halfWidth;
    Vec2 c4 = b + perp * halfWidth;

    uint32_t baseCol = isResistiveBody ? packColor(66, 64, 60) : packColor(38, 42, 50);
    uint32_t outlineCol = isResistiveBody ? packColor(226, 226, 216, 235) : packColor(120, 180, 120);

    ctx.out.quads.push_back({c1, c2, c3, c4, baseCol, true, 0.0});

    double screenW = halfWidth * 2.0 * ctx.p.cameraScale;
    bool hasGradient = ctx.p.layers.potential && ctx.hasPotentialRange() && screenW > 1.0;
    if (hasGradient) {
        render::PrimGradient grad;
        grad.a = a; grad.b = b;
        grad.width = halfWidth * 2.0;
        grad.vA = va; grad.vB = vb;
        grad.vMin = ctx.vMin; grad.vMax = ctx.vMax;
        grad.alpha = isResistiveBody ? 225 : 255;
        ctx.out.gradients.push_back(grad);
    } else if (!isResistiveBody && screenW > 1.0) {
        double coreHW = halfWidth * 0.6;
        ctx.out.quads.push_back({a + perp * coreHW, a - perp * coreHW,
                                 b - perp * coreHW, b + perp * coreHW,
                                 packColor(130, 200, 130), true, 0.0});
    }

    ctx.out.quads.push_back({c1, c2, c3, c4, outlineCol, false, isResistiveBody ? 2.1 : 1.5});
    if (!isResistiveBody) {
        ctx.out.circles.push_back({a, halfWidth, outlineCol, 1.5, false, false});
        ctx.out.circles.push_back({b, halfWidth, outlineCol, 1.5, false, false});
    }
}

void emitWire(BuildContext& ctx, Vec2 a, Vec2 b, double va, double vb) {
    emitConductor(ctx, a, b, va, vb, ctx.p.wireThickness * 0.5);
}

void emitResistor(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                  double va, double vb, double branchPower) {
    Vec2 dir = b - a;
    double len = dir.length();
    if (len < 1.0) return;
    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);

    auto sections = physics::resistorPathSections(a, b, va, vb, ctx.p.wireThickness);
    if (sections.empty()) return;

    const auto* body = &sections.front();
    for (const auto& section : sections) {
        if (section.material == physics::VisualMaterial::ResistiveBody) { body = &section; break; }
    }

    // Heat underline behind the body, width grows with dissipated power fraction.
    double frac = physics::heatFraction(ComponentType::Resistor, branchPower, ctx.maxP);
    if (ctx.p.layers.heat && frac > 1e-12 && body->length() > 0.5) {
        double heatW = body->halfWidth * 2.0 + (4.0 + 9.0 * frac) / ctx.safeScale();
        uint32_t heatCol = packColor(
            static_cast<unsigned>(180 + 75 * frac),
            static_cast<unsigned>(100 - 40 * frac),
            static_cast<unsigned>(50 - 30 * frac),
            static_cast<unsigned>(65 + 95 * frac));
        ctx.out.lines.push_back({body->start, body->end, heatW, heatCol, false});
    }

    for (const auto& section : sections) {
        emitConductor(ctx, section.start, section.end, section.voltageStart, section.voltageEnd,
                      section.halfWidth, section.material == physics::VisualMaterial::ResistiveBody);
    }

    // Body end ticks.
    double rectH = body->halfWidth;
    Vec2 rectLeft = body->start;
    Vec2 rectRight = body->end;
    ctx.out.lines.push_back({rectLeft + perp * rectH, rectLeft - perp * rectH, 2.0,
                             packColor(245, 245, 238, 240), true});
    ctx.out.lines.push_back({rectRight + perp * rectH, rectRight - perp * rectH, 2.0,
                             packColor(245, 245, 238, 240), true});

    if (ctx.p.layers.potential && ctx.hasPotentialRange()) {
        for (int i = 1; i < 5; ++i) {
            double t = static_cast<double>(i) / 5.0;
            Vec2 p0 = rectLeft + (rectRight - rectLeft) * t;
            uint32_t lineCol = withAlpha(potentialColor(va + (vb - va) * t, ctx.vMin, ctx.vMax), 105);
            ctx.out.lines.push_back({p0 + perp * rectH * 0.92, p0 - perp * rectH * 0.92,
                                     1.0, lineCol, true});
        }
    }

    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    Vec2 lbl = mid + perp * (rectH + 8.0 / ctx.safeScale());
    char buf[32];
    if (comp.value >= 1000.0)
        std::snprintf(buf, sizeof(buf), "%.1f %s", comp.value / 1000.0, tr("k\xCE\xA9"));
    else
        std::snprintf(buf, sizeof(buf), "%.0f %s", comp.value, tr("\xCE\xA9"));
    ctx.out.labels.push_back({lbl, buf, packColor(200, 200, 200), false});
}

void emitVoltageSource(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                       double va, double vb) {
    Vec2 dir = b - a;
    double len = dir.length();
    if (len < 1.0) return;
    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);

    double r = 15.0;
    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    Vec2 leadA = mid - unit * r;
    Vec2 leadB = mid + unit * r;

    double tA = len > 2.0 * r ? (len * 0.5 - r) / len : 0.5;
    double tB = len > 2.0 * r ? (len * 0.5 + r) / len : 0.5;
    emitConductor(ctx, a, leadA, va, va + (vb - va) * tA, ctx.p.wireThickness * 0.5);
    emitConductor(ctx, leadB, b, va + (vb - va) * tB, vb, ctx.p.wireThickness * 0.5);

    ctx.out.circles.push_back({mid, r, packColor(255, 255, 255), 2.5, false, false});

    // "+" near the positive terminal (node A side), "-" near the negative
    // one; kept apart so the glyphs never overlap.
    double s = r * 0.28;
    Vec2 plusCenter = mid - unit * (r * 0.45);
    Vec2 minusCenter = mid + unit * (r * 0.45);
    ctx.out.lines.push_back({plusCenter - unit * s, plusCenter + unit * s, 2.0,
                             packColor(255, 100, 100), true});
    ctx.out.lines.push_back({plusCenter - perp * s, plusCenter + perp * s, 2.0,
                             packColor(255, 100, 100), true});
    ctx.out.lines.push_back({minusCenter - perp * s, minusCenter + perp * s, 2.0,
                             packColor(100, 100, 255), true});

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", comp.value, tr("V"));
    ctx.out.labels.push_back({mid + perp * 22.0, buf, packColor(200, 200, 200), false});
}

void emitGround(BuildContext& ctx, Vec2 pos) {
    // Constant screen-size symbol: convert pixel offsets to world units.
    double s = 1.0 / ctx.safeScale();
    auto P = [&](double dx, double dy) { return pos + Vec2(dx * s, dy * s); };
    uint32_t col = packColor(255, 255, 255);
    ctx.out.lines.push_back({P(0, 0), P(0, 10), 2.0, col, true});
    ctx.out.lines.push_back({P(-12, 10), P(12, 10), 2.0, col, true});
    ctx.out.lines.push_back({P(-8, 16), P(8, 16), 2.0, col, true});
    ctx.out.lines.push_back({P(-5, 22), P(5, 22), 2.0, col, true});
}

void formatCapacitance(double farads, char* buf, size_t n) {
    if (farads >= 1.0) std::snprintf(buf, n, "%.2f %s", farads, tr("F"));
    else if (farads >= 1e-3) std::snprintf(buf, n, "%.1f %s", farads * 1e3, tr("mF"));
    else if (farads >= 1e-6) std::snprintf(buf, n, "%.1f %s", farads * 1e6, tr("\xC2\xB5F"));
    else std::snprintf(buf, n, "%.1f %s", farads * 1e9, tr("nF"));
}

void formatInductance(double henries, char* buf, size_t n) {
    if (henries >= 1.0) std::snprintf(buf, n, "%.2f %s", henries, tr("H"));
    else if (henries >= 1e-3) std::snprintf(buf, n, "%.1f %s", henries * 1e3, tr("mH"));
    else std::snprintf(buf, n, "%.1f %s", henries * 1e6, tr("\xC2\xB5H"));
}

void emitCapacitorSymbol(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                         double va, double vb) {
    auto g = capacitorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;

    double halfW = ctx.p.wireThickness * 0.5;
    emitConductor(ctx, a, g.leadAEnd, va, va, halfW);
    emitConductor(ctx, g.leadBEnd, b, vb, vb, halfW);

    bool colorByPotential = ctx.p.layers.potential && ctx.hasPotentialRange();
    uint32_t colA = colorByPotential ? withAlpha(potentialColor(va, ctx.vMin, ctx.vMax), 255)
                                     : packColor(226, 226, 216, 235);
    uint32_t colB = colorByPotential ? withAlpha(potentialColor(vb, ctx.vMin, ctx.vMax), 255)
                                     : packColor(226, 226, 216, 235);
    ctx.out.lines.push_back({g.plateATop, g.plateABottom, 3.0, colA, true});
    ctx.out.lines.push_back({g.plateBTop, g.plateBBottom, 3.0, colB, true});

    char buf[32];
    formatCapacitance(comp.value, buf, sizeof(buf));
    ctx.out.labels.push_back({g.mid + g.perp * (g.plateHalf + 8.0 / ctx.safeScale()), buf,
                              packColor(200, 200, 200), false});
}

// Physics view of the capacitor: charge on the plates, E-field in the gap,
// stored-energy glow. The gap carries no conduction current, so the drift /
// current layers stay on the leads.
void emitCurrentArrows(BuildContext& ctx, Vec2 a, Vec2 b, double current,
                       double conductorHalfWidth);
void emitDriftParticles(BuildContext& ctx, Vec2 a, Vec2 b, double current, int compId,
                        double visualThickness, double driftSpeedScale);

void emitCapacitorPhysics(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                          double va, double vb, double branchCurrent) {
    auto g = capacitorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;

    if (ctx.p.layers.current && std::abs(branchCurrent) > 1e-12) {
        emitCurrentArrows(ctx, a, g.leadAEnd, branchCurrent, -1.0);
        emitCurrentArrows(ctx, g.leadBEnd, b, branchCurrent, -1.0);
    }
    // Sampling fallback only: the Box2D world has no capacitor channel
    // (charges cannot cross the gap), so with simParticles active there is
    // nothing to draw — and the pseudo-ids must not reach the sim filter,
    // where a collision with a real component id would draw alien particles.
    if (!ctx.p.simParticles && ctx.p.layers.drift && std::abs(branchCurrent) > 1e-12) {
        emitDriftParticles(ctx, a, g.leadAEnd, branchCurrent, comp.id * 31 + 1, -1.0, 1.0);
        emitDriftParticles(ctx, g.leadBEnd, b, branchCurrent, comp.id * 31 + 2, -1.0, 1.0);
    }

    double dV = va - vb;
    double vRange = std::max(std::abs(ctx.vMax - ctx.vMin), 1e-9);
    double charge = std::clamp(std::abs(dV) / vRange, 0.0, 1.0);

    if (ctx.p.layers.electricField && std::abs(dV) > 1e-9 && ctx.maxE > 1e-12) {
        Vec2 fieldDir = dV > 0.0 ? g.unit : g.unit * -1.0;
        double eMag = physics::electricFieldMagnitude(dV, g.gap);
        double frac = std::min(1.0, eMag / ctx.maxE);
        double arrowSize = (4.0 + frac * 5.0) / ctx.safeScale();
        uint32_t color = packColor(
            static_cast<unsigned>(60 + 155 * frac),
            static_cast<unsigned>(140 + 115 * frac),
            static_cast<unsigned>(60 + 60 * frac), 200);
        for (int row = -2; row <= 2; ++row) {
            Vec2 pos = g.mid + g.perp * (g.plateHalf * 0.35 * row);
            ctx.out.arrows.push_back({pos, fieldDir, arrowSize, color});
        }
    }

    if (ctx.p.layers.surfaceCharge && charge > 0.02) {
        int dots = std::max(3, static_cast<int>(g.plateHalf / 6.0));
        bool plateAPositive = dV > 0.0;
        int alpha = std::min(static_cast<int>(140 * charge + 60), 230);
        uint32_t plus = packColor(255, 90, 70, alpha);
        uint32_t minus = packColor(70, 90, 255, alpha);
        double dotR = std::max(1.5, 2.5 * charge + 1.0);
        for (int i = 0; i < dots; ++i) {
            double t = dots > 1 ? -1.0 + 2.0 * i / (dots - 1) : 0.0;
            Vec2 offset = g.perp * (g.plateHalf * 0.85 * t);
            ctx.out.particles.push_back({g.leadAEnd + offset - g.unit * (2.0 / ctx.safeScale()),
                                         dotR, plateAPositive ? plus : minus, true});
            ctx.out.particles.push_back({g.leadBEnd + offset + g.unit * (2.0 / ctx.safeScale()),
                                         dotR, plateAPositive ? minus : plus, true});
        }
    }

    if (ctx.p.layers.electricField && charge > 0.02) {
        ctx.out.glows.push_back({g.mid, g.plateHalf * 1.3, charge,
                                 packColor(120, 200, 255, static_cast<unsigned>(10 + 26 * charge))});
    }
}

void emitInductorSymbol(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                        double va, double vb) {
    auto g = inductorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;

    double halfW = ctx.p.wireThickness * 0.5;
    emitConductor(ctx, a, g.coilStart, va, va, halfW);
    emitConductor(ctx, g.coilEnd, b, vb, vb, halfW);
    // Slim core under the coil carrying the (transient) voltage gradient.
    emitConductor(ctx, g.coilStart, g.coilEnd, va, vb, halfW * 0.55);

    for (int i = 0; i < g.bumps; ++i) {
        auto arc = inductorBumpArc(g, i);
        if (arc.empty()) continue;
        render::PrimPolyline bump{std::move(arc), 2.5, packColor(214, 214, 224, 235), true};
        ctx.out.polylines.push_back(std::move(bump));
    }

    char buf[32];
    formatInductance(comp.value, buf, sizeof(buf));
    ctx.out.labels.push_back({g.coilStart + g.perp * (g.bumpRadius + 10.0 / ctx.safeScale()) +
                                  g.unit * (g.coilEnd - g.coilStart).length() * 0.5,
                              buf, packColor(200, 200, 200), false});
}

// Stored magnetic energy glow; ring density already follows |I| via the
// shared magnetic layer.
void emitInductorPhysics(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                         double branchCurrent) {
    (void)comp;
    auto g = inductorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;

    double iFrac = ctx.maxI > 1e-12 ? std::clamp(std::abs(branchCurrent) / ctx.maxI, 0.0, 1.0) : 0.0;
    if (ctx.p.layers.magnetic && iFrac > 0.02) {
        Vec2 mid = (g.coilStart + g.coilEnd) * 0.5;
        double radius = (g.coilEnd - g.coilStart).length() * 0.7;
        ctx.out.glows.push_back({mid, radius, iFrac,
                                 packColor(168, 130, 255, static_cast<unsigned>(8 + 30 * iFrac))});
    }
}

void emitDiodeSymbol(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                     double va, double vb) {
    (void)comp;
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);
    double s = std::clamp(ctx.p.wireThickness * 1.5, 10.0, len * 0.4);
    Vec2 mid = a + ab * 0.5;
    Vec2 leadAEnd = mid - unit * (s * 0.5);
    Vec2 leadBEnd = mid + unit * (s * 0.5);

    double halfW = ctx.p.wireThickness * 0.5;
    emitConductor(ctx, a, leadAEnd, va, va, halfW);
    emitConductor(ctx, leadBEnd, b, vb, vb, halfW);

    // Triangle pointing in the conduction direction (A -> B), plus the bar.
    ctx.out.arrows.push_back({leadAEnd, unit, s, packColor(235, 235, 230, 240)});
    ctx.out.lines.push_back({leadBEnd + perp * (s * 0.55), leadBEnd - perp * (s * 0.55),
                             3.0, packColor(235, 235, 230, 240), true});
}

void emitSwitchSymbol(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                      double va, double vb) {
    // Общая геометрия с hit-test'ом хот-зоны (hitTestSwitchToggle): глиф и
    // кликабельная область обязаны совпадать.
    auto g = switchGeometry(a, b);
    if (!g.valid) return;
    Vec2 unit = g.unit;
    Vec2 perp = g.perp;
    double s = g.s;
    Vec2 mid = g.mid;
    Vec2 leadAEnd = g.leadAEnd;
    Vec2 leadBEnd = g.leadBEnd;

    double halfW = ctx.p.wireThickness * 0.5;
    emitConductor(ctx, a, leadAEnd, va, va, halfW);
    emitConductor(ctx, leadBEnd, b, vb, vb, halfW);

    bool closed = comp.value >= 0.5;
    uint32_t col = packColor(235, 235, 230, 240);
    ctx.out.circles.push_back({leadAEnd, 3.0, col, 1.6, false, true});
    ctx.out.circles.push_back({leadBEnd, 3.0, col, 1.6, false, true});

    Vec2 leverEnd = closed ? leadBEnd : leadAEnd + (unit * 0.8 + perp * 0.6) * s;
    ctx.out.lines.push_back({leadAEnd, leverEnd, 2.5, col, true});

    ctx.out.labels.push_back({mid + perp * (s * 0.8 + 6.0 / ctx.safeScale()),
                              closed ? tr("closed") : tr("open"),
                              packColor(180, 185, 190, 220), false});
}

void emitSelectionHighlight(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b) {
    Vec2 dir = b - a;
    double len = dir.length();
    if (len <= 1.0) return;
    Vec2 unit = dir / len;
    Vec2 perp(-unit.y, unit.x);
    double halfWidth = comp.type == ComponentType::Resistor
        ? physics::resistorBodyHalfWidth(ctx.p.wireThickness) + 6.0 / ctx.safeScale()
        : ctx.p.wireThickness * 0.5 + 8.0 / ctx.safeScale();
    Vec2 q1 = a + perp * halfWidth;
    Vec2 q2 = a - perp * halfWidth;
    Vec2 q3 = b - perp * halfWidth;
    Vec2 q4 = b + perp * halfWidth;
    ctx.out.quads.push_back({q1, q2, q3, q4, packColor(255, 210, 90, 20), true, 0.0});
    ctx.out.quads.push_back({q1, q2, q3, q4, packColor(255, 215, 110, 220), false, 2.0});
    ctx.out.lines.push_back({a, b, 7.0, packColor(255, 220, 120, 135), true});
}

// --- physics layers ----------------------------------------------------------

void emitCurrentArrows(BuildContext& ctx, Vec2 a, Vec2 b, double current,
                       double conductorHalfWidth = -1.0) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;

    double absI = std::abs(current);
    double scale = ctx.p.cameraScale;

    double arrowSpacing = std::clamp(40.0 / std::max(0.05, scale), 8.0, 80.0);

    double arrowSize = 6.0;
    if (ctx.maxI > 1e-12) arrowSize = 5.0 + (absI / ctx.maxI) * 5.0;
    arrowSize /= ctx.safeScale();

    Vec2 flowDir = unit;
    if (current < 0.0) flowDir = flowDir * -1.0;
    if (ctx.p.layers.electronFlow) flowDir = flowDir * -1.0;

    uint32_t color = currentColor(absI, ctx.maxI);

    double speed = std::max(absI * 20.0, 5.0);
    double phase = std::fmod(ctx.p.time * speed, arrowSpacing);
    // The march must go where the glyphs point: the current sign and the
    // electron-flow toggle flip flowDir, so the animation phase flips too
    // (regression: source arrows pointed up the branch but marched down).
    if (flowDir.x * unit.x + flowDir.y * unit.y < 0.0) phase = -phase;

    Vec2 perp(-unit.y, unit.x);
    double halfWidth = conductorHalfWidth > 0.0 ? conductorHalfWidth : 0.0;
    double screenWidth = halfWidth * 2.0 * scale;
    int rows = 1;
    if (screenWidth > 26.0)
        rows = std::clamp(static_cast<int>(screenWidth / 24.0), 1, 4);

    int count = static_cast<int>((len - arrowSpacing * 0.5) / arrowSpacing) + 1;
    for (int row = 0; row < rows; ++row) {
        double rowOffset = 0.0;
        if (rows > 1)
            rowOffset = -halfWidth * 0.68 + halfWidth * 1.36 * static_cast<double>(row) /
                        static_cast<double>(rows - 1);
        for (int i = 0; i < count; ++i) {
            double t = arrowSpacing * 0.5 + i * arrowSpacing + phase;
            if (t > len) t -= len;
            if (t < 0.0) t += len;
            Vec2 p(a.x + unit.x * t, a.y + unit.y * t);
            ctx.out.arrows.push_back({p + perp * rowOffset, flowDir, arrowSize, color});
        }
    }
}

void emitEFieldArrows(BuildContext& ctx, Vec2 a, Vec2 b, double va, double vb,
                      double conductorHalfWidth = -1.0) {
    physics::FieldSamplingConfig config;
    config.cameraScale = ctx.p.cameraScale;
    config.wireHalfWidth = conductorHalfWidth > 0.0 ? conductorHalfWidth : ctx.p.wireThickness * 0.5;
    config.maxMagnitude = ctx.maxE;

    auto samples = physics::sampleFieldArrows(a, b, va, vb, config);
    if (samples.empty() || ctx.maxE < 1e-12) return;

    double frac = std::min(1.0, samples.front().magnitude / ctx.maxE);
    double arrowSize = (4.0 + frac * 5.0) / ctx.safeScale();
    uint32_t color = packColor(
        static_cast<unsigned>(60 + 155 * frac),
        static_cast<unsigned>(140 + 115 * frac),
        static_cast<unsigned>(60 + 60 * frac), 200);

    for (const auto& sample : samples)
        ctx.out.arrows.push_back({sample.position, sample.direction, arrowSize, color});
}

void emitDriftParticles(BuildContext& ctx, Vec2 a, Vec2 b, double current, int compId,
                        double visualThickness = -1.0, double driftSpeedScale = 1.0) {
    physics::DriftSamplingConfig config;
    config.wireThickness = visualThickness > 0.0 ? visualThickness : ctx.p.wireThickness;
    config.cameraScale = ctx.p.cameraScale;
    config.time = ctx.p.time;
    config.visualSpeedMultiplier *= std::clamp(driftSpeedScale, 0.05, 4.0);
    config.componentId = compId;
    config.electronFlowVisualization = ctx.p.layers.electronFlow;

    double radius = physics::particleWorldRadius(config.wireThickness);
    uint32_t color = ctx.p.layers.electronFlow ? packColor(245, 182, 87, 170)
                                               : packColor(120, 180, 255, 170);

    if (ctx.p.simParticles) {
        // Box2D microdynamics: real elastic particles from the shared sim.
        for (const auto& sp : *ctx.p.simParticles)
            if (sp.componentId == compId)
                ctx.out.particles.push_back({sp.pos, radius, color, false});
        return;
    }

    auto particles = physics::sampleDriftParticles(a, b, current, config);
    for (const auto& particle : particles)
        ctx.out.particles.push_back({particle.position, radius, color, false});
}

void emitSurfaceCharge(BuildContext& ctx, Vec2 a, Vec2 b, double va, double vb,
                       double visualThickness = -1.0) {
    physics::SurfaceChargeSamplingConfig config;
    config.wireThickness = visualThickness > 0.0 ? visualThickness : ctx.p.wireThickness;
    config.cameraScale = ctx.p.cameraScale;

    auto samples = physics::sampleSurfaceCharges(a, b, va, vb, ctx.vMin, ctx.vMax, config);
    if (samples.empty()) return;

    double screenW = ctx.p.wireThickness * ctx.p.cameraScale;
    for (const auto& sample : samples) {
        double dotR = std::max(1.2, screenW * 0.07 * sample.displayStrength);
        dotR = std::min(dotR, screenW * 0.13);

        bool positive = sample.signedStrength > 0.0;
        int alpha = std::min(static_cast<int>(140 * sample.displayStrength + 40), 230);

        uint32_t col = positive
            ? packColor(255, static_cast<unsigned>(100 - 60 * sample.displayStrength),
                        static_cast<unsigned>(80 - 50 * sample.displayStrength), alpha)
            : packColor(static_cast<unsigned>(80 - 50 * sample.displayStrength),
                        static_cast<unsigned>(100 - 60 * sample.displayStrength), 255, alpha);

        ctx.out.particles.push_back({sample.topPosition, dotR, col, true});
        ctx.out.particles.push_back({sample.bottomPosition, dotR, col, true});
    }
}

void emitMagneticField(BuildContext& ctx, Vec2 a, Vec2 b, double current) {
    physics::MagneticFieldSamplingConfig config;
    config.wireThickness = ctx.p.wireThickness;
    config.cameraScale = ctx.p.cameraScale;

    auto samples = physics::sampleMagneticField(a, b, current, config);
    if (samples.empty()) return;

    double maxMagnitude = 0.0;
    for (const auto& sample : samples)
        maxMagnitude = std::max(maxMagnitude, sample.magnitude);

    double s = 1.0 / ctx.safeScale();
    for (const auto& sample : samples) {
        double frac = maxMagnitude > 1e-18 ? sample.magnitude / maxMagnitude : 0.0;
        double glyphR = std::max(3.5, 3.0 + frac * 4.5); // screen px
        uint32_t col = packColor(
            static_cast<unsigned>(80 + 120 * frac),
            static_cast<unsigned>(160 + 50 * frac),
            static_cast<unsigned>(210 + 35 * frac),
            static_cast<unsigned>(110 + 90 * frac));

        ctx.out.circles.push_back({sample.position, glyphR, col, 1.4, false, true});

        if (sample.direction == physics::PageDirection::OutOfPage) {
            ctx.out.circles.push_back({sample.position, std::max(1.2, glyphR * 0.28), col, 0.0, true, true});
        } else {
            double arm = glyphR * 0.42 * s; // world units for constant screen size
            ctx.out.lines.push_back({sample.position + Vec2(-arm, -arm), sample.position + Vec2(arm, arm),
                                     1.5, col, true});
            ctx.out.lines.push_back({sample.position + Vec2(-arm, arm), sample.position + Vec2(arm, -arm),
                                     1.5, col, true});
        }
    }
}

void emitFieldBackdrop(BuildContext& ctx) {
    if (!ctx.hasPotentialRange()) return;

    double range = ctx.vMax - ctx.vMin;
    double vMid = (ctx.vMin + ctx.vMax) * 0.5;

    std::vector<FieldSource> sources;
    sources.reserve(ctx.circuit.nodes.size());
    for (const auto& node : ctx.circuit.nodes) {
        double q = (potentialFor(ctx.solution, node.id) - vMid) / range * 2.0;
        if (std::abs(q) > 0.05)
            sources.push_back({node.position, q});
    }

    // Soft potential aura along conductors.
    if (ctx.p.layers.potential) {
        for (const auto& comp : ctx.circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            const Node* nodeA = ctx.circuit.findNode(comp.nodeA);
            const Node* nodeB = ctx.circuit.findNode(comp.nodeB);
            if (!nodeA || !nodeB) continue;
            double len = (nodeB->position - nodeA->position).length();
            if (len < 1.0) continue;

            double va = potentialFor(ctx.solution, comp.nodeA);
            double vb = potentialFor(ctx.solution, comp.nodeB);
            render::PrimGradient wide;
            wide.a = nodeA->position; wide.b = nodeB->position;
            wide.vA = va; wide.vB = vb;
            wide.vMin = ctx.vMin; wide.vMax = ctx.vMax;
            wide.width = ctx.p.wireThickness * 6.0;
            wide.alpha = 18;
            ctx.out.gradients.push_back(wide);
            wide.width = ctx.p.wireThickness * 3.0;
            wide.alpha = 28;
            ctx.out.gradients.push_back(wide);
        }
    }

    for (const auto& source : sources) {
        double intensity = std::min(1.0, std::abs(source.strength));
        double radiusPx = std::clamp((52.0 + 48.0 * intensity) * ctx.p.cameraScale, 18.0, 220.0);
        double radius = radiusPx / ctx.safeScale();
        ctx.out.glows.push_back({source.position, radius, intensity,
                                 fieldGlowColor(source.strength, 16)});
    }

    if (!ctx.p.layers.electricField || sources.empty()) return;

    Vec2 worldMin = ctx.p.viewMin;
    Vec2 worldMax = ctx.p.viewMax;
    double margin = 180.0 / ctx.safeScale();
    worldMin = worldMin - Vec2(margin, margin);
    worldMax = worldMax + Vec2(margin, margin);

    bool hasPositive = false;
    for (const auto& source : sources)
        hasPositive = hasPositive || source.strength > 0.05;

    for (const auto& source : sources) {
        if (hasPositive && source.strength <= 0.05) continue;

        double traceSign = source.strength >= 0.0 ? 1.0 : -1.0;
        int seedCount = std::clamp(10 + static_cast<int>(std::abs(source.strength) * 8.0), 10, 18);
        double seedRadius = ctx.p.wireThickness * 2.0 + 12.0 / ctx.safeScale();
        double stepLen = std::clamp(10.0 / ctx.safeScale(), 2.5, 18.0);

        for (int seed = 0; seed < seedCount; ++seed) {
            double angle = (static_cast<double>(seed) + 0.5) / seedCount * kPi * 2.0;
            Vec2 p = source.position + Vec2(std::cos(angle), std::sin(angle)) * seedRadius;
            std::vector<Vec2> pts;
            pts.reserve(96);

            for (int step = 0; step < 84; ++step) {
                if (p.x < worldMin.x || p.x > worldMax.x || p.y < worldMin.y || p.y > worldMax.y)
                    break;
                pts.push_back(p);

                Vec2 e = qualitativeFieldAt(p, sources);
                double eLen = e.length();
                if (eLen < 1e-9) break;

                Vec2 dir = (e / eLen) * traceSign;
                p = p + dir * stepLen;

                bool reachedSink = false;
                for (const auto& other : sources) {
                    if (&other == &source) continue;
                    if ((p - other.position).length() < seedRadius * 0.75) { reachedSink = true; break; }
                }
                if (reachedSink) break;
            }

            if (pts.size() < 5) continue;

            render::PrimPolyline haloLine{pts, 4.0, fieldGlowColor(source.strength, 18), true};
            render::PrimPolyline coreLine{pts, 1.25, fieldGlowColor(source.strength, 82), true};
            ctx.out.polylines.push_back(std::move(haloLine));

            if (pts.size() > 12 && seed % 2 == 0) {
                size_t idx = pts.size() / 2;
                Vec2 dir = (pts[idx + 1] - pts[idx - 1]).normalized();
                ctx.out.arrows.push_back({pts[idx], dir, 5.0 / ctx.safeScale(),
                                          fieldGlowColor(source.strength, 120)});
            }
            ctx.out.polylines.push_back(std::move(coreLine));
        }
    }
}

void emitJunctions(BuildContext& ctx) {
    double radius = ctx.p.wireThickness * 0.5;
    if (radius * ctx.p.cameraScale <= 1.0) return;

    bool potentialFill = ctx.p.layers.potential && ctx.hasPotentialRange();

    for (const auto& node : ctx.circuit.nodes) {
        int connected = 0;
        for (const auto& comp : ctx.circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            if (comp.nodeA == node.id || comp.nodeB == node.id) ++connected;
        }
        if (connected < 2) continue;

        if (potentialFill) {
            uint32_t fill = withAlpha(potentialColor(potentialFor(ctx.solution, node.id),
                                                     ctx.vMin, ctx.vMax), 255);
            ctx.out.circles.push_back({node.position, radius, fill, 0.0, true, false});
        } else {
            ctx.out.circles.push_back({node.position, radius, packColor(38, 42, 50), 0.0, true, false});
            ctx.out.circles.push_back({node.position, radius * 0.6, packColor(130, 200, 130), 0.0, true, false});
        }
    }
}

void emitNodes(BuildContext& ctx) {
    for (const auto& node : ctx.circuit.nodes) {
        bool selected = (node.id == ctx.p.selectedNode);
        if (!ctx.p.debugView && !selected) continue;

        uint32_t fill = selected ? packColor(255, 200, 50) : packColor(200, 200, 200, 190);
        double radius = selected ? 6.0 : 4.0; // screen px
        ctx.out.circles.push_back({node.position, radius, fill, 0.0, true, true});
        ctx.out.circles.push_back({node.position, radius + 1.0,
                                   selected ? packColor(255, 255, 255, 230) : packColor(255, 255, 255, 150),
                                   1.3, false, true});

        double s = 1.0 / ctx.safeScale();
        if (ctx.p.debugView && !node.label.empty())
            ctx.out.labels.push_back({node.position + Vec2(10 * s, -18 * s), node.label,
                                      packColor(200, 200, 200, 210), true});

        if (ctx.solution && (ctx.p.debugView || (selected && ctx.p.layers.canvasReadouts))) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.3f %s", potentialFor(ctx.solution, node.id), tr("V"));
            ctx.out.labels.push_back({node.position + Vec2(10 * s, 2 * s), buf,
                                      packColor(130, 205, 255, 230), false});
        }
    }
}

void emitReadoutLabels(BuildContext& ctx, const Component& comp, Vec2 mid, Vec2 perp,
                       double current, double power) {
    if (!(ctx.p.debugView || (ctx.p.layers.canvasReadouts && comp.id == ctx.p.selectedComponent)))
        return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "I=%.2f %s", current * 1000.0, tr("mA"));
    ctx.out.labels.push_back({mid + perp * 18.0, buf, packColor(255, 220, 50, 235), false});
    if (ctx.p.layers.power) {
        std::snprintf(buf, sizeof(buf), "P=%.2f %s", power * 1000.0, tr("mW"));
        ctx.out.labels.push_back({mid + perp * 30.0, buf, packColor(255, 160, 80, 235), false});
    }
}

// --- mechanics (mechanical analogy) -----------------------------------------
// Magnitudes and signs come from MechanicsMapping (exact images of solver
// values); only the on-screen motion speed is scaled by kVisual* constants.

using mechanics::kVisualChainSpeed;
using mechanics::kVisualSpinRate;

// Continuous wheel phase: prefer the integral of current (smooth when I
// changes every frame, e.g. while cranking); fall back to t*omega.
double spinPhase(const BuildContext& ctx, int componentId, double current, double rate) {
    if (ctx.p.flowIntegrals)
        return componentIntegral(ctx.p.flowIntegrals, componentId) * rate;
    return ctx.p.time * current * rate;
}

double chainHalfWidth(const BuildContext& ctx) {
    return physics::chain_geometry::chainHalfWidth(ctx.p.wireThickness);
}

std::vector<Vec2> sampleClockwiseArc(Vec2 center, double radius,
                                     Vec2 from, Vec2 to, int steps = 16) {
    namespace cg = physics::chain_geometry;
    double start = cg::angleOf(from - center);
    double delta = cg::clockwiseDelta(start, cg::angleOf(to - center));
    std::vector<Vec2> pts;
    pts.reserve(static_cast<size_t>(steps + 1));
    for (int i = 0; i <= steps; ++i) {
        double s = delta * static_cast<double>(i) / steps;
        pts.push_back(cg::circlePoint(center, radius, start - s));
    }
    return pts;
}

// toothPhase rotates the teeth (meshed to the chain rollers); bodyPhase rotates
// the rigid wheel decoration (hub lightening holes). They share an average rate
// but bodyPhase is smooth while toothPhase snaps to the gaps — drawing the holes
// off the smooth phase stops them jittering on the sub-tooth mesh correction.
void emitSprocket(BuildContext& ctx, Vec2 center, double pitchR,
                  double toothPhase, double bodyPhase, bool drive) {
    namespace cg = physics::chain_geometry;
    const double rollerR = cg::linkRadius(ctx.p.wireThickness);
    const double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    const double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    const int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));

    const uint32_t bodyFill = drive ? packColor(70, 58, 46, 255)
                                    : packColor(58, 64, 76, 255);
    const uint32_t edgeCol = drive ? packColor(236, 178, 96, 235)
                                   : packColor(170, 178, 190, 230);
    const uint32_t toothFill = drive ? packColor(194, 132, 58, 248)
                                     : packColor(120, 128, 142, 245);

    ctx.out.circles.push_back({center, rootR, bodyFill, 0.0, true, false});
    ctx.out.circles.push_back({center, rootR, edgeCol, 1.6, false, false});

    const double toothPitch = 2.0 * kPi / teeth;
    for (int tooth = 0; tooth < teeth; ++tooth) {
        double mid = toothPhase + tooth * toothPitch;
        double rootHalf = toothPitch * 0.30;
        double tipHalf = toothPitch * 0.16;
        auto at = [&](double angle, double r) {
            return center + Vec2(std::cos(angle), std::sin(angle)) * r;
        };
        ctx.out.quads.push_back({at(mid - rootHalf, rootR), at(mid - tipHalf, tipR),
                                 at(mid + tipHalf, tipR), at(mid + rootHalf, rootR),
                                 toothFill, true, 0.0});
    }

    ctx.out.circles.push_back({center, rootR * 0.30, packColor(36, 40, 48, 255), 0.0, true, false});
    ctx.out.circles.push_back({center, rootR * 0.30, edgeCol, 1.2, false, false});
    if (rootR > 6.0) {
        for (int h = 0; h < 4; ++h) {
            double angle = bodyPhase + (h + 0.5) * (kPi / 2.0);
            Vec2 pos = center + Vec2(std::cos(angle), std::sin(angle)) * (rootR * 0.62);
            ctx.out.circles.push_back({pos, rootR * 0.14, packColor(36, 40, 48, 220), 0.0, true, false});
        }
    }
}

void emitChain(BuildContext& ctx, Vec2 a, Vec2 b, double va, double vb,
               double current, int compId, bool sourceDrive = false) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);
    double half = chainHalfWidth(ctx);

    // Tension colouring = the same solver potentials as every other view.
    if (ctx.p.layers.potential && ctx.hasPotentialRange()) {
        render::PrimGradient grad;
        grad.a = a; grad.b = b;
        grad.width = half * 2.0;
        grad.vA = va; grad.vB = vb;
        grad.vMin = ctx.vMin; grad.vMax = ctx.vMax;
        grad.alpha = 110;
        ctx.out.gradients.push_back(grad);
    }

    namespace cg = physics::chain_geometry;
    const double rollerR = cg::linkRadius(ctx.p.wireThickness);
    const double railOff = cg::sprocketPitchRadius(half, rollerR);
    const double driveR = cg::driveSprocketPitchRadius(half, rollerR);
    const auto drivePath =
        sourceDrive ? cg::sourceDrivePath(a, b, railOff, driveR)
                    : cg::SourceDrivePath{};

    // Guide rails follow the simulated pitch track exactly.
    uint32_t rail = packColor(150, 160, 175, 120);
    if (drivePath.valid) {
        ctx.out.lines.push_back({drivePath.aTop, drivePath.driveLeftTop, 1.2, rail, true});
        ctx.out.polylines.push_back({sampleClockwiseArc(drivePath.center, drivePath.driveRadius,
                                                        drivePath.driveLeftTop,
                                                        drivePath.driveRightTop),
                                     1.2, rail, true});
        ctx.out.lines.push_back({drivePath.driveRightTop, drivePath.bTop, 1.2, rail, true});
        ctx.out.lines.push_back({drivePath.bBottom, drivePath.driveRightBottom, 1.2, rail, true});
        ctx.out.polylines.push_back({sampleClockwiseArc(drivePath.center, drivePath.driveRadius,
                                                        drivePath.driveRightBottom,
                                                        drivePath.driveLeftBottom),
                                     1.2, rail, true});
        ctx.out.lines.push_back({drivePath.driveLeftBottom, drivePath.aBottom, 1.2, rail, true});
    } else {
        ctx.out.lines.push_back({a + perp * railOff, b + perp * railOff, 1.2, rail, true});
        ctx.out.lines.push_back({a - perp * railOff, b - perp * railOff, 1.2, rail, true});
    }

    // REAL Box2D chain: rigid-jointed links from the mechanics world, drawn
    // as a bicycle chain — rollers with pins, joined by alternating outer
    // (wide, light) and inner (narrow, dark) plate pairs.
    if (ctx.p.chainLinks) {
        const uint32_t rollerFill = packColor(70, 76, 88, 255);
        const uint32_t rollerEdge = packColor(208, 214, 224, 235);
        const uint32_t pinCol = packColor(228, 232, 240, 245);
        const uint32_t outerPlate = packColor(196, 204, 216, 225);
        const uint32_t innerPlate = packColor(140, 148, 162, 215);

        auto emitPlates = [&](Vec2 from, Vec2 to, bool outer) {
            Vec2 d = to - from;
            double dl = d.length();
            if (dl < 1e-6) return;
            Vec2 n(-d.y / dl, d.x / dl);
            double off = outer ? rollerR * 0.62 : rollerR * 0.34;
            uint32_t col = outer ? outerPlate : innerPlate;
            double w = outer ? cg::kOuterPlateWidth : cg::kInnerPlateWidth;
            ctx.out.lines.push_back({from + n * off, to + n * off, w, col, true});
            ctx.out.lines.push_back({from - n * off, to - n * off, w, col, true});
        };

        const physics::ChainLink* prev = nullptr;
        const physics::ChainLink* first = nullptr;
        for (const auto& link : *ctx.p.chainLinks) {
            if (link.componentId != compId) continue;
            if (!first) first = &link;
            if (prev)
                emitPlates(prev->pos, link.pos, prev->indexInLoop % 2 == 0);
            prev = &link;
        }
        if (prev && first && prev != first)
            emitPlates(prev->pos, first->pos, prev->indexInLoop % 2 == 0);
        // Rollers drawn after the plates so they sit on top at the joints.
        for (const auto& link : *ctx.p.chainLinks) {
            if (link.componentId != compId) continue;
            ctx.out.circles.push_back({link.pos, rollerR, rollerFill, 0.0, true, false});
            ctx.out.circles.push_back({link.pos, rollerR, rollerEdge, 1.4, false, false});
            ctx.out.circles.push_back({link.pos, rollerR * 0.35, pinCol, 0.0, true, false});
        }
        return;
    }

    // Fallback (no sim): phase animation. Bicycle-chain look.
    double speed = mechanics::chainSpeedFromCurrent(current);
    double spacing = drivePath.valid ? cg::linkPitch(rollerR) : std::max(half * 2.4, 10.0);
    double perimeter = drivePath.valid ? drivePath.perimeter : len;
    double phase = std::fmod(ctx.p.time * speed * kVisualChainSpeed, spacing * 2.0);
    if (phase < 0.0) phase += spacing * 2.0;

    uint32_t linkCol = packColor(208, 214, 224, 230);
    uint32_t barCol = packColor(150, 160, 175, 220);
    int count = static_cast<int>(perimeter / spacing) + 2;
    Vec2 prev;
    bool hasPrev = false;
    for (int i = 0; i < count; ++i) {
        double t = i * spacing + phase;
        t = std::fmod(t, perimeter);
        if (t < 0.0) t += perimeter;
        Vec2 p = drivePath.valid ? cg::sourceDrivePointAt(drivePath, t)
                                 : a + unit * t;
        ctx.out.circles.push_back({p, drivePath.valid ? rollerR : half * 0.78,
                                   linkCol, 1.8, false, false});
        if (hasPrev && (p - prev).length() < spacing * 1.5) {
            Vec2 linkUnit = (p - prev).normalized();
            ctx.out.lines.push_back({prev + linkUnit * (half * 0.5), p - linkUnit * (half * 0.5),
                                     2.2, barCol, true});
        }
        prev = p;
        hasPrev = true;
        (void)compId;
    }
}

void emitBrake(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
               double va, double vb, double power) {
    auto sections = physics::resistorPathSections(a, b, va, vb, ctx.p.wireThickness);
    const physics::ConductivePathSection* body = nullptr;
    for (const auto& s : sections)
        if (s.material == physics::VisualMaterial::ResistiveBody) { body = &s; break; }
    if (!body) return;

    Vec2 axis = body->end - body->start;
    double len = axis.length();
    if (len < 0.5) return;
    Vec2 unit = axis / len;
    Vec2 perp(-unit.y, unit.x);

    double half = chainHalfWidth(ctx);
    double padTh = ctx.p.wireThickness * 0.6;
    uint32_t padCol = packColor(126, 56, 46, 235);
    uint32_t padEdge = packColor(214, 142, 120, 220);

    for (int side : {1, -1}) {
        Vec2 inner = perp * (half * 1.15 * side);
        Vec2 outer = perp * ((half * 1.15 + padTh) * side);
        render::PrimQuad pad{body->start + inner, body->start + outer,
                             body->end + outer, body->end + inner, padCol, true, 0.0};
        ctx.out.quads.push_back(pad);
        pad.filled = false;
        pad.color = padEdge;
        pad.outlineThickness = 1.4;
        ctx.out.quads.push_back(pad);
    }

    double heat = mechanics::brakeHeatFromPower(comp.type, power);
    double frac = ctx.maxP > 1e-12 ? std::clamp(heat / ctx.maxP, 0.0, 1.0) : 0.0;
    if (ctx.p.layers.heat && frac > 0.02) {
        Vec2 mid = (body->start + body->end) * 0.5;
        ctx.out.glows.push_back({mid, len * 0.7, frac,
                                 packColor(255, 140, 60, static_cast<unsigned>(12 + 40 * frac))});
    }

    char buf[48];
    if (comp.value >= 1000.0)
        std::snprintf(buf, sizeof(buf), "%.1f %s", comp.value / 1000.0, tr("k\xCE\xA9 brake"));
    else
        std::snprintf(buf, sizeof(buf), "%.0f %s", comp.value, tr("\xCE\xA9 brake"));
    Vec2 mid = (body->start + body->end) * 0.5;
    ctx.out.labels.push_back({mid + perp * (half + padTh + 10.0 / ctx.safeScale()), buf,
                              packColor(200, 200, 200), false});
}

// A meshing sprocket must drop its teeth into the gaps BETWEEN the chain rollers
// riding it, so the wheel visibly pushes the chain (no slip). We measure where
// the engaged rollers sit on the pitch circle and return the tooth phase modulo
// the tooth pitch. compId < 0 means "any chain" (junction gears mesh every
// branch meeting at the node). A smooth radial weight fades rollers in/out at
// the arc ends instead of popping them at a hard gate, so the measured phase is
// jitter-free. false when no rollers ride the wheel yet (sim cold).
bool wheelMeshPhase(BuildContext& ctx, Vec2 center, double pitchR, double rollerR,
                    int teeth, int compId, double* meshMod) {
    if (!ctx.p.chainLinks || teeth <= 0) return false;
    const double band = rollerR * 2.0;
    double sx = 0.0, sy = 0.0, wsum = 0.0;
    for (const auto& link : *ctx.p.chainLinks) {
        if (compId >= 0 && link.componentId != compId) continue;
        Vec2 rel = link.pos - center;
        double dr = std::abs(rel.length() - pitchR);
        if (dr >= band) continue;
        double w = 1.0 - dr / band; // 1 on the pitch circle, smoothly -> 0 at the band edge
        double ang = std::atan2(rel.y, rel.x);
        // Weighted circular mean at the tooth frequency: collapses every roller
        // onto one representative angle modulo the tooth pitch.
        sx += w * std::cos(teeth * ang);
        sy += w * std::sin(teeth * ang);
        wsum += w;
    }
    if (wsum < 0.25) return false;
    double rollerPhase = std::atan2(sy, sx) / teeth; // roller angle mod tooth pitch
    *meshMod = rollerPhase + kPi / teeth;            // teeth half a pitch on, in the gaps
    return true;
}

// Honest chain travel (∫ targetSpeed dt) for a component, when plumbed from the
// sim. This advances at the real chain speed, so a wheel spun from it turns WITH
// the chain (no slip) — unlike the ~100x slower ∫I dt phase.
bool honestChainTravel(BuildContext& ctx, int compId, double* travel) {
    if (!ctx.p.chainTravel) return false;
    auto it = ctx.p.chainTravel->find(compId);
    if (it == ctx.p.chainTravel->end()) return false;
    *travel = it->second;
    return true;
}

void emitCrank(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
               double va, double vb, double current) {
    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    namespace cg = physics::chain_geometry;
    const double rollerR = cg::linkRadius(ctx.p.wireThickness);
    const double pitchR = cg::driveSprocketPitchRadius(chainHalfWidth(ctx), rollerR);
    const int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));
    const double toothPitch = 2.0 * kPi / teeth;

    // The wheel BODY spins at the honest chain speed (no slip), continuously —
    // this is what the hub holes/knob rotate by, smoothly. Fallback to the ∫I dt
    // phase only when the sim travel is not plumbed (unit tests).
    double travel = 0.0;
    double chainTravel = honestChainTravel(ctx, comp.id, &travel)
        ? travel
        : (ctx.p.flowIntegrals
               ? componentIntegral(ctx.p.flowIntegrals, comp.id) * kVisualChainSpeed
               : ctx.p.time * mechanics::chainSpeedFromCurrent(current) * kVisualChainSpeed);
    double bodyPhase = cg::sourceDriveSprocketPhaseFromChainTravel(chainTravel, pitchR);

    // The TEETH snap onto the measured roller gaps so they mesh and push the
    // chain; the continuous bodyPhase supplies the winding, so the snapped tooth
    // phase is continuous AND meshed (bodyPhase and the rollers move together).
    double toothPhase = bodyPhase;
    double meshMod = 0.0;
    if (wheelMeshPhase(ctx, mid, pitchR, rollerR, teeth, comp.id, &meshMod)) {
        double k = std::round((bodyPhase - meshMod) / toothPitch);
        toothPhase = meshMod + k * toothPitch;
    }
    emitSprocket(ctx, mid, pitchR, toothPhase, bodyPhase, true);

    emitChain(ctx, a, b, va, vb, current, comp.id, true);

    // Grab knob on the rim: the handle you can drag (dynamo). Rides the body.
    Vec2 knobDir(std::cos(bodyPhase), std::sin(bodyPhase));
    ctx.out.circles.push_back({mid + knobDir * (pitchR * 0.92), rollerR * 1.35,
                               packColor(255, 220, 130, 245), 0.0, true, true});

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f %s", comp.value, tr("V drive"));
    Vec2 dir = (b - a).normalized();
    Vec2 perp(-dir.y, dir.x);
    ctx.out.labels.push_back({mid + perp * 24.0, buf, packColor(200, 200, 200), false});
}

void emitAnchor(BuildContext& ctx, Vec2 pos) {
    double s = 1.0 / ctx.safeScale();
    auto P = [&](double dx, double dy) { return pos + Vec2(dx * s, dy * s); };
    uint32_t col = packColor(235, 235, 235);
    ctx.out.lines.push_back({P(0, 0), P(0, 8), 2.0, col, true});
    ctx.out.quads.push_back({P(-12, 8), P(12, 8), P(12, 16), P(-12, 16),
                             packColor(70, 76, 86, 255), true, 0.0});
    ctx.out.quads.push_back({P(-12, 8), P(12, 8), P(12, 16), P(-12, 16), col, false, 1.5});
    for (int i = 0; i < 3; ++i)
        ctx.out.lines.push_back({P(-10 + i * 8, 16), P(-4 + i * 8, 22), 1.5, col, true});
}

// Render-only: a closed bicycle-chain OVAL between two equal-radius sprockets
// centred at A and B (pitch radius R) — two straight runs tangent to both tooth
// circles plus a semicircle wrap arc around EACH sprocket. This is the same
// racetrack the chain sim runs for every other component. It threads onto both
// gears — the node gear at A and the shaft sprocket at B — instead of collapsing
// into the node point. `phase` (arc length) advances the rollers so the chain
// turns WITH the sprockets: for the capacitor it is driven by the shaft angle θ
// (= charge), so winding the spring and moving the chain are the same motion,
// fed by the neighbouring node gear during charging.
void emitStaticChainOval(BuildContext& ctx, Vec2 A, Vec2 B, double R,
                         double va, double vb, double phase = 0.0) {
    namespace cg = physics::chain_geometry;
    Vec2 ab = B - A;
    double len = ab.length();
    if (len < 1e-3) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    // Oval racetrack point at arc-length t (matches ChainSim::Oval, ccw).
    double arc = kPi * R;
    double perimeter = 2.0 * len + 2.0 * arc;
    auto ovalAt = [&](double t) -> Vec2 {
        t = std::fmod(t, perimeter);
        if (t < 0.0) t += perimeter;
        if (t < len) return A + unit * t + perp * R;                  // top a->b
        if (t < len + arc) {
            double phi = (t - len) / R, ang = kPi * 0.5 - phi;        // wrap B
            return B + perp * (R * std::sin(ang)) + unit * (R * std::cos(ang));
        }
        if (t < 2.0 * len + arc)
            return B - unit * (t - len - arc) - perp * R;             // bottom b->a
        double phi = (t - 2.0 * len - arc) / R, ang = -kPi * 0.5 - phi; // wrap A
        return A + perp * (R * std::sin(ang)) + unit * (R * std::cos(ang));
    };

    const double rollerR = cg::linkRadius(ctx.p.wireThickness);
    const double pitch = cg::linkPitch(rollerR);
    int count = std::max(8, static_cast<int>(perimeter / pitch));
    double spacing = perimeter / count;

    // Potential tint, same palette as the rest of the chain.
    if (ctx.p.layers.potential && ctx.hasPotentialRange()) {
        render::PrimGradient grad;
        grad.a = A; grad.b = B;
        grad.width = cg::chainHalfWidth(ctx.p.wireThickness) * 2.0;
        grad.vA = va; grad.vB = vb; grad.vMin = ctx.vMin; grad.vMax = ctx.vMax;
        grad.alpha = 90;
        ctx.out.gradients.push_back(grad);
    }

    // Faint guide rails along the two straight runs (as emitChain draws them).
    uint32_t rail = packColor(150, 160, 175, 120);
    ctx.out.lines.push_back({A + perp * R, B + perp * R, 1.2, rail, true});
    ctx.out.lines.push_back({A - perp * R, B - perp * R, 1.2, rail, true});

    // Same bicycle-chain look as the sim: alternating outer/inner plate pairs
    // between consecutive rollers, then rollers (fill + edge + pin) on top.
    const uint32_t rollerFill = packColor(70, 76, 88, 255);
    const uint32_t rollerEdge = packColor(208, 214, 224, 235);
    const uint32_t pinCol = packColor(228, 232, 240, 245);
    const uint32_t outerPlate = packColor(196, 204, 216, 225);
    const uint32_t innerPlate = packColor(140, 148, 162, 215);
    auto emitPlates = [&](Vec2 from, Vec2 to, bool outer) {
        Vec2 d = to - from;
        double dl = d.length();
        if (dl < 1e-6) return;
        Vec2 n(-d.y / dl, d.x / dl);
        double off = outer ? rollerR * 0.62 : rollerR * 0.34;
        uint32_t col = outer ? outerPlate : innerPlate;
        double w = outer ? cg::kOuterPlateWidth : cg::kInnerPlateWidth;
        ctx.out.lines.push_back({from + n * off, to + n * off, w, col, true});
        ctx.out.lines.push_back({from - n * off, to - n * off, w, col, true});
    };
    for (int i = 0; i < count; ++i) {
        Vec2 p = ovalAt(spacing * i + phase);
        Vec2 q = ovalAt(spacing * ((i + 1) % count) + phase);
        emitPlates(p, q, i % 2 == 0);
    }
    for (int i = 0; i < count; ++i) {
        Vec2 p = ovalAt(spacing * i + phase);
        ctx.out.circles.push_back({p, rollerR, rollerFill, 0.0, true, false});
        ctx.out.circles.push_back({p, rollerR, rollerEdge, 1.4, false, false});
        ctx.out.circles.push_back({p, rollerR * 0.35, pinCol, 0.0, true, false});
    }
}

// Spring capacitor: a spring slung between two crank arms on two INDEPENDENT,
// counter-rotating shafts (NOT a shared axle — that is what makes it a
// capacitor, vs the inductor flywheel/junction idlers). Relative shaft angle =
// charge; spring restoring moment = voltage. Kinematics live in the pure,
// testable mechanics::SpringCapacitorModel; this function is render-only.
void emitSpring(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                double va, double vb) {
    auto g = capacitorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;
    double len = (b - a).length();

    // Charge angle from the (transient) capacitor voltage; +Vc -> compression,
    // so "spring contracts as the cap charges" holds.
    double vc = va - vb;
    double vRange = std::max(std::abs(ctx.vMax - ctx.vMin), 1e-9);

    mechanics::SpringCapacitorModel m;
    m.p.halfSpan = std::clamp(len * 0.30, 24.0, len * 0.42);
    m.p.armLen = std::clamp(len * 0.11, g.plateHalf * 0.55, m.p.halfSpan * 0.8);
    m.p.baseAmp = std::max(g.plateHalf * 0.5, ctx.p.wireThickness * 0.9);
    m.theta = mechanics::capacitorThetaFromVoltage(vc, vRange, m.p.thetaMax);

    // local (x along axis, y along +perp) -> world.
    auto toWorld = [&](Vec2 l) { return g.mid + g.unit * l.x + g.perp * l.y; };

    Vec2 shaftLW = toWorld(m.shaftL());
    Vec2 shaftRW = toWorld(m.shaftR());
    Vec2 crankLW = toWorld(m.crankL());
    Vec2 crankRW = toWorld(m.crankR());
    Vec2 springMidW = (crankLW + crankRW) * 0.5;

    double chargeMag = std::abs(m.charge());
    // Violet = stretch/charge+, coral = compress/charge−.
    uint32_t pos = packColor(127, 119, 221, 245);
    uint32_t neg = packColor(224, 96, 122, 245);
    uint32_t chargeCol = m.charge() >= 0.0 ? pos : neg;
    uint32_t metal = packColor(139, 147, 176, 235);

    // Sprockets at the chain PITCH radius so the wrapping chain seats on the
    // teeth; counter-rotated by ±theta (independent shafts turning against each
    // other). The shaft is just a short hub at the centre — no long hanging line.
    namespace cg = physics::chain_geometry;
    double pitchR = cg::sprocketPitchRadius(cg::chainHalfWidth(ctx.p.wireThickness),
                                            cg::linkRadius(ctx.p.wireThickness));

    // Each lead is a chain OVAL between the node gear (at a / b) and the shaft
    // sprocket — threaded onto both, identical to the system chain. The chain,
    // the shaft sprocket, the crank arm and the spring are ONE rigid drive: they
    // all advance with the shaft angle θ (= charge), so the chain visibly rolls
    // as the spring winds. The roller phase = R·θ (the arc the sprocket turns);
    // the two shafts counter-rotate, so their phases are opposite. During
    // charging the neighbouring node gear turns, the chain rolls, the spring
    // winds — the torque path the user asked for.
    double chainPhase = pitchR * m.theta;
    emitStaticChainOval(ctx, a, shaftLW, pitchR, va, va, -chainPhase);
    emitStaticChainOval(ctx, b, shaftRW, pitchR, vb, vb, +chainPhase);

    emitSprocket(ctx, shaftLW, pitchR, m.theta, m.theta, false);
    emitSprocket(ctx, shaftRW, pitchR, -m.theta, -m.theta, false);

    // Crank arms shaft -> tip, tips marked in the charge colour.
    ctx.out.lines.push_back({shaftLW, crankLW, 4.0, metal, true});
    ctx.out.lines.push_back({shaftRW, crankRW, 4.0, metal, true});
    double knob = std::max(2.5, m.p.armLen * 0.16);

    // The procedural spring: coil spacing = len/coils (coils spread when
    // stretched, bunch when compressed), amplitude opposite the deflection. Its
    // endpoints are pinned pixel-exact to the crank tips so the spring never
    // detaches from its attachment knobs.
    std::vector<Vec2> pts;
    for (const Vec2& l : m.springPath()) pts.push_back(toWorld(l));
    pts.front() = crankLW;
    pts.back() = crankRW;
    uint32_t springCol =
        render::blendColor(packColor(170, 178, 190, 230), chargeCol, 0.35 + 0.65 * chargeMag);
    ctx.out.polylines.push_back({std::move(pts), 2.6, springCol, true});

    // Attachment knobs drawn AFTER the spring, at the exact same crank tips.
    ctx.out.circles.push_back({crankLW, knob, chargeCol, 0.0, true, false});
    ctx.out.circles.push_back({crankRW, knob, chargeCol, 0.0, true, false});

    // Mode label + capacitance, above the spring.
    const char* modeText = m.mode() == mechanics::SpringCapacitorModel::Mode::Stretched
                               ? tr("stretched")
                           : m.mode() == mechanics::SpringCapacitorModel::Mode::Compressed
                               ? tr("compressed")
                               : tr("neutral");
    double s = 1.0 / ctx.safeScale();
    Vec2 above = springMidW + g.perp * (m.p.armLen + 12.0 * s);
    ctx.out.labels.push_back({above, modeText, packColor(200, 200, 200), false});

    char buf[48];
    formatCapacitance(comp.value, buf, sizeof(buf));
    ctx.out.labels.push_back({above + g.perp * (12.0 * s), buf, packColor(170, 176, 190), false});
}

void emitFlywheel(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                  double va, double vb, double current) {
    auto g = inductorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;

    emitChain(ctx, a, g.coilStart, va, va, current, comp.id);
    emitChain(ctx, g.coilEnd, b, vb, vb, current, comp.id);

    Vec2 mid = (g.coilStart + g.coilEnd) * 0.5;
    double coilLen = (g.coilEnd - g.coilStart).length();
    double radius = coilLen * 0.55;

    double iFrac = ctx.maxI > 1e-12 ? std::clamp(std::abs(current) / ctx.maxI, 0.0, 1.0) : 0.0;
    uint32_t rimCol = packColor(
        static_cast<unsigned>(150 + 60 * iFrac),
        static_cast<unsigned>(140 + 30 * iFrac),
        static_cast<unsigned>(200 + 40 * iFrac), 235);
    ctx.out.circles.push_back({mid, radius, rimCol, 2.0 + 2.5 * iFrac, false, false});
    ctx.out.circles.push_back({mid, radius * 0.16, rimCol, 0.0, true, false});

    // Spokes spin with the INTEGRAL of angular momentum: the flywheel angle
    // is proportional to the charge that has passed (and never teleports).
    double angle0 = spinPhase(ctx, comp.id,
                              mechanics::flywheelAngularMomentumFromCurrent(current),
                              kVisualSpinRate);
    for (int s = 0; s < 4; ++s) {
        double angle = angle0 + s * (kPi / 2.0);
        Vec2 dir(std::cos(angle), std::sin(angle));
        ctx.out.lines.push_back({mid - dir * (radius * 0.9), mid + dir * (radius * 0.9),
                                 1.8, rimCol, true});
    }

    if (iFrac > 0.02) {
        ctx.out.glows.push_back({mid, radius * 1.4, iFrac,
                                 packColor(168, 130, 255, static_cast<unsigned>(8 + 30 * iFrac))});
    }

    char buf[32];
    formatInductance(comp.value, buf, sizeof(buf));
    ctx.out.labels.push_back({mid + g.perp * (radius + 10.0 / ctx.safeScale()), buf,
                              packColor(200, 200, 200), false});
}

// Sprockets at junction nodes; teeth rotate with the local chain speed,
// visually coupling every branch that meets here (one chain, one motion).
// All radii come from chain_geometry, so the simulated chain rollers ride
// exactly on the pitch circle between the drawn teeth.
void emitGears(BuildContext& ctx) {
    namespace cg = physics::chain_geometry;
    const double rollerR = cg::linkRadius(ctx.p.wireThickness);
    const double pitchR = cg::sprocketPitchRadius(chainHalfWidth(ctx), rollerR);
    const double tipR = cg::sprocketTipRadius(pitchR, rollerR);
    const double rootR = cg::sprocketRootRadius(pitchR, rollerR);
    const int teeth = cg::sprocketTeeth(pitchR, cg::linkPitch(rollerR));

    for (const auto& node : ctx.circuit.nodes) {
        int connected = 0;
        double meanCurrent = 0.0;
        for (const auto& comp : ctx.circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            if (comp.nodeA != node.id && comp.nodeB != node.id) continue;
            ++connected;
            if (const BranchResult* br = branchFor(ctx.solution, comp.id))
                meanCurrent += std::abs(br->current);
        }
        if (connected < 2) continue;
        meanCurrent /= connected;

        const uint32_t bodyFill = packColor(58, 64, 76, 255);
        const uint32_t edgeCol = packColor(170, 178, 190, 230);
        const uint32_t toothFill = packColor(120, 128, 142, 245);

        // Disc body under the teeth + rim.
        ctx.out.circles.push_back({node.position, rootR, bodyFill, 0.0, true, false});
        ctx.out.circles.push_back({node.position, rootR, edgeCol, 1.6, false, false});

        // Wheel BODY spins at the honest chain speed of the branches meeting
        // here (no slip), continuously — the hub holes ride this. The chain
        // wraps the node clockwise for forward travel, hence the minus sign.
        // Fallback to the ∫I dt node phase only when travel is not plumbed.
        const double toothPitch = 2.0 * kPi / teeth;
        double bestTravel = 0.0, bestAbs = -1.0;
        int bestComp = -1; // dominant branch through this node
        bool haveTravel = false;
        if (ctx.p.chainTravel) {
            for (const auto& comp : ctx.circuit.components) {
                if (comp.type == ComponentType::Ground) continue;
                if (comp.nodeA != node.id && comp.nodeB != node.id) continue;
                auto it = ctx.p.chainTravel->find(comp.id);
                if (it == ctx.p.chainTravel->end()) continue;
                if (std::abs(it->second) > bestAbs) {
                    bestAbs = std::abs(it->second);
                    bestTravel = it->second;
                    bestComp = comp.id;
                    haveTravel = true;
                }
            }
        }
        double bodyPhase = haveTravel
            ? -bestTravel / pitchR
            : (ctx.p.flowIntegrals
                   ? nodeIntegral(ctx.p.flowIntegrals, node.id) * kVisualChainSpeed / pitchR
                   : ctx.p.time * mechanics::chainSpeedFromCurrent(meanCurrent) *
                         kVisualChainSpeed / pitchR);

        // Teeth: snapped onto the gaps between the rollers of the DOMINANT branch
        // through this node (independent per-branch sims can't all co-phase; mesh
        // the strongest cleanly), so the gear visibly meshes with the chain.
        double toothPhase = bodyPhase;
        double meshMod = 0.0;
        if (wheelMeshPhase(ctx, node.position, pitchR, rollerR, teeth, bestComp, &meshMod)) {
            double k = std::round((bodyPhase - meshMod) / toothPitch);
            toothPhase = meshMod + k * toothPitch;
        }
        for (int tooth = 0; tooth < teeth; ++tooth) {
            double mid = toothPhase + tooth * toothPitch;
            double rootHalf = toothPitch * 0.30; // angular half-widths
            double tipHalf = toothPitch * 0.16;
            auto at = [&](double angle, double r) {
                return node.position + Vec2(std::cos(angle), std::sin(angle)) * r;
            };
            render::PrimQuad quad{at(mid - rootHalf, rootR), at(mid - tipHalf, tipR),
                                  at(mid + tipHalf, tipR), at(mid + rootHalf, rootR),
                                  toothFill, true, 0.0};
            ctx.out.quads.push_back(quad);
        }

        // Hub: bearing hole + four lightening holes between hub and rim.
        ctx.out.circles.push_back({node.position, rootR * 0.30, packColor(36, 40, 48, 255), 0.0, true, false});
        ctx.out.circles.push_back({node.position, rootR * 0.30, edgeCol, 1.2, false, false});
        if (rootR > 6.0) {
            for (int h = 0; h < 4; ++h) {
                double angle = bodyPhase + (h + 0.5) * (kPi / 2.0);
                Vec2 pos = node.position + Vec2(std::cos(angle), std::sin(angle)) * (rootR * 0.62);
                ctx.out.circles.push_back({pos, rootR * 0.14, packColor(36, 40, 48, 220), 0.0, true, false});
            }
        }
    }
}

void buildMechanics(BuildContext& ctx) {
    // Sprockets first: their disc bodies must stay UNDER the chain rollers
    // (circles render in push order), so the chain visibly wraps the gears.
    emitGears(ctx);

    for (const auto& comp : ctx.circuit.components) {
        const Node* nodeA = ctx.circuit.findNode(comp.nodeA);
        const Node* nodeB = ctx.circuit.findNode(comp.nodeB);
        if (!nodeA || !nodeB) continue;

        Vec2 a = nodeA->position;
        Vec2 b = nodeB->position;
        double va = potentialFor(ctx.solution, comp.nodeA);
        double vb = potentialFor(ctx.solution, comp.nodeB);
        double branchCurrent = 0.0;
        double branchPower = 0.0;
        if (const BranchResult* branch = branchFor(ctx.solution, comp.id)) {
            branchCurrent = branch->current;
            branchPower = branch->power;
        }

        switch (comp.type) {
            case ComponentType::Wire:
                emitChain(ctx, a, b, va, vb, branchCurrent, comp.id);
                break;
            case ComponentType::Resistor:
                emitChain(ctx, a, b, va, vb, branchCurrent, comp.id);
                emitBrake(ctx, comp, a, b, va, vb, branchPower);
                break;
            case ComponentType::VoltageSource:
                emitCrank(ctx, comp, a, b, va, vb, branchCurrent);
                break;
            case ComponentType::Ground:
                emitAnchor(ctx, b);
                break;
            case ComponentType::Capacitor:
                emitSpring(ctx, comp, a, b, va, vb);
                break;
            case ComponentType::Inductor:
                emitFlywheel(ctx, comp, a, b, va, vb, branchCurrent);
                break;
            case ComponentType::Diode: {
                // Ratchet: chain plus a pawl triangle showing the only allowed
                // direction of motion (A -> B, same as diode conduction).
                emitChain(ctx, a, b, va, vb, branchCurrent, comp.id);
                Vec2 unit = (b - a).normalized();
                Vec2 perp(-unit.y, unit.x);
                Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
                double s = std::max(ctx.p.wireThickness * 1.2, 10.0);
                ctx.out.arrows.push_back({mid - unit * (s * 0.5), unit, s,
                                          packColor(255, 196, 110, 235)});
                ctx.out.lines.push_back({mid + unit * (s * 0.6) + perp * (s * 0.6),
                                         mid + unit * (s * 0.6) - perp * (s * 0.6),
                                         2.5, packColor(235, 235, 230, 240), true});
                ctx.out.labels.push_back({mid + perp * (s + 8.0 / ctx.safeScale()), tr("ratchet"),
                                          packColor(180, 185, 190, 220), false});
                break;
            }
            case ComponentType::Switch: {
                // Coupler: closed = continuous chain; open = visible gap.
                bool closed = comp.value >= 0.5;
                Vec2 unit = (b - a).normalized();
                Vec2 perp(-unit.y, unit.x);
                Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
                double s = std::max(ctx.p.wireThickness * 1.2, 10.0);
                if (closed) {
                    emitChain(ctx, a, b, va, vb, branchCurrent, comp.id);
                    ctx.out.lines.push_back({mid + perp * (s * 0.7), mid - perp * (s * 0.7),
                                             3.0, packColor(208, 214, 224, 235), true});
                } else {
                    emitChain(ctx, a, mid - unit * s, va, va, 0.0, comp.id);
                    emitChain(ctx, mid + unit * s, b, vb, vb, 0.0, comp.id);
                }
                ctx.out.labels.push_back({mid + perp * (s + 8.0 / ctx.safeScale()),
                                          closed ? tr("coupled") : tr("decoupled"),
                                          packColor(180, 185, 190, 220), false});
                break;
            }
        }

        if (comp.id == ctx.p.selectedComponent)
            emitSelectionHighlight(ctx, comp, a, b);

        if (ctx.solution && comp.type != ComponentType::Ground) {
            Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
            Vec2 dirr = (b - a).normalized();
            Vec2 perp(-dirr.y, dirr.x);
            emitReadoutLabels(ctx, comp, mid, perp, branchCurrent, branchPower);
        }
    }

    emitNodes(ctx);

    if (ctx.solution && ctx.p.layers.potential && ctx.hasPotentialRange())
        ctx.out.legend = {true, ctx.vMin, ctx.vMax};
}

// --- hydraulic (water) analogy -------------------------------------------------
// Mapped quantities come from HydraulicMapping (exact images of solver
// values); only the on-screen water-particle speed is amplified.

double pipeHalfWidth(const BuildContext& ctx) { return ctx.p.wireThickness * 0.55; }

// Pipe body: dark shell, pressure gradient (same honest potentials), outline.
void emitPipe(BuildContext& ctx, Vec2 a, Vec2 b, double va, double vb, double halfW) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 0.5) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    Vec2 c1 = a + perp * halfW;
    Vec2 c2 = a - perp * halfW;
    Vec2 c3 = b - perp * halfW;
    Vec2 c4 = b + perp * halfW;

    ctx.out.quads.push_back({c1, c2, c3, c4, packColor(26, 36, 52), true, 0.0});

    if (ctx.p.layers.potential && ctx.hasPotentialRange()) {
        render::PrimGradient grad;
        grad.a = a; grad.b = b;
        grad.width = halfW * 2.0;
        grad.vA = va; grad.vB = vb;
        grad.vMin = ctx.vMin; grad.vMax = ctx.vMax;
        grad.alpha = 150;
        ctx.out.gradients.push_back(grad);
    }

    ctx.out.quads.push_back({c1, c2, c3, c4, packColor(110, 150, 200, 230), false, 1.6});
    ctx.out.circles.push_back({a, halfW, packColor(110, 150, 200, 230), 1.6, false, false});
    ctx.out.circles.push_back({b, halfW, packColor(110, 150, 200, 230), 1.6, false, false});
}

// Water particles flowing with the mapped flow rate (sign-correct direction).
void emitWaterFlow(BuildContext& ctx, Vec2 a, Vec2 b, double current, int compId,
                   double halfW) {
    double flow = hydraulic::flowFromCurrent(current);
    if (std::abs(flow) <= 1e-12) return;

    physics::DriftSamplingConfig config;
    config.wireThickness = halfW * 2.0;
    config.cameraScale = ctx.p.cameraScale;
    config.time = ctx.p.time;
    config.componentId = compId;

    // Water balls are drawn at the EXACT Box2D collider size: the water world
    // is configured with particleWorldRadius(p.wireThickness) (MainWindow), so
    // any inflation here makes touching colliders (2r apart) read as balls
    // squashed half a radius into each other (user finding, 2026-06-11).
    double radius = physics::particleWorldRadius(ctx.p.wireThickness);
    uint32_t color = packColor(96, 170, 255, 185);

    if (ctx.p.simParticles) {
        for (const auto& sp : *ctx.p.simParticles)
            if (sp.componentId == compId)
                ctx.out.particles.push_back({sp.pos, radius, color, false});
        return;
    }

    auto particles = physics::sampleDriftParticles(a, b, flow, config);
    for (const auto& particle : particles)
        ctx.out.particles.push_back({particle.position, radius, color, false});
}

void emitConstriction(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                      double va, double vb, double power) {
    Vec2 ab = b - a;
    double len = ab.length();
    if (len < 1.0) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);

    // ONE geometry with the Box2D collider (ParticleSim addWalls): the drawn
    // funnel sits a hair (drawMargin) OUTSIDE the collider wall, so a ball
    // resting on the collider is still inside the drawn throat. Without this
    // shared profile the water flows in a wider channel than the throat necked
    // around it and rides outside the funnel (user finding 2026-06-13).
    double simHalf = ctx.p.wireThickness * 0.5;
    physics::HydraulicThrottle th = physics::hydraulicThrottle(len, simHalf);
    double margin = physics::hydraulicWallDrawMargin(simHalf);
    uint32_t wall = packColor(110, 150, 200, 230);

    auto wallPt = [&](double t, int side) {
        return a + unit * t + perp * (side * (th.halfWidthAt(t) + margin));
    };
    // Funnel walls: inlet cone -> throat -> outlet cone (piecewise-linear).
    for (int side : {1, -1}) {
        ctx.out.lines.push_back({wallPt(th.leadIn, side), wallPt(th.throatStart, side),
                                 1.8, wall, true});
        ctx.out.lines.push_back({wallPt(th.throatStart, side), wallPt(th.throatEnd, side),
                                 1.8, wall, true});
        ctx.out.lines.push_back({wallPt(th.throatEnd, side), wallPt(th.leadOut, side),
                                 1.8, wall, true});
    }
    // The necked flow region reads as a narrow pipe: shell + pressure gradient.
    emitPipe(ctx, a + unit * th.throatStart, a + unit * th.throatEnd, va, vb,
             th.throatHalfWidth + margin);

    Vec2 bodyMid = a + unit * ((th.leadIn + th.leadOut) * 0.5);
    double heat = hydraulic::frictionHeatFromPower(comp.type, power);
    double frac = ctx.maxP > 1e-12 ? std::clamp(heat / ctx.maxP, 0.0, 1.0) : 0.0;
    if (ctx.p.layers.heat && frac > 0.02) {
        ctx.out.glows.push_back({bodyMid, (th.leadOut - th.leadIn) * 0.7, frac,
                                 packColor(255, 140, 60, static_cast<unsigned>(10 + 36 * frac))});
    }

    char buf[32];
    if (comp.value >= 1000.0)
        std::snprintf(buf, sizeof(buf), "%.1f %s", comp.value / 1000.0, tr("k\xCE\xA9"));
    else
        std::snprintf(buf, sizeof(buf), "%.0f %s", comp.value, tr("\xCE\xA9"));
    ctx.out.labels.push_back({bodyMid + perp * (simHalf + margin + 10.0 / ctx.safeScale()), buf,
                              packColor(200, 200, 200), false});
}

void emitPump(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
              double va, double vb, double current) {
    emitPipe(ctx, a, b, va, vb, pipeHalfWidth(ctx));
    emitWaterFlow(ctx, a, b, current, comp.id, pipeHalfWidth(ctx));

    // The impeller is drawn EXACTLY where the Box2D collider lives (offset
    // toward the casing side, physics half-width) — what you see is what the
    // water particles are pushed by.
    double physHalf = ctx.p.wireThickness * 0.5;
    Vec2 center = physics::pumpImpellerCenter(a, b, physHalf);
    double r = physics::pumpImpellerRadius(physHalf);

    // Casing pocket bulge on the offset side.
    ctx.out.circles.push_back({center, r * 1.2, packColor(26, 36, 52, 255), 0.0, true, false});
    ctx.out.circles.push_back({center, r * 1.2, packColor(140, 190, 255), 2.0, false, false});

    // Blades at the REAL Box2D impeller angle when the water world runs, so
    // what you see is exactly what the particles collide with.
    double angle0 = 0.0;
    bool haveReal = false;
    if (ctx.p.paddleStates) {
        for (const auto& paddle : *ctx.p.paddleStates) {
            if (paddle.componentId == comp.id) {
                angle0 = paddle.angle;
                haveReal = true;
                break;
            }
        }
    }
    if (!haveReal)
        angle0 = spinPhase(ctx, comp.id, hydraulic::flowFromCurrent(current), kVisualSpinRate);
    for (int s = 0; s < 4; ++s) {
        double angle = angle0 + s * (kPi / 2.0);
        Vec2 dir(std::cos(angle), std::sin(angle));
        // Full blade as the collider: from -r to +r through the hub.
        ctx.out.lines.push_back({center - dir * r, center + dir * r, 2.4,
                                 packColor(96, 170, 255, 235), true});
    }
    ctx.out.circles.push_back({center, r * 0.22, packColor(96, 170, 255, 245), 0.0, true, false});

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f %s", comp.value, tr("V pump"));
    Vec2 dir = (b - a).normalized();
    Vec2 perp(-dir.y, dir.x);
    Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    ctx.out.labels.push_back({mid + perp * 24.0, buf, packColor(200, 200, 200), false});
}

// Hydraulic capacitor = a sealed tank split by an ELASTIC MEMBRANE. Water from
// terminal A bows the membrane toward B (displacing B-water out the B terminal),
// and vice versa — NO water ever crosses the membrane. Charge Q ∝ how far the
// membrane is bowed ∝ Vc; current through the cap = the SPEED the membrane moves,
// so a fully charged cap (membrane at rest) passes no DC — shown honestly here.
// During an RLC ring the membrane springs back and forth, visualising the
// oscillation. The tank is drawn wider than the schematic plate gap so the
// membrane motion is legible; both chambers stay FULL (water is incompressible),
// and the A-side balls tint "charged" as the membrane bows, so the growing
// charged region reads as filling.
void emitTank(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
              double va, double vb) {
    auto g = capacitorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;
    Vec2 unit = g.unit, perp = g.perp;

    double ph = g.plateHalf;
    double tankHalfAxis = std::max(ph * 0.9, g.gap * 0.5);
    Vec2 mouthA = g.mid - unit * tankHalfAxis;
    Vec2 mouthB = g.mid + unit * tankHalfAxis;

    double halfW = pipeHalfWidth(ctx);
    emitPipe(ctx, a, mouthA, va, va, halfW);
    emitPipe(ctx, mouthB, b, vb, vb, halfW);

    // Tank shell (filled, drawn under the water balls).
    Vec2 A1 = mouthA + perp * ph, A2 = mouthA - perp * ph;
    Vec2 B2 = mouthB - perp * ph, B1 = mouthB + perp * ph;
    ctx.out.quads.push_back({A1, A2, B2, B1, packColor(20, 30, 44), true, 0.0});

    // Membrane bow from Vc (signed): flat at Vc=0, bows toward B as Vc rises
    // (toward A if negative). Parabolic, pinned to the tank walls at top/bottom.
    double vc = va - vb;
    double range = std::max(ctx.vMax - ctx.vMin, 1e-9);
    double disp = std::clamp(vc / range, -1.0, 1.0);
    double bow = disp * tankHalfAxis * 0.82;
    auto membraneAxial = [&](double f) { return bow * (1.0 - f * f); }; // f in [-1,1]

    // Both chambers stay full (incompressible). Tint by side of the membrane:
    // A-side = "charged" (brightens with |Vc|), B-side = plain water.
    double r = std::max(1.0, physics::particleWorldRadius(ctx.p.wireThickness) * 0.85);
    double pitch = r * 2.15;
    double chargeI = std::clamp(std::abs(disp), 0.0, 1.0);
    uint32_t plain = packColor(96, 170, 255, 175);
    uint32_t charged = packColor(140, static_cast<unsigned>(205 + 40 * chargeI), 255, 215);
    int nAx = std::max(2, static_cast<int>(std::floor((2.0 * tankHalfAxis - r) / pitch)));
    int nPp = std::max(2, static_cast<int>(std::floor((2.0 * ph - r) / pitch)));
    for (int i = 0; i < nAx; ++i) {
        double ax = -tankHalfAxis + r +
                    (2.0 * tankHalfAxis - 2.0 * r) * (nAx > 1 ? double(i) / (nAx - 1) : 0.5);
        for (int j = 0; j < nPp; ++j) {
            double pp = -ph + r + (2.0 * ph - 2.0 * r) * (nPp > 1 ? double(j) / (nPp - 1) : 0.5);
            double f = pp / std::max(ph, 1e-9);
            bool aSide = ax < membraneAxial(f); // left of the bowed membrane
            Vec2 pos = g.mid + unit * ax + perp * pp;
            ctx.out.particles.push_back({pos, r, aSide ? charged : plain, false});
        }
    }

    // Membrane: a bold bowed polyline — a SOLID elastic barrier balls never cross.
    const int seg = 10;
    Vec2 prev;
    for (int s = 0; s <= seg; ++s) {
        double f = -1.0 + 2.0 * s / seg;
        Vec2 p = g.mid + unit * membraneAxial(f) + perp * (f * ph);
        if (s > 0)
            ctx.out.lines.push_back({prev, p, 2.6, packColor(235, 185, 120, 245), true});
        prev = p;
    }

    // Tank outline on top.
    ctx.out.quads.push_back({A1, A2, B2, B1, packColor(110, 150, 200, 230), false, 1.8});

    char buf[32];
    formatCapacitance(comp.value, buf, sizeof(buf));
    ctx.out.labels.push_back({g.mid + perp * (ph + 9.0 / ctx.safeScale()), buf,
                              packColor(200, 200, 200), false});
}

void emitTurbine(BuildContext& ctx, const Component& comp, Vec2 a, Vec2 b,
                 double va, double vb, double current) {
    auto g = inductorGeometry(a, b, ctx.p.wireThickness);
    if (!g.valid) return;

    double halfW = pipeHalfWidth(ctx);
    emitPipe(ctx, a, g.coilStart, va, va, halfW);
    emitPipe(ctx, g.coilEnd, b, vb, vb, halfW);
    emitPipe(ctx, g.coilStart, g.coilEnd, va, vb, halfW * 0.7);

    Vec2 mid = (g.coilStart + g.coilEnd) * 0.5;
    double radius = (g.coilEnd - g.coilStart).length() * 0.5;
    double flow = hydraulic::flowFromCurrent(current);
    double iFrac = ctx.maxI > 1e-12 ? std::clamp(std::abs(current) / ctx.maxI, 0.0, 1.0) : 0.0;

    uint32_t col = packColor(static_cast<unsigned>(120 + 60 * iFrac),
                             static_cast<unsigned>(170 + 30 * iFrac), 255, 235);
    ctx.out.circles.push_back({mid, radius, col, 2.0 + 2.0 * iFrac, false, false});
    double angle0 = ctx.p.time * flow * kVisualSpinRate;
    for (int s = 0; s < 4; ++s) {
        double angle = angle0 + s * (kPi / 2.0);
        Vec2 dir(std::cos(angle), std::sin(angle));
        ctx.out.lines.push_back({mid - dir * (radius * 0.85), mid + dir * (radius * 0.85),
                                 1.8, col, true});
    }

    char buf[32];
    formatInductance(comp.value, buf, sizeof(buf));
    ctx.out.labels.push_back({mid + g.perp * (radius + 10.0 / ctx.safeScale()), buf,
                              packColor(200, 200, 200), false});
}

void emitReservoir(BuildContext& ctx, Vec2 pos) {
    double s = 1.0 / ctx.safeScale();
    auto P = [&](double dx, double dy) { return pos + Vec2(dx * s, dy * s); };
    uint32_t col = packColor(140, 190, 255);
    ctx.out.lines.push_back({P(0, 0), P(0, 8), 2.0, col, true});
    ctx.out.lines.push_back({P(-12, 8), P(-12, 20), 1.8, col, true});
    ctx.out.lines.push_back({P(12, 8), P(12, 20), 1.8, col, true});
    ctx.out.lines.push_back({P(-12, 20), P(12, 20), 1.8, col, true});
    ctx.out.lines.push_back({P(-9, 13), P(9, 13), 1.5, packColor(96, 170, 255, 200), true});
}

void buildHydraulic(BuildContext& ctx) {
    for (const auto& comp : ctx.circuit.components) {
        const Node* nodeA = ctx.circuit.findNode(comp.nodeA);
        const Node* nodeB = ctx.circuit.findNode(comp.nodeB);
        if (!nodeA || !nodeB) continue;

        Vec2 a = nodeA->position;
        Vec2 b = nodeB->position;
        double va = potentialFor(ctx.solution, comp.nodeA);
        double vb = potentialFor(ctx.solution, comp.nodeB);
        double branchCurrent = 0.0;
        double branchPower = 0.0;
        if (const BranchResult* branch = branchFor(ctx.solution, comp.id)) {
            branchCurrent = branch->current;
            branchPower = branch->power;
        }

        double halfW = pipeHalfWidth(ctx);
        Vec2 unit = (b - a).normalized();
        Vec2 perp(-unit.y, unit.x);
        Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
        double s = std::max(ctx.p.wireThickness * 1.2, 10.0);

        switch (comp.type) {
            case ComponentType::Wire:
                emitPipe(ctx, a, b, va, vb, halfW);
                emitWaterFlow(ctx, a, b, branchCurrent, comp.id, halfW);
                break;
            case ComponentType::Resistor:
                emitPipe(ctx, a, b, va, vb, halfW);
                emitWaterFlow(ctx, a, b, branchCurrent, comp.id, halfW);
                emitConstriction(ctx, comp, a, b, va, vb, branchPower);
                break;
            case ComponentType::VoltageSource:
                emitPump(ctx, comp, a, b, va, vb, branchCurrent);
                break;
            case ComponentType::Ground:
                emitReservoir(ctx, b);
                break;
            case ComponentType::Capacitor:
                emitTank(ctx, comp, a, b, va, vb);
                break;
            case ComponentType::Inductor:
                emitTurbine(ctx, comp, a, b, va, vb, branchCurrent);
                emitWaterFlow(ctx, a, b, branchCurrent, comp.id, halfW * 0.7);
                break;
            case ComponentType::Diode: {
                // Check valve: flap triangle in the allowed direction (A -> B).
                emitPipe(ctx, a, b, va, vb, halfW);
                emitWaterFlow(ctx, a, b, branchCurrent, comp.id, halfW);
                ctx.out.arrows.push_back({mid - unit * (s * 0.5), unit, s,
                                          packColor(140, 190, 255, 235)});
                ctx.out.lines.push_back({mid + unit * (s * 0.6) + perp * (s * 0.6),
                                         mid + unit * (s * 0.6) - perp * (s * 0.6),
                                         2.5, packColor(200, 220, 255, 240), true});
                break;
            }
            case ComponentType::Switch: {
                bool closed = comp.value >= 0.5;
                if (closed) {
                    emitPipe(ctx, a, b, va, vb, halfW);
                    emitWaterFlow(ctx, a, b, branchCurrent, comp.id, halfW);
                } else {
                    emitPipe(ctx, a, mid - unit * s, va, va, halfW);
                    emitPipe(ctx, mid + unit * s, b, vb, vb, halfW);
                }
                // Gate valve: a bar across the pipe, lifted when open.
                Vec2 gateTop = mid + perp * (halfW * 2.2);
                Vec2 gateBottom = closed ? mid - perp * (halfW * 1.1) : mid + perp * (halfW * 0.6);
                ctx.out.lines.push_back({gateTop, gateBottom, 3.0,
                                         packColor(200, 220, 255, 240), true});
                ctx.out.lines.push_back({gateTop - unit * (s * 0.4), gateTop + unit * (s * 0.4),
                                         2.0, packColor(140, 190, 255, 235), true});
                break;
            }
        }

        if (comp.id == ctx.p.selectedComponent)
            emitSelectionHighlight(ctx, comp, a, b);

        if (ctx.solution && comp.type != ComponentType::Ground)
            emitReadoutLabels(ctx, comp, mid, perp, branchCurrent, branchPower);
    }

    // Junction chambers at the nodes — drawn at the SAME radius as the
    // physical plumbing chamber the particles flow through, filled in the
    // quad pass so the transiting water stays visible on top.
    double chamberR = physics::junctionRadius(ctx.p.wireThickness * 0.5);
    for (const auto& node : ctx.circuit.nodes) {
        int connected = 0;
        for (const auto& comp : ctx.circuit.components) {
            if (comp.type == ComponentType::Ground) continue;
            if (comp.nodeA == node.id || comp.nodeB == node.id) ++connected;
        }
        if (connected < 2) continue;
        // Octagon-ish disc from two overlapping filled squares (quads render
        // under the particle pass; a filled circle would cover the water).
        uint32_t shell = packColor(26, 36, 52);
        double h = chamberR * 0.71; // square corners ~ on the chamber circle
        Vec2 p = node.position;
        ctx.out.quads.push_back({p + Vec2(-h, -h), p + Vec2(h, -h),
                                 p + Vec2(h, h), p + Vec2(-h, h), shell, true, 0.0});
        double d = chamberR; // diamond corners on the chamber circle
        ctx.out.quads.push_back({p + Vec2(0, -d), p + Vec2(d, 0),
                                 p + Vec2(0, d), p + Vec2(-d, 0), shell, true, 0.0});
        ctx.out.circles.push_back({p, chamberR, packColor(110, 150, 200, 230), 1.6, false, false});
    }

    emitNodes(ctx);

    if (ctx.solution && ctx.p.layers.potential && ctx.hasPotentialRange())
        ctx.out.legend = {true, ctx.vMin, ctx.vMax};
}

// --- per-kind builders -------------------------------------------------------

void buildCircuitShapes(BuildContext& ctx, bool physicsLayers) {
    if (physicsLayers && ctx.solution && (ctx.p.layers.electricField || ctx.p.layers.potential))
        emitFieldBackdrop(ctx);

    for (const auto& comp : ctx.circuit.components) {
        const Node* nodeA = ctx.circuit.findNode(comp.nodeA);
        const Node* nodeB = ctx.circuit.findNode(comp.nodeB);
        if (!nodeA || !nodeB) continue;

        Vec2 a = nodeA->position;
        Vec2 b = nodeB->position;
        double va = potentialFor(ctx.solution, comp.nodeA);
        double vb = potentialFor(ctx.solution, comp.nodeB);
        double branchCurrent = 0.0;
        double branchPower = 0.0;
        if (const BranchResult* branch = branchFor(ctx.solution, comp.id)) {
            branchCurrent = branch->current;
            branchPower = branch->power;
        }

        switch (comp.type) {
            case ComponentType::Wire:          emitWire(ctx, a, b, va, vb); break;
            case ComponentType::Resistor:      emitResistor(ctx, comp, a, b, va, vb, branchPower); break;
            case ComponentType::VoltageSource: emitVoltageSource(ctx, comp, a, b, va, vb); break;
            case ComponentType::Ground:        emitGround(ctx, b); break;
            case ComponentType::Capacitor:     emitCapacitorSymbol(ctx, comp, a, b, va, vb); break;
            case ComponentType::Inductor:      emitInductorSymbol(ctx, comp, a, b, va, vb); break;
            case ComponentType::Diode:         emitDiodeSymbol(ctx, comp, a, b, va, vb); break;
            case ComponentType::Switch:        emitSwitchSymbol(ctx, comp, a, b, va, vb); break;
        }

        if (comp.id == ctx.p.selectedComponent)
            emitSelectionHighlight(ctx, comp, a, b);

        if (!ctx.solution || comp.type == ComponentType::Ground) continue;

        Vec2 mid((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
        Vec2 dirr = (b - a).normalized();
        Vec2 perp(-dirr.y, dirr.x);

        if (physicsLayers) {
            if (comp.type == ComponentType::Capacitor) {
                emitCapacitorPhysics(ctx, comp, a, b, va, vb, branchCurrent);
            } else if (comp.type == ComponentType::Resistor) {
                auto sections = physics::resistorPathSections(a, b, va, vb, ctx.p.wireThickness);
                // Box2D particles are tagged with the REAL component id (one
                // channel per component, makeChannelSpecs) — the per-section
                // pseudo-ids below never match them, so the sim path emits
                // once for the whole conductor, at the collider size
                // (regression: «в проводнике с резистором не виден поток
                // электронов»). The pseudo-ids stay as phase seeds for the
                // stateless sampling fallback.
                if (ctx.p.simParticles && ctx.p.layers.drift && std::abs(branchCurrent) > 1e-12)
                    emitDriftParticles(ctx, a, b, branchCurrent, comp.id);
                for (size_t si = 0; si < sections.size(); ++si) {
                    const auto& section = sections[si];
                    bool isBody = section.material == physics::VisualMaterial::ResistiveBody;
                    int sectionId = comp.id * 17 + static_cast<int>(si);

                    if (ctx.p.layers.electricField && isBody)
                        emitEFieldArrows(ctx, section.start, section.end,
                                         section.voltageStart, section.voltageEnd, section.halfWidth);
                    if (ctx.p.layers.current && std::abs(branchCurrent) > 1e-12)
                        emitCurrentArrows(ctx, section.start, section.end, branchCurrent, section.halfWidth);
                    if (!ctx.p.simParticles && ctx.p.layers.drift && std::abs(branchCurrent) > 1e-12)
                        emitDriftParticles(ctx, section.start, section.end, branchCurrent, sectionId,
                                           section.halfWidth * 2.0, section.driftSpeedScale);
                    if (ctx.p.layers.surfaceCharge && isBody)
                        emitSurfaceCharge(ctx, section.start, section.end,
                                          section.voltageStart, section.voltageEnd, section.halfWidth * 2.0);
                }
            } else {
                // Inside a source the carriers move AGAINST the field, pushed
                // by the EMF; drawing E-arrows there reads as "everything
                // flows into the minus", so the source shows an EMF arrow
                // (- to +) instead.
                if (ctx.p.layers.electricField && comp.type != ComponentType::VoltageSource)
                    emitEFieldArrows(ctx, a, b, va, vb);
                if (ctx.p.layers.electricField && comp.type == ComponentType::VoltageSource) {
                    Vec2 emfDir = (comp.value >= 0.0 ? (a - b) : (b - a)).normalized();
                    Vec2 mid2((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
                    double sz = std::max(ctx.p.wireThickness * 1.2, 9.0);
                    ctx.out.arrows.push_back({mid2 - emfDir * (sz * 0.4), emfDir, sz,
                                              packColor(255, 196, 110, 235)});
                    ctx.out.labels.push_back({mid2 + Vec2(0, 0) + emfDir * (sz * 1.2), "EMF",
                                              packColor(255, 196, 110, 220), false});
                }
                if (ctx.p.layers.current && std::abs(branchCurrent) > 1e-12)
                    emitCurrentArrows(ctx, a, b, branchCurrent);
                if (ctx.p.layers.drift && std::abs(branchCurrent) > 1e-12)
                    emitDriftParticles(ctx, a, b, branchCurrent, comp.id);
                if (ctx.p.layers.surfaceCharge)
                    emitSurfaceCharge(ctx, a, b, va, vb);
            }

            if (ctx.p.layers.magnetic && std::abs(branchCurrent) > 1e-12 &&
                comp.type != ComponentType::Capacitor)
                emitMagneticField(ctx, a, b, branchCurrent);

            if (comp.type == ComponentType::Inductor)
                emitInductorPhysics(ctx, comp, a, b, branchCurrent);
        } else if (ctx.p.layers.current && std::abs(branchCurrent) > 1e-12) {
            emitCurrentArrows(ctx, a, b, branchCurrent);
        }

        emitReadoutLabels(ctx, comp, mid, perp, branchCurrent, branchPower);
    }

    emitJunctions(ctx);
    emitNodes(ctx);

    if (physicsLayers && ctx.solution && ctx.p.layers.potential && ctx.hasPotentialRange())
        ctx.out.legend = {true, ctx.vMin, ctx.vMax};
}

std::vector<ElementState> buildElements(const Circuit& circuit, const CircuitSolution* solution) {
    std::vector<ElementState> elements;
    elements.reserve(circuit.components.size());
    for (const auto& comp : circuit.components) {
        ElementState el;
        el.componentId = comp.id;
        el.type = comp.type;
        el.voltageA = potentialFor(solution, comp.nodeA);
        el.voltageB = potentialFor(solution, comp.nodeB);
        if (const BranchResult* branch = branchFor(solution, comp.id)) {
            el.current = branch->current;
            el.power = branch->power;
            if (comp.type == ComponentType::Capacitor)
                el.storedEnergy = 0.5 * comp.value * branch->voltageDrop * branch->voltageDrop;
            else if (comp.type == ComponentType::Inductor)
                el.storedEnergy = 0.5 * comp.value * branch->current * branch->current;
        }
        elements.push_back(el);
    }
    return elements;
}

} // namespace

ProjectionResult buildProjection(ProjectionKind kind,
                                 const Circuit& circuit,
                                 const CircuitSolution* solution,
                                 const ViewParams& params) {
    ProjectionResult result;
    result.elements = buildElements(circuit, solution);

    BuildContext ctx{circuit, solution, params, result.prims};
    computeRanges(ctx);

    switch (kind) {
        case ProjectionKind::Schematic:
            buildCircuitShapes(ctx, /*physicsLayers=*/false);
            break;
        case ProjectionKind::Physics:
            buildCircuitShapes(ctx, /*physicsLayers=*/true);
            break;
        case ProjectionKind::Mechanical:
            buildMechanics(ctx);
            break;
        case ProjectionKind::Hydraulic:
            buildHydraulic(ctx);
            break;
    }

    return result;
}

bool projectionHasComponent(const ProjectionResult& result, int componentId) {
    return projectionElement(result, componentId) != nullptr;
}

const ElementState* projectionElement(const ProjectionResult& result, int componentId) {
    for (const auto& el : result.elements) {
        if (el.componentId == componentId) return &el;
    }
    return nullptr;
}

} // namespace current_lab::projection
