#include "physics/ParticleSim.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace current_lab::physics {

namespace {

// Box2D likes metre-scale numbers; the canvas works in ~100s of world units.
constexpr float kToSim = 0.05f;   // world units -> sim metres
constexpr float kFromSim = 1.0f / kToSim;
constexpr float kSubStep = 1.0f / 120.0f;
constexpr int kVelocityIterations = 6;
constexpr int kPositionIterations = 2;

b2Vec2 toSim(Vec2 v) { return b2Vec2(static_cast<float>(v.x) * kToSim,
                                     static_cast<float>(v.y) * kToSim); }
Vec2 fromSim(b2Vec2 v) { return Vec2(v.x * kFromSim, v.y * kFromSim); }

struct Channel {
    ChannelSpec spec;
    Vec2 unit, perp;
    double length = 0.0;
    std::vector<b2Body*> bodies; // particles of this channel
    b2Body* paddleBody = nullptr;
};

} // namespace

// Fixtures carry their channel index in userData; bodies of different
// channels never collide, so walls of one pipe cannot block another pipe
// crossing it at a junction.
class ChannelContactFilter : public b2ContactFilter {
public:
    bool ShouldCollide(b2Fixture* a, b2Fixture* b) override {
        return a->GetUserData().pointer == b->GetUserData().pointer;
    }
};

struct ParticleSim::Impl {
    ChannelContactFilter contactFilter;
    std::unique_ptr<b2World> world;
    std::vector<Channel> channels;
    double particleRadius = 1.2;
    double accumulator = 0.0;
    uint64_t signature = 0;

    void clear() {
        world.reset();
        channels.clear();
    }

    void buildChannel(const ChannelSpec& spec) {
        const uintptr_t channelTag = channels.size() + 1;
        Channel channel;
        channel.spec = spec;
        Vec2 ab = spec.b - spec.a;
        channel.length = ab.length();
        if (channel.length < 4.0 * particleRadius) return;
        channel.unit = ab / channel.length;
        channel.perp = Vec2(-channel.unit.y, channel.unit.x);

        double wallOffset = spec.halfWidth;

        // Channel walls: two static edges along the axis.
        b2BodyDef wallDef;
        b2Body* walls = world->CreateBody(&wallDef);
        for (int side : {1, -1}) {
            b2EdgeShape edge;
            Vec2 p0 = spec.a + channel.perp * (wallOffset * side);
            Vec2 p1 = spec.b + channel.perp * (wallOffset * side);
            edge.SetTwoSided(toSim(p0), toSim(p1));
            b2FixtureDef fixture;
            fixture.shape = &edge;
            fixture.friction = 0.05f;
            fixture.restitution = 0.35f;
            fixture.userData.pointer = channelTag;
            walls->CreateFixture(&fixture);
        }

        // Drude lattice: staggered bumps attached to the walls. The free
        // corridor past every bump is kept >= 1.4 particle diameters, so the
        // resistor is ALWAYS passable — particles squeeze through, they never
        // wall up (regression: frozen electrons in the resistor).
        if (spec.scatterers) {
            double start = channel.length * 0.32;
            double end = channel.length * 0.68;
            double maxR = (2.0 * spec.halfWidth - 2.8 * particleRadius) / 2.0;
            double pillarR = std::clamp(spec.halfWidth * 0.45, 0.6, std::max(0.6, maxR));
            double stepAlong = std::max(pillarR * 3.0, particleRadius * 3.2);
            int row = 0;
            for (double t = start; t <= end; t += stepAlong, ++row) {
                double lateral = (row % 2 == 0) ? -(spec.halfWidth - pillarR * 0.6)
                                                : (spec.halfWidth - pillarR * 0.6);
                Vec2 center = spec.a + channel.unit * t + channel.perp * lateral;
                b2BodyDef pillarDef;
                pillarDef.position = toSim(center);
                b2Body* pillar = world->CreateBody(&pillarDef);
                b2CircleShape circle;
                circle.m_radius = static_cast<float>(pillarR) * kToSim;
                b2FixtureDef fixture;
                fixture.shape = &circle;
                fixture.friction = 0.2f;
                fixture.restitution = 0.3f;
                fixture.userData.pointer = channelTag;
                pillar->CreateFixture(&fixture);
            }
        }

        // Pump / crank impeller: kinematic body with blades, spun by setTargets.
        if (spec.paddle) {
            Vec2 mid = spec.a + channel.unit * (channel.length * 0.5);
            b2BodyDef paddleDef;
            paddleDef.type = b2_kinematicBody;
            paddleDef.position = toSim(mid);
            channel.paddleBody = world->CreateBody(&paddleDef);
            double bladeLen = spec.halfWidth * 0.85;
            for (int blade = 0; blade < 3; ++blade) {
                b2PolygonShape box;
                float angle = static_cast<float>(blade) * (2.0f * b2_pi / 3.0f);
                box.SetAsBox(static_cast<float>(bladeLen) * kToSim,
                             static_cast<float>(particleRadius * 0.45) * kToSim,
                             b2Vec2(0, 0), angle);
                b2FixtureDef fixture;
                fixture.shape = &box;
                fixture.friction = 0.4f;
                fixture.restitution = 0.25f;
                fixture.userData.pointer = channelTag;
                channel.paddleBody->CreateFixture(&fixture);
            }
        }

        // Particles: spaced along the channel with lateral jitter; Box2D keeps
        // them from overlapping after that.
        double usableHalf = std::max(0.0, spec.halfWidth - particleRadius - 0.3);
        int count = std::clamp(static_cast<int>(channel.length * spec.halfWidth /
                                                (particleRadius * particleRadius * 26.0)),
                               4, 60);
        if (spec.seedParticles >= 0)
            count = spec.seedParticles;
        for (int i = 0; i < count; ++i) {
            double t = (i + 0.5) / count * channel.length;
            double lateral = usableHalf * std::sin(i * 2.39996); // golden-angle spread
            Vec2 pos = spec.a + channel.unit * t + channel.perp * lateral;

            b2BodyDef bodyDef;
            bodyDef.type = b2_dynamicBody;
            bodyDef.position = toSim(pos);
            bodyDef.fixedRotation = true;
            bodyDef.linearDamping = 0.4f;
            b2Body* body = world->CreateBody(&bodyDef);

            b2CircleShape circle;
            circle.m_radius = static_cast<float>(particleRadius) * kToSim;
            b2FixtureDef fixture;
            fixture.shape = &circle;
            fixture.density = 1.0f;
            fixture.friction = 0.05f;
            fixture.restitution = 0.4f;
            fixture.userData.pointer = channelTag;
            body->CreateFixture(&fixture);
            channel.bodies.push_back(body);
        }

        channels.push_back(std::move(channel));
    }

    void applyDriveForces() {
        for (auto& channel : channels) {
            b2Vec2 axis = toSim(channel.unit);
            axis.Normalize();
            float target = static_cast<float>(channel.spec.targetSpeed) * kToSim;
            for (b2Body* body : channel.bodies) {
                b2Vec2 vel = body->GetLinearVelocity();
                float along = b2Dot(vel, axis);
                // Proportional drive toward the calibrated drift speed.
                float force = (target - along) * body->GetMass() * 6.0f;
                body->ApplyForceToCenter(b2Vec2(axis.x * force, axis.y * force), true);
            }
            if (channel.paddleBody)
                channel.paddleBody->SetAngularVelocity(
                    static_cast<float>(channel.spec.paddleSpeed));
        }
    }

    // Node-buffer transfer: a particle that leaves a channel through a node
    // continues into ANOTHER channel of that node (weighted by |flow|), so
    // the stream is continuous across the whole circuit. With no outgoing
    // channel (dead end) it falls back to wrapping within its own channel.
    int pickOutgoing(int nodeId, int exceptChannel) {
        double totalWeight = 0.0;
        int best = -1;
        for (size_t i = 0; i < channels.size(); ++i) {
            if (static_cast<int>(i) == exceptChannel) continue;
            const auto& spec = channels[i].spec;
            int entryNode = spec.targetSpeed >= 0.0 ? spec.nodeA : spec.nodeB;
            if (entryNode != nodeId) continue;
            double weight = std::abs(spec.targetSpeed) + 0.05;
            totalWeight += weight;
            // weighted reservoir pick, deterministic enough via running hash
            pickState = pickState * 1664525u + 1013904223u;
            double roll = (pickState % 10000u) / 10000.0;
            if (roll < weight / totalWeight) best = static_cast<int>(i);
        }
        return best;
    }

    uint32_t pickState = 12345u;

    void relocate(b2Body* body, Channel& to, double lateralFrac, double speed) {
        bool forward = to.spec.targetSpeed >= 0.0;
        double entryT = forward ? particleRadius * 1.5 : to.length - particleRadius * 1.5;
        double maxLat = std::max(0.0, to.spec.halfWidth - particleRadius - 0.3);
        Vec2 pos = to.spec.a + to.unit * entryT + to.perp * (lateralFrac * maxLat);
        Vec2 vel = to.unit * (forward ? speed : -speed);
        body->SetTransform(toSim(pos), 0.0f);
        body->SetLinearVelocity(b2Vec2(static_cast<float>(vel.x * kToSim),
                                       static_cast<float>(vel.y * kToSim)));
    }

    void wrapParticles() {
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            Channel& channel = channels[ci];
            double maxLat = std::max(0.0, channel.spec.halfWidth - particleRadius * 0.5);
            for (size_t bi = 0; bi < channel.bodies.size(); ++bi) {
                b2Body* body = channel.bodies[bi];
                Vec2 pos = fromSim(body->GetPosition());
                Vec2 rel = pos - channel.spec.a;
                double t = rel.x * channel.unit.x + rel.y * channel.unit.y;
                double lateral = rel.x * channel.perp.x + rel.y * channel.perp.y;

                int exitNode = -1;
                if (t > channel.length) exitNode = channel.spec.nodeB;
                else if (t < 0.0) exitNode = channel.spec.nodeA;

                if (exitNode >= 0) {
                    int next = pickOutgoing(exitNode, static_cast<int>(ci));
                    double speed = fromSim(body->GetLinearVelocity()).length();
                    double lateralFrac = maxLat > 1e-9 ? std::clamp(lateral / maxLat, -1.0, 1.0) : 0.0;
                    if (next >= 0) {
                        // hand the particle over to the next pipe
                        Channel& to = channels[next];
                        relocate(body, to, lateralFrac, std::max(speed, 2.0));
                        to.bodies.push_back(body);
                        channel.bodies.erase(channel.bodies.begin() + bi);
                        --bi;
                        continue;
                    }
                    // dead end: wrap within the channel (legacy behaviour)
                    t = t > channel.length ? t - channel.length : t + channel.length;
                }

                bool moved = exitNode >= 0;
                if (std::abs(lateral) > maxLat) {
                    lateral = std::clamp(lateral, -maxLat, maxLat);
                    moved = true;
                }
                if (moved) {
                    Vec2 fixedPos = channel.spec.a + channel.unit * t + channel.perp * lateral;
                    body->SetTransform(toSim(fixedPos), 0.0f);
                }
            }
        }
    }
};

ParticleSim::ParticleSim() : m_impl(std::make_unique<Impl>()) {}
ParticleSim::~ParticleSim() = default;

uint64_t ParticleSim::layoutSignature(const std::vector<ChannelSpec>& channels) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ull;
    };
    for (const auto& spec : channels) {
        mix(static_cast<uint64_t>(spec.componentId));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.a.x * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.a.y * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.b.x * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.b.y * 8)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.halfWidth * 8)));
        mix(spec.scatterers ? 7u : 3u);
        mix(spec.paddle ? 13u : 5u);
    }
    return hash;
}

void ParticleSim::configure(const std::vector<ChannelSpec>& channels, double particleRadius) {
    m_impl->clear();
    m_impl->particleRadius = std::max(0.5, particleRadius);
    m_impl->world = std::make_unique<b2World>(b2Vec2(0.0f, 0.0f));
    m_impl->world->SetContactFilter(&m_impl->contactFilter);
    for (const auto& spec : channels)
        m_impl->buildChannel(spec);
    m_impl->signature = layoutSignature(channels);
    m_impl->accumulator = 0.0;
}

void ParticleSim::setTargets(const std::vector<ChannelSpec>& channels) {
    if (!m_impl->world) return;
    if (layoutSignature(channels) != m_impl->signature) {
        configure(channels, m_impl->particleRadius);
        return;
    }
    size_t i = 0;
    for (const auto& spec : channels) {
        // channels vector skips too-short specs; match by componentId.
        while (i < m_impl->channels.size() &&
               m_impl->channels[i].spec.componentId != spec.componentId)
            ++i;
        if (i >= m_impl->channels.size()) break;
        m_impl->channels[i].spec.targetSpeed = spec.targetSpeed;
        m_impl->channels[i].spec.paddleSpeed = spec.paddleSpeed;
        ++i;
    }
}

void ParticleSim::step(double dt) {
    if (!m_impl->world) return;
    m_impl->accumulator += std::min(dt, 0.1); // never spiral after a hitch
    int steps = 0;
    while (m_impl->accumulator >= kSubStep && steps < 8) {
        m_impl->applyDriveForces();
        m_impl->world->Step(kSubStep, kVelocityIterations, kPositionIterations);
        m_impl->wrapParticles();
        m_impl->accumulator -= kSubStep;
        ++steps;
    }
}

std::vector<SimParticle> ParticleSim::particles() const {
    std::vector<SimParticle> out;
    for (const auto& channel : m_impl->channels) {
        for (const b2Body* body : channel.bodies) {
            SimParticle particle;
            particle.pos = fromSim(body->GetPosition());
            particle.vel = fromSim(body->GetLinearVelocity());
            particle.componentId = channel.spec.componentId;
            out.push_back(particle);
        }
    }
    return out;
}

std::vector<PaddleState> ParticleSim::paddles() const {
    std::vector<PaddleState> out;
    for (const auto& channel : m_impl->channels) {
        if (!channel.paddleBody) continue;
        out.push_back({channel.spec.componentId,
                       static_cast<double>(channel.paddleBody->GetAngle())});
    }
    return out;
}

bool ParticleSim::configured() const { return m_impl->world != nullptr; }

} // namespace current_lab::physics
