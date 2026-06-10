#include "physics/ParticleSim.h"
#include "physics/ChannelSpecs.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace current_lab::physics {

namespace {

// Box2D likes metre-scale numbers; the canvas works in ~100s of world units.
constexpr float kToSim = 0.05f;   // world units -> sim metres
constexpr float kFromSim = 1.0f / kToSim;
constexpr float kSubStep = 1.0f / 120.0f;
constexpr int kVelocityIterations = 16; // long contact chains: pump pressure must cross the whole loop
constexpr int kPositionIterations = 6;
constexpr double kPi = 3.14159265358979323846;

b2Vec2 toSim(Vec2 v) { return b2Vec2(static_cast<float>(v.x) * kToSim,
                                     static_cast<float>(v.y) * kToSim); }
Vec2 fromSim(b2Vec2 v) { return Vec2(v.x * kFromSim, v.y * kFromSim); }

struct Channel {
    ChannelSpec spec;
    Vec2 unit, perp;
    double length = 0.0;
    double trimA = 0.0, trimB = 0.0; // wall trim where a junction chamber begins
    std::vector<b2Body*> bodies; // particles of this channel
    b2Body* paddleBody = nullptr;
};

// A node where pipe mouths meet; the chamber walls close the gaps between
// the mouths so the network is water-tight.
struct Junction {
    Vec2 pos;
    double radius = 0.0;
    std::vector<std::pair<int, bool>> mouths; // (channel index, isEndA)
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
    std::vector<Junction> junctions;
    double particleRadius = 1.2;
    double accumulator = 0.0;
    uint64_t signature = 0;
    bool connectedMode = false; // water network: one collision domain

    void clear() {
        world.reset();
        channels.clear();
        junctions.clear();
    }

    // In connected mode every fixture shares one tag (the whole network is a
    // single collision domain); legacy mode keeps per-channel isolation.
    uintptr_t tagFor(size_t channelIndex) const {
        return connectedMode ? 1u : channelIndex + 1;
    }

    void buildChannel(const ChannelSpec& spec) {
        const uintptr_t channelTag = tagFor(channels.size());
        Channel channel;
        channel.spec = spec;
        Vec2 ab = spec.b - spec.a;
        channel.length = ab.length();
        if (channel.length < 4.0 * particleRadius) return;
        channel.unit = ab / channel.length;
        channel.perp = Vec2(-channel.unit.y, channel.unit.x);

        // Drude lattice: staggered bumps attached to the walls. The free
        // corridor past every bump is kept >= 1.4 particle diameters, so the
        // resistor is ALWAYS passable — particles squeeze through, they never
        // wall up (regression: frozen electrons in the resistor).
        if (spec.scatterers) {
            double start = channel.length * 0.32;
            double end = channel.length * 0.68;
            // Connected (water) mode keeps a wider corridor: dense granular
            // flow arches and clogs at narrow throats, electrons do not.
            double corridor = spec.connected ? 4.2 : 2.8;
            double maxR = (2.0 * spec.halfWidth - corridor * particleRadius) / 2.0;
            double baseFrac = spec.connected ? 0.32 : 0.45;
            double pillarR = std::clamp(spec.halfWidth * baseFrac, 0.6, std::max(0.6, maxR));
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
            // Connected (water) mode: paddle wheel offset into a casing pocket
            // — the exposed sweep above the axis entrains the flow, the casing
            // side blocks the back-flow short-circuit. Legacy: centered wheel.
            Vec2 mid = spec.connected
                ? pumpImpellerCenter(spec.a, spec.b, spec.halfWidth)
                : spec.a + channel.unit * (channel.length * 0.5);
            b2BodyDef paddleDef;
            paddleDef.type = b2_kinematicBody;
            paddleDef.position = toSim(mid);
            channel.paddleBody = world->CreateBody(&paddleDef);
            double bladeLen = spec.connected ? pumpImpellerRadius(spec.halfWidth)
                                             : spec.halfWidth * 0.85;
            int blades = spec.connected ? 4 : 3;
            double bladeHalfTh = spec.connected ? particleRadius * 0.6 : particleRadius * 0.45;
            for (int blade = 0; blade < blades; ++blade) {
                b2PolygonShape box;
                float angle = static_cast<float>(blade) * (2.0f * b2_pi / blades);
                box.SetAsBox(static_cast<float>(bladeLen) * kToSim,
                             static_cast<float>(bladeHalfTh) * kToSim,
                             b2Vec2(0, 0), angle);
                b2FixtureDef fixture;
                fixture.shape = &box;
                fixture.friction = 0.4f;
                fixture.restitution = 0.25f;
                fixture.userData.pointer = channelTag;
                channel.paddleBody->CreateFixture(&fixture);
            }
        }

        // Particles. Legacy: sparse markers along the axis. Connected (water):
        // densely packed grid — water FILLS the pipe and pressure propagates
        // through particle contacts.
        double usableHalf = std::max(0.0, spec.halfWidth - particleRadius - 0.3);
        int count;
        if (spec.connected) {
            // Dense enough that contact chains form and pressure propagates
            // from the pump around the whole loop (incompressibility).
            double area = channel.length * 2.0 * spec.halfWidth;
            double particleArea = kPi * particleRadius * particleRadius;
            count = std::clamp(static_cast<int>(area * 0.62 / particleArea), 6, 320);
        } else {
            count = std::clamp(static_cast<int>(channel.length * spec.halfWidth /
                                                (particleRadius * particleRadius * 26.0)),
                               4, 60);
        }
        if (spec.seedParticles >= 0)
            count = spec.seedParticles;

        int rows = spec.connected
            ? std::max(1, static_cast<int>(std::floor(usableHalf / (particleRadius * 1.05))))
            : 1;
        for (int i = 0; i < count; ++i) {
            double t, lateral;
            if (spec.connected) {
                // Row-major grid with a half-step stagger per row (hex-ish).
                int cols = (count + rows - 1) / rows;
                int row = i % rows;
                int col = i / rows;
                t = (col + 0.5 + 0.5 * (row % 2)) / (cols + 1) * channel.length;
                lateral = rows > 1
                    ? -usableHalf + 2.0 * usableHalf * row / (rows - 1)
                    : 0.0;
            } else {
                t = (i + 0.5) / count * channel.length;
                lateral = usableHalf * std::sin(i * 2.39996); // golden-angle spread
            }
            Vec2 pos = spec.a + channel.unit * t + channel.perp * lateral;

            b2BodyDef bodyDef;
            bodyDef.type = b2_dynamicBody;
            bodyDef.position = toSim(pos);
            bodyDef.fixedRotation = true;
            bodyDef.linearDamping = spec.connected ? 0.10f : 0.4f;
            b2Body* body = world->CreateBody(&bodyDef);

            b2CircleShape circle;
            circle.m_radius = static_cast<float>(particleRadius) * kToSim;
            b2FixtureDef fixture;
            fixture.shape = &circle;
            fixture.density = 1.0f;
            fixture.friction = spec.connected ? 0.02f : 0.05f;
            fixture.restitution = spec.connected ? 0.05f : 0.4f;
            fixture.userData.pointer = channelTag;
            body->CreateFixture(&fixture);
            channel.bodies.push_back(body);
        }

        channels.push_back(std::move(channel));
    }

    // --- water-network plumbing -------------------------------------------------

    // Channel walls: two static edges along the axis, trimmed where junction
    // chambers begin (trim = 0 in legacy mode).
    void addWalls(size_t channelIndex) {
        Channel& channel = channels[channelIndex];
        const ChannelSpec& spec = channel.spec;
        b2BodyDef wallDef;
        b2Body* walls = world->CreateBody(&wallDef);
        Vec2 startBase = spec.a + channel.unit * channel.trimA;
        Vec2 endBase = spec.b - channel.unit * channel.trimB;
        for (int side : {1, -1}) {
            b2EdgeShape edge;
            Vec2 p0 = startBase + channel.perp * (spec.halfWidth * side);
            Vec2 p1 = endBase + channel.perp * (spec.halfWidth * side);
            edge.SetTwoSided(toSim(p0), toSim(p1));
            b2FixtureDef fixture;
            fixture.shape = &edge;
            fixture.friction = 0.05f;
            fixture.restitution = spec.connected ? 0.05f : 0.35f;
            fixture.userData.pointer = tagFor(channelIndex);
            walls->CreateFixture(&fixture);
        }
        // Legacy mode keeps open ends (teleport wrap handles them); connected
        // dead ends are capped by the single-mouth junction arc instead.
    }

    // Group the BUILT channels' ends by circuit node and size the chambers.
    void buildJunctions() {
        std::map<int, Junction> byNode;
        for (size_t i = 0; i < channels.size(); ++i) {
            const ChannelSpec& spec = channels[i].spec;
            for (bool atA : {true, false}) {
                int node = atA ? spec.nodeA : spec.nodeB;
                if (node < 0) continue;
                Junction& j = byNode[node];
                j.pos = atA ? spec.a : spec.b;
                j.radius = std::max(j.radius, junctionRadius(spec.halfWidth));
                j.mouths.push_back({static_cast<int>(i), atA});
            }
        }
        junctions.clear();
        for (auto& [node, j] : byNode) {
            for (auto [ci, atA] : j.mouths) {
                if (atA) channels[ci].trimA = j.radius;
                else channels[ci].trimB = j.radius;
            }
            junctions.push_back(std::move(j));
        }
    }

    // Close the chamber boundary between neighbouring pipe mouths with static
    // polyline arcs, endpoints exactly on the trimmed wall corners (no leaks).
    void addJunctionWalls(const Junction& j) {
        struct Mouth {
            double angle;
            Vec2 cornerCCW, cornerCW;
        };
        std::vector<Mouth> mouths;
        for (auto [ci, atA] : j.mouths) {
            const Channel& ch = channels[ci];
            Vec2 dir = atA ? ch.unit : ch.unit * -1.0;
            Vec2 left(-dir.y, dir.x);
            double hw = ch.spec.halfWidth;
            Mouth m;
            m.angle = std::atan2(dir.y, dir.x);
            m.cornerCCW = j.pos + dir * j.radius + left * hw;
            m.cornerCW = j.pos + dir * j.radius - left * hw;
            mouths.push_back(m);
        }
        std::sort(mouths.begin(), mouths.end(),
                  [](const Mouth& a, const Mouth& b) { return a.angle < b.angle; });

        b2BodyDef bodyDef;
        b2Body* body = world->CreateBody(&bodyDef);
        const size_t k = mouths.size();
        for (size_t i = 0; i < k; ++i) {
            Vec2 from = mouths[i].cornerCCW;
            Vec2 to = mouths[(i + 1) % k].cornerCW;
            double a0 = std::atan2(from.y - j.pos.y, from.x - j.pos.x);
            double a1 = std::atan2(to.y - j.pos.y, to.x - j.pos.x);
            double sweep = a1 - a0;
            while (sweep <= 0.0) sweep += 2.0 * kPi;
            // Overlapping mouths (nearly parallel pipes): a wall here would
            // block the other mouth — leave the sliver open.
            if (k >= 2 && sweep > 2.0 * kPi - 0.35) continue;
            double r0 = (from - j.pos).length();
            double r1 = (to - j.pos).length();
            int segments = std::max(3, static_cast<int>(sweep / 0.35));
            Vec2 prev = from;
            for (int s = 1; s <= segments; ++s) {
                double frac = static_cast<double>(s) / segments;
                double angle = a0 + sweep * frac;
                double r = r0 + (r1 - r0) * frac;
                Vec2 point = s == segments
                    ? to
                    : j.pos + Vec2(std::cos(angle), std::sin(angle)) * r;
                b2EdgeShape edge;
                edge.SetTwoSided(toSim(prev), toSim(point));
                b2FixtureDef fixture;
                fixture.shape = &edge;
                fixture.friction = 0.05f;
                fixture.restitution = 0.05f;
                fixture.userData.pointer = 1u; // connected mode: shared domain
                body->CreateFixture(&fixture);
                prev = point;
            }
        }
    }

    uint64_t stepCount = 0;

    void applyDriveForces() {
        uint32_t noiseSeed = static_cast<uint32_t>(stepCount * 2246822519u);
        size_t bodyIndex = 0;
        for (auto& channel : channels) {
            b2Vec2 axis = toSim(channel.unit);
            axis.Normalize();
            float target = static_cast<float>(channel.spec.targetSpeed) * kToSim;
            // Connected (water) mode: the PUMP is the cause of motion; the
            // per-channel drive is only a weak assist that calibrates the mean
            // drift to the solver current and overcomes numerical friction.
            float gain = channel.spec.connected ? 1.5f : 6.0f;
            for (b2Body* body : channel.bodies) {
                ++bodyIndex;
                if (channel.spec.connected) {
                    // Thermal agitation: isotropic, zero-mean pseudo-random
                    // kicks act like molecular pressure — they refill the void
                    // behind the pump wheel (suction side) and break granular
                    // arches at constrictions. No net direction: circulation
                    // can only come from the impeller (or the assist field).
                    uint32_t h = static_cast<uint32_t>(bodyIndex) * 2654435761u ^ noiseSeed;
                    h ^= h >> 16;
                    h *= 2246822519u;
                    float angle = static_cast<float>(h % 6283u) * 0.001f;
                    float kick = body->GetMass() * 18.0f * kToSim;
                    body->ApplyForceToCenter(
                        b2Vec2(std::cos(angle) * kick, std::sin(angle) * kick), true);

                    // No field force inside junction chambers.
                    Vec2 rel = fromSim(body->GetPosition()) - channel.spec.a;
                    double t = rel.x * channel.unit.x + rel.y * channel.unit.y;
                    if (t < channel.trimA || t > channel.length - channel.trimB)
                        continue;
                }
                b2Vec2 vel = body->GetLinearVelocity();
                float along = b2Dot(vel, axis);
                // Proportional drive toward the calibrated drift speed.
                float force = (target - along) * body->GetMass() * gain;
                body->ApplyForceToCenter(b2Vec2(axis.x * force, axis.y * force), true);
            }
            if (channel.paddleBody)
                channel.paddleBody->SetAngularVelocity(
                    static_cast<float>(channel.spec.paddleSpeed));
        }
        ++stepCount;
    }

    // Connected mode: particles cross junctions PHYSICALLY; here we only keep
    // the bookkeeping (which channel owns which body, for rendering and the
    // assist axis) and rescue the rare runaway that tunnels out of the pipes.
    void flowOwnership() {
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            Channel& channel = channels[ci];
            for (size_t bi = 0; bi < channel.bodies.size(); ++bi) {
                b2Body* body = channel.bodies[bi];
                Vec2 pos = fromSim(body->GetPosition());
                Vec2 rel = pos - channel.spec.a;
                double t = rel.x * channel.unit.x + rel.y * channel.unit.y;
                double lateral = rel.x * channel.perp.x + rel.y * channel.perp.y;

                bool insideOwn = t >= -particleRadius &&
                                 t <= channel.length + particleRadius &&
                                 std::abs(lateral) <= channel.spec.halfWidth + particleRadius;
                if (insideOwn) continue;

                // Entered another pipe? Hand the bookkeeping over.
                int newOwner = -1;
                for (size_t oi = 0; oi < channels.size() && newOwner < 0; ++oi) {
                    if (oi == ci) continue;
                    Channel& other = channels[oi];
                    Vec2 orel = pos - other.spec.a;
                    double ot = orel.x * other.unit.x + orel.y * other.unit.y;
                    double olat = orel.x * other.perp.x + orel.y * other.perp.y;
                    if (ot >= 0.0 && ot <= other.length &&
                        std::abs(olat) <= other.spec.halfWidth - particleRadius * 0.2)
                        newOwner = static_cast<int>(oi);
                }
                if (newOwner >= 0) {
                    channels[newOwner].bodies.push_back(body);
                    channel.bodies.erase(channel.bodies.begin() + bi);
                    --bi;
                    continue;
                }

                // Inside a junction chamber: legitimate transit, keep owner.
                bool inJunction = false;
                for (const auto& j : junctions) {
                    if ((pos - j.pos).length() <= j.radius + particleRadius * 2.0) {
                        inJunction = true;
                        break;
                    }
                }
                if (inJunction) continue;

                // Escaped the plumbing entirely (tunnelled): put it back into
                // its own pipe, keeping the along-axis station.
                double tFix = std::clamp(t, particleRadius, channel.length - particleRadius);
                double latFix = std::clamp(lateral,
                                           -(channel.spec.halfWidth - particleRadius),
                                           channel.spec.halfWidth - particleRadius);
                Vec2 fixedPos = channel.spec.a + channel.unit * tFix + channel.perp * latFix;
                body->SetTransform(toSim(fixedPos), 0.0f);
            }
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

                double speed = fromSim(body->GetLinearVelocity()).length();
                double lateralFrac = maxLat > 1e-9 ? std::clamp(lateral / maxLat, -1.0, 1.0) : 0.0;
                if (exitNode >= 0) {
                    int next = pickOutgoing(exitNode, static_cast<int>(ci));
                    if (next >= 0) {
                        Channel& to = channels[next];
                        relocate(body, to, lateralFrac, std::max(speed, 2.0));
                        to.bodies.push_back(body);
                        channel.bodies.erase(channel.bodies.begin() + bi);
                        --bi;
                        continue;
                    }
                }

                bool moved = false;
                if (t > channel.length || t < 0.0) {
                    // wrap within the same channel (either dead-end node or no node defined)
                    t = t > channel.length ? t - channel.length : t + channel.length;
                    moved = true;
                }
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
        mix(spec.connected ? 17u : 11u);
        // Junction plumbing depends on which nodes the pipes share.
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.nodeA) + 1));
        mix(static_cast<uint64_t>(static_cast<int64_t>(spec.nodeB) + 1));
    }
    return hash;
}

void ParticleSim::configure(const std::vector<ChannelSpec>& channels, double particleRadius) {
    m_impl->clear();
    m_impl->particleRadius = std::max(0.5, particleRadius);
    m_impl->connectedMode = !channels.empty() && channels.front().connected;
    m_impl->world = std::make_unique<b2World>(b2Vec2(0.0f, 0.0f));
    m_impl->world->SetContactFilter(&m_impl->contactFilter);
    for (const auto& spec : channels)
        m_impl->buildChannel(spec);
    if (m_impl->connectedMode) {
        // One plumbing network: trim the walls at shared nodes, then close
        // the junction chambers so the system is water-tight.
        m_impl->buildJunctions();
        for (size_t i = 0; i < m_impl->channels.size(); ++i)
            m_impl->addWalls(i);
        for (const auto& junction : m_impl->junctions)
            m_impl->addJunctionWalls(junction);
    } else {
        for (size_t i = 0; i < m_impl->channels.size(); ++i)
            m_impl->addWalls(i);
    }
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
        if (m_impl->connectedMode)
            m_impl->flowOwnership(); // physical transit, bookkeeping only
        else
            m_impl->wrapParticles(); // legacy teleport wrap/transfer
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
