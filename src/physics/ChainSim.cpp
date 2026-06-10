#include "physics/ChainSim.h"

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

    void at(double t, Vec2* point, Vec2* tangent) const {
        double straight = len;
        double arc = kPi * off;
        t = std::fmod(t, perimeter());
        if (t < 0.0) t += perimeter();

        if (t < straight) { // top straight: a->b side at +off
            *point = a + unit * t + perp * off;
            *tangent = unit;
        } else if (t < straight + arc) { // arc around b
            double phi = (t - straight) / off; // 0..pi
            double angle = kPi * 0.5 - phi;    // from +perp to -perp around b
            *point = b + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
            *tangent = unit * std::sin(angle) - perp * std::cos(angle);
            // tangent of decreasing angle: d/dphi = (-cos, ... ) — normalize below
            Vec2 radial = (*point - b).normalized();
            *tangent = Vec2(-radial.y, radial.x) * -1.0; // clockwise around b
        } else if (t < 2.0 * straight + arc) { // bottom straight: b->a at -off
            double s = t - straight - arc;
            *point = b - unit * s - perp * off;
            *tangent = unit * -1.0;
        } else { // arc around a
            double phi = (t - 2.0 * straight - arc) / off;
            double angle = -kPi * 0.5 - phi;
            *point = a + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
            Vec2 radial = (*point - a).normalized();
            *tangent = Vec2(-radial.y, radial.x) * -1.0;
        }
    }

    // Inverse-ish: the param of the closest racetrack station (for brakes).
    double topParamOf(Vec2 p) const {
        Vec2 rel = p - a;
        return rel.x * unit.x + rel.y * unit.y; // along-axis coordinate
    }

    bool onTopStraight(Vec2 p) const {
        Vec2 rel = p - a;
        double lateral = rel.x * perp.x + rel.y * perp.y;
        return lateral > 0.0;
    }
};

struct Loop {
    ChainSpec spec;
    Oval oval;
    std::vector<b2Body*> bodies;
};

class LoopContactFilter : public b2ContactFilter {
public:
    bool ShouldCollide(b2Fixture* a, b2Fixture* b) override {
        return a->GetUserData().pointer == b->GetUserData().pointer;
    }
};

} // namespace

struct ChainSim::Impl {
    LoopContactFilter filter;
    std::unique_ptr<b2World> world;
    std::vector<Loop> loops;
    double linkRadius = 1.1;
    double accumulator = 0.0;
    uint64_t signature = 0;

    void buildLoop(const ChainSpec& spec) {
        Loop loop;
        loop.spec = spec;
        Vec2 ab = spec.b - spec.a;
        double len = ab.length();
        if (len < 8.0 * linkRadius) return;

        loop.oval.a = spec.a;
        loop.oval.b = spec.b;
        loop.oval.unit = ab / len;
        loop.oval.perp = Vec2(-loop.oval.unit.y, loop.oval.unit.x);
        loop.oval.len = len;
        loop.oval.off = std::max(spec.halfWidth * 0.55, linkRadius * 1.6);

        const uintptr_t tag = loops.size() + 1;

        // Guide rails: inner and outer racetrack walls (polyline of edges).
        double gap = linkRadius * 1.5;
        for (double railOff : {loop.oval.off - gap, loop.oval.off + gap}) {
            if (railOff < linkRadius) continue;
            b2BodyDef railDef;
            b2Body* rail = world->CreateBody(&railDef);
            Oval railOval = loop.oval;
            railOval.off = railOff;
            int segments = std::max(24, static_cast<int>(railOval.perimeter() / 6.0));
            Vec2 prev, tangent;
            railOval.at(0.0, &prev, &tangent);
            for (int i = 1; i <= segments; ++i) {
                Vec2 point;
                railOval.at(railOval.perimeter() * i / segments, &point, &tangent);
                b2EdgeShape edge;
                edge.SetTwoSided(toSim(prev), toSim(point));
                b2FixtureDef fixture;
                fixture.shape = &edge;
                fixture.friction = 0.05f;
                fixture.restitution = 0.1f;
                fixture.userData.pointer = tag;
                rail->CreateFixture(&fixture);
                prev = point;
            }
        }

        // Links: rigid bodies around the oval, jointed into a closed ring.
        double spacing = linkRadius * 2.6;
        int count = std::max(8, static_cast<int>(loop.oval.perimeter() / spacing));
        spacing = loop.oval.perimeter() / count; // exact ring closure

        for (int i = 0; i < count; ++i) {
            Vec2 point, tangent;
            loop.oval.at(spacing * i, &point, &tangent);
            b2BodyDef bodyDef;
            bodyDef.type = b2_dynamicBody;
            bodyDef.position = toSim(point);
            bodyDef.fixedRotation = true;
            bodyDef.linearDamping = 0.3f;
            b2Body* body = world->CreateBody(&bodyDef);
            b2CircleShape circle;
            circle.m_radius = static_cast<float>(linkRadius) * kToSim;
            b2FixtureDef fixture;
            fixture.shape = &circle;
            fixture.density = 1.0f;
            fixture.friction = 0.02f;
            fixture.restitution = 0.05f;
            fixture.userData.pointer = tag;
            body->CreateFixture(&fixture);
            loop.bodies.push_back(body);
        }

        // Rigid distance joints: the chain cannot stretch or compress.
        for (int i = 0; i < count; ++i) {
            b2Body* bodyA = loop.bodies[i];
            b2Body* bodyB = loop.bodies[(i + 1) % count];
            b2DistanceJointDef joint;
            joint.Initialize(bodyA, bodyB, bodyA->GetPosition(), bodyB->GetPosition());
            joint.minLength = joint.length * 0.95f;
            joint.maxLength = joint.length * 1.05f;
            joint.stiffness = 0.0f;
            world->CreateJoint(&joint);
        }

        loops.push_back(std::move(loop));
    }

    void applyDrive() {
        for (auto& loop : loops) {
            float target = static_cast<float>(loop.spec.targetSpeed) * kToSim;
            for (b2Body* body : loop.bodies) {
                Vec2 pos = fromSim(body->GetPosition());
                // Tangent at the nearest racetrack station: use the radial
                // trick — works on straights and arcs alike.
                Vec2 point, tangent;
                // cheap: recompute from the along-axis/lateral signs
                Vec2 rel = pos - loop.oval.a;
                double along = rel.x * loop.oval.unit.x + rel.y * loop.oval.unit.y;
                double lateral = rel.x * loop.oval.perp.x + rel.y * loop.oval.perp.y;
                if (along >= 0.0 && along <= loop.oval.len) {
                    tangent = lateral >= 0.0 ? loop.oval.unit : loop.oval.unit * -1.0;
                } else {
                    Vec2 center = along < 0.0 ? loop.oval.a : loop.oval.b;
                    Vec2 radial = (pos - center).normalized();
                    tangent = Vec2(-radial.y, radial.x) * -1.0;
                }
                (void)point;

                b2Vec2 vel = body->GetLinearVelocity();
                b2Vec2 tang(static_cast<float>(tangent.x), static_cast<float>(tangent.y));
                float vTang = b2Dot(vel, tang);

                // Friction brake on the resistor body: heavy damping the
                // drive must overcome (dissipation made visible).
                bool inBrake = loop.spec.brake && lateral > 0.0 &&
                               along > loop.oval.len * 0.32 && along < loop.oval.len * 0.68;
                body->SetLinearDamping(inBrake ? 5.0f : 0.3f);

                float force = (target - vTang) * body->GetMass() * 8.0f;
                body->ApplyForceToCenter(b2Vec2(tang.x * force, tang.y * force), true);
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
    m_impl->world->SetContactFilter(&m_impl->filter);
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
        m_impl->applyDrive();
        m_impl->world->Step(kSubStep, 6, 2);
        m_impl->accumulator -= kSubStep;
        ++steps;
    }
}

std::vector<ChainLink> ChainSim::links() const {
    std::vector<ChainLink> out;
    for (const auto& loop : m_impl->loops) {
        int size = static_cast<int>(loop.bodies.size());
        for (int i = 0; i < size; ++i) {
            out.push_back({fromSim(loop.bodies[i]->GetPosition()),
                           loop.spec.componentId, i, size});
        }
    }
    return out;
}

bool ChainSim::configured() const { return m_impl->world != nullptr; }

} // namespace current_lab::physics
