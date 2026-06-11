#include "physics/ChainSim.h"

#include "projection/MechanicsMapping.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>

namespace current_lab::physics {

namespace {

constexpr float kToSim = 0.05f;
constexpr float kFromSim = 1.0f / kToSim;
constexpr float kSubStep = 1.0f / 120.0f;
constexpr double kPi = 3.14159265358979323846;

b2Vec2 toSim(Vec2 v) { return b2Vec2(static_cast<float>(v.x) * kToSim,
                                     static_cast<float>(v.y) * kToSim); }
Vec2 fromSim(b2Vec2 v) { return Vec2(v.x * kFromSim, v.y * kFromSim); }

// Oval racetrack around the segment a->b at distance `off`:
// param t in [0, perimeter) -> point + tangent (counter-clockwise).
struct Oval {
    Vec2 a, b, unit, perp;
    double len = 0.0, off = 0.0;

    double perimeter() const { return 2.0 * len + 2.0 * kPi * off; }

    Vec2 pointAt(double t) const {
        double straight = len;
        double arc = kPi * off;
        t = std::fmod(t, perimeter());
        if (t < 0.0) t += perimeter();

        if (t < straight)
            return a + unit * t + perp * off;
        if (t < straight + arc) {
            double phi = (t - straight) / off;
            double angle = kPi * 0.5 - phi;
            return b + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
        }
        if (t < 2.0 * straight + arc) {
            double s = t - straight - arc;
            return b - unit * s - perp * off;
        }
        double phi = (t - 2.0 * straight - arc) / off;
        double angle = -kPi * 0.5 - phi;
        return a + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
    }

    Vec2 tangentAt(double t) const {
        double straight = len;
        double arc = kPi * off;
        t = std::fmod(t, perimeter());
        if (t < 0.0) t += perimeter();

        if (t < straight)
            return unit;
        if (t < straight + arc) {
            Vec2 pt = pointAt(t);
            Vec2 radial = (pt - b).normalized();
            return Vec2(-radial.y, radial.x) * -1.0;
        }
        if (t < 2.0 * straight + arc)
            return unit * -1.0;
        Vec2 pt = pointAt(t);
        Vec2 radial = (pt - a).normalized();
        return Vec2(-radial.y, radial.x) * -1.0;
    }

    // Fraction along the top straight where the brake zone falls.
    double topStraightFraction() const {
        double perim = perimeter();
        return perim > 0.0 ? len / perim : 0.0;
    }
};

struct BodyState {
    b2Body* body = nullptr;
    double t = 0.0;
};

struct KinematicLoop {
    ChainSpec spec;
    Oval oval;
    std::vector<BodyState> bodies;
    double perimeter = 0.0;
    double step = 0.0;
};

} // namespace

struct ChainSim::Impl {
    std::unique_ptr<b2World> world;
    std::vector<KinematicLoop> loops;
    double linkRadius = 1.1;
    double accumulator = 0.0;
    uint64_t signature = 0;

    void buildLoop(const ChainSpec& spec) {
        KinematicLoop loop;
        loop.spec = spec;
        Vec2 ab = spec.b - spec.a;
        double len = ab.length();
        if (len < 8.0 * linkRadius) return;

        loop.oval.a = spec.a;
        loop.oval.b = spec.b;
        loop.oval.unit = ab / len;
        loop.oval.perp = Vec2(-loop.oval.unit.y, loop.oval.unit.x);
        loop.oval.len = len;
        loop.oval.off = mechanics::kChainOrbitRadius;
        loop.perimeter = loop.oval.perimeter();

        double spacing = linkRadius * 2.6;
        int count = std::max(8, static_cast<int>(loop.perimeter / spacing));
        loop.step = loop.perimeter / count;

        for (int i = 0; i < count; ++i) {
            double t = i * loop.step;
            Vec2 point = loop.oval.pointAt(t);

            b2BodyDef bodyDef;
            bodyDef.type = b2_kinematicBody;
            bodyDef.position = toSim(point);
            bodyDef.fixedRotation = true;
            b2Body* body = world->CreateBody(&bodyDef);

            b2CircleShape circle;
            circle.m_radius = static_cast<float>(linkRadius) * kToSim;
            b2FixtureDef fixture;
            fixture.shape = &circle;
            fixture.density = 0.0f;
            fixture.friction = 0.0f;
            body->CreateFixture(&fixture);

            loop.bodies.push_back({body, t});
        }

        loops.push_back(std::move(loop));
    }

    void advanceKinematic(double dt) {
        for (auto& loop : loops) {
            double target = loop.spec.targetSpeed;
            if (target == 0.0) continue;

            double effective = target;
            if (loop.spec.brake) {
                double brakeFrac = loop.oval.topStraightFraction() * 0.36;
                effective = target * (1.0 - 0.80 * brakeFrac);
            }

            double dT = effective * dt;
            for (auto& bs : loop.bodies) {
                bs.t += dT;
                bs.t = std::fmod(bs.t, loop.perimeter);
                if (bs.t < 0.0) bs.t += loop.perimeter;

                Vec2 nextPos = loop.oval.pointAt(bs.t);
                b2Vec2 cur = bs.body->GetPosition();
                b2Vec2 nxt = toSim(nextPos);
                float invDt = static_cast<float>(1.0 / dt);
                bs.body->SetLinearVelocity({(nxt.x - cur.x) * invDt,
                                            (nxt.y - cur.y) * invDt});
            }
        }
    }
};

ChainSim::ChainSim() : m_impl(std::make_unique<Impl>()) {}
ChainSim::~ChainSim() = default;

uint64_t ChainSim::layoutSignature(const std::vector<ChainSpec>& specs) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t v) { hash ^= v; hash *= 1099511628211ull; };
    for (const auto& spec : specs) {
        mix(static_cast<uint64_t>(spec.componentId));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.a.x * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.a.y * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.b.x * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.b.y * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.halfWidth * 8)));
        mix(spec.brake ? 7u : 3u);
    }
    return hash;
}

void ChainSim::configure(const std::vector<ChainSpec>& specs, double linkRadius) {
    m_impl->world = std::make_unique<b2World>(b2Vec2(0, 0));
    m_impl->loops.clear();
    m_impl->linkRadius = std::max(0.5, linkRadius);
    for (const auto& spec : specs)
        m_impl->buildLoop(spec);
    m_impl->signature = layoutSignature(specs);
    m_impl->accumulator = 0.0;
}

void ChainSim::setTargets(const std::vector<ChainSpec>& specs) {
    if (!m_impl->world) return;
    if (layoutSignature(specs) != m_impl->signature) {
        configure(specs, m_impl->linkRadius);
        return;
    }
    size_t i = 0;
    for (const auto& spec : specs) {
        while (i < m_impl->loops.size() &&
               m_impl->loops[i].spec.componentId != spec.componentId)
            ++i;
        if (i >= m_impl->loops.size()) break;
        m_impl->loops[i].spec.targetSpeed = spec.targetSpeed;
        ++i;
    }
}

void ChainSim::step(double dt) {
    if (!m_impl->world) return;
    m_impl->accumulator += std::min(dt, 0.1);
    int steps = 0;
    while (m_impl->accumulator >= kSubStep && steps < 8) {
        m_impl->advanceKinematic(kSubStep);
        m_impl->world->Step(kSubStep, 0, 0);
        m_impl->accumulator -= kSubStep;
        ++steps;
    }
}

std::vector<ChainLink> ChainSim::links() const {
    std::vector<ChainLink> out;
    for (const auto& loop : m_impl->loops) {
        int size = static_cast<int>(loop.bodies.size());
        for (int i = 0; i < size; ++i) {
            out.push_back({fromSim(loop.bodies[i].body->GetPosition()),
                           loop.spec.componentId, i, size});
        }
    }
    return out;
}

bool ChainSim::configured() const { return m_impl->world != nullptr; }

} // namespace current_lab::physics