#include "physics/ParticleSim.h"
#include "physics/ChannelSpecs.h"
#include "physics/ResistiveElementModel.h"

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
// 1/120 обязателен для качества: пробовали 1/90 и 1/60 — контакты плотной воды
// не успевают разрешаться у насоса (пары проседают >0.5 радиуса, падает
// WaterBallsStayEssentiallyIncompressible). Главный груз воды — подшаги×тела;
// дешевле НЕ сделать без потери качества → выносить сим в поток, не резать шаг.
constexpr float kSubStep = 1.0f / 120.0f;
// 10 velocity (было 16): после камерного ассиста длинные контактные цепочки —
// не единственный переносчик давления. Позиционных — 6: на 4 шарики
// просачиваются сквозь рёбра труб (находка пользователя). Несжимаемость/поток
// закреплены WaterNetwork.*.
constexpr int kVelocityIterations = 10;
constexpr int kPositionIterations = 6;
// kPi приходит из PhysicalUnits.h (через ResistiveElementModel.h).

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
        // ELECTRON world only: for charges the Drude lattice IS the resistance
        // metaphor. Water uses a smooth venturi throat instead (addWalls /
        // hydraulicThrottle) — pillars at the walls let balls slip around them
        // by the opposite wall and ride outside the drawn funnel (user 2026-06-13).
        if (spec.scatterers && !spec.connected) {
            // Pillars sit ONLY under the drawn resistive body
            // (resistorBodySpan == the ResistiveBody section of
            // resistorPathSections); the leads are drawn as plain wire and
            // must not hide obstacles.
            AxialSpan span = resistorBodySpan(channel.length, spec.halfWidth * 2.0);
            double start = span.start;
            double end = span.end;
            // Electron world only (water gets the smooth venturi throat, not
            // pillars): the free corridor past every staggered bump stays 2.8
            // particle radii so charges always squeeze through and never wall up.
            double corridor = 2.8;
            double maxR = (2.0 * spec.halfWidth - corridor * particleRadius) / 2.0;
            double baseFrac = 0.45;
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

        int rows = 1;
        int cols = count;
        if (spec.connected) {
            // Use the whole pipe cross-section. The previous formula used
            // only half-width and produced a single centreline row for the
            // default 8 wu pipe, so the "water" looked like a sparse trickle.
            double rowPitch = particleRadius * 2.0 * 1.03;
            rows = std::max(2, static_cast<int>(std::floor((usableHalf * 2.0) / rowPitch)) + 1);
            double colPitch = particleRadius * 2.0 * 1.04;
            cols = std::max(1, static_cast<int>(std::floor(channel.length / colPitch)));
            count = std::min(count, rows * cols);
        }
        for (int i = 0; i < count; ++i) {
            double t, lateral;
            if (spec.connected) {
                // Row-major grid with a half-step stagger per row (hex-ish).
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
            // Water resistor: a grid row that falls outside the necked venturi
            // wall is SKIPPED, not squeezed to the wall — clamping would stack
            // several rows on the throat centreline, and Box2D blasting the
            // overlap apart turns the throat into a contact hotspot (3x slower
            // step). Skipping thins the throat to its real capacity.
            if (spec.scatterers && spec.connected) {
                HydraulicThrottle th = hydraulicThrottle(channel.length, spec.halfWidth);
                double localUsable = std::max(0.0, th.halfWidthAt(t) - particleRadius - 0.3);
                if (std::abs(lateral) > localUsable) continue;
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
            // Вода скользкая (находка пользователя: «шарики плотно
            // забиваются — сделай их скользкими»): нулевое трение шарик-шарик
            // и шарик-стенка/столбик (mix = sqrt(f1*f2) = 0), арки у горловин
            // распадаются сами.
            fixture.friction = spec.connected ? 0.0f : 0.05f;
            fixture.restitution = spec.connected ? 0.05f : 0.4f;
            fixture.userData.pointer = channelTag;
            body->CreateFixture(&fixture);
            if (spec.connected && std::abs(spec.targetSpeed) > 1e-9) {
                Vec2 initialVel = channel.unit * spec.targetSpeed;
                body->SetLinearVelocity(toSim(initialVel));
            }
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
        double t0 = channel.trimA;
        double t1 = channel.length - channel.trimB;
        if (t1 <= t0) return;

        // Water resistor = venturi throat: the walls follow the SAME profile the
        // renderer draws (hydraulicThrottle), so water can never flow in a wider
        // channel than the throat necked around it (user finding 2026-06-13).
        // Other channels: two straight edges along the axis.
        bool funnel = spec.scatterers && spec.connected;
        HydraulicThrottle th = funnel ? hydraulicThrottle(channel.length, spec.halfWidth)
                                      : HydraulicThrottle{};
        auto halfAt = [&](double t) { return funnel ? th.halfWidthAt(t) : spec.halfWidth; };

        // Axial stations: ends, plus the funnel breakpoints (piecewise-linear
        // profile, so vertices at the breakpoints reproduce it exactly).
        std::vector<double> ts = {t0};
        if (funnel) {
            for (double s : {th.leadIn, th.throatStart, th.throatEnd, th.leadOut}) {
                double cs = std::clamp(s, t0, t1);
                if (std::abs(cs - ts.back()) > 1e-6) ts.push_back(cs);
            }
        }
        if (std::abs(t1 - ts.back()) > 1e-6) ts.push_back(t1);

        for (int side : {1, -1}) {
            for (size_t k = 0; k + 1 < ts.size(); ++k) {
                Vec2 p0 = spec.a + channel.unit * ts[k]     + channel.perp * (halfAt(ts[k]) * side);
                Vec2 p1 = spec.a + channel.unit * ts[k + 1] + channel.perp * (halfAt(ts[k + 1]) * side);
                b2EdgeShape edge;
                edge.SetTwoSided(toSim(p0), toSim(p1));
                b2FixtureDef fixture;
                fixture.shape = &edge;
                fixture.friction = 0.05f;
                fixture.restitution = spec.connected ? 0.05f : 0.35f;
                fixture.userData.pointer = tagFor(channelIndex);
                walls->CreateFixture(&fixture);
            }
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

    std::vector<double> pumpPressureTargets() const {
        std::vector<double> targets(channels.size(), 0.0);
        if (!connectedMode) return targets;

        for (size_t pumpIndex = 0; pumpIndex < channels.size(); ++pumpIndex) {
            const Channel& pump = channels[pumpIndex];
            if (!pump.paddleBody || std::abs(pump.spec.paddleSpeed) <= 1e-6)
                continue;

            double pumpFlow = -pump.spec.paddleSpeed * 10.0;
            if (std::abs(pumpFlow) <= 1e-6) continue;

            targets[pumpIndex] = pumpFlow;
            int node = pumpFlow >= 0.0 ? pump.spec.nodeB : pump.spec.nodeA;
            int prev = static_cast<int>(pumpIndex);
            double speed = std::abs(pumpFlow) * 0.85;

            for (size_t step = 0; step < channels.size(); ++step) {
                int next = -1;
                for (size_t i = 0; i < channels.size(); ++i) {
                    if (static_cast<int>(i) == prev) continue;
                    const auto& spec = channels[i].spec;
                    if (spec.nodeA == node || spec.nodeB == node) {
                        next = static_cast<int>(i);
                        break;
                    }
                }
                if (next < 0) break;

                const auto& spec = channels[next].spec;
                double sign = spec.nodeA == node ? 1.0 : -1.0;
                targets[next] = sign * speed;
                node = sign > 0.0 ? spec.nodeB : spec.nodeA;
                prev = next;

                if (next == static_cast<int>(pumpIndex))
                    break;
            }
        }

        return targets;
    }

    // Chamber assist: a particle inside a node fitting gets a gentle pull
    // toward the core of the pipe its flow continues into, so fittings are
    // flow-through and no teleporting is needed. Scales with the calibrated
    // channel speed: zero current — water rests, the pump-only mode is not
    // faked (circulation may only come from the impeller or the assist).
    void steerThroughChamber(b2Body* body, int ownChannel, int nodeId, Vec2 pos) {
        const Channel& own = channels[ownChannel];
        int exitNode = own.spec.targetSpeed >= 0.0 ? own.spec.nodeB
                                                   : own.spec.nodeA;
        int targetChannel = ownChannel;
        if (nodeId == exitNode) {
            // Leaving the own pipe: continue into the strongest outgoing pipe.
            int best = -1;
            double bestSpeed = 0.0;
            for (size_t i = 0; i < channels.size(); ++i) {
                if (static_cast<int>(i) == ownChannel) continue;
                const auto& spec = channels[i].spec;
                int entry = spec.targetSpeed >= 0.0 ? spec.nodeA : spec.nodeB;
                if (entry != nodeId) continue;
                if (best < 0 || std::abs(spec.targetSpeed) > std::abs(bestSpeed)) {
                    best = static_cast<int>(i);
                    bestSpeed = spec.targetSpeed;
                }
            }
            if (best < 0) return; // dead end: the chamber arc holds the water
            targetChannel = best;
        }

        const Channel& to = channels[targetChannel];
        double speed = std::abs(to.spec.targetSpeed);
        if (speed < 1e-9) {
            // При нулевом токе мягко возвращаем частицу к оси канала,
            // чтобы она не застревала в камере узла.
            Vec2 relTo = pos - to.spec.a;
            double latTo = relTo.x * to.perp.x + relTo.y * to.perp.y;
            Vec2 centering = to.perp * (-latTo * 0.5);
            b2Vec2 vel = body->GetLinearVelocity();
            float gain = 1.5f;
            body->ApplyForceToCenter(
                b2Vec2((static_cast<float>(centering.x * kToSim) - vel.x) * body->GetMass() * gain,
                       (static_cast<float>(centering.y * kToSim) - vel.y) * body->GetMass() * gain),
                true);
            return;
        }

        // Funnel field, not a point target (converging on one spot makes the
        // crowd jam at the mouth and the flow arrive in bursts): the axial
        // component carries the stream into the pipe, a lateral component
        // bends it toward the pipe axis so the whole mouth width feeds.
        bool forward = to.spec.targetSpeed >= 0.0;
        Vec2 axisDir = forward ? to.unit : to.unit * -1.0;
        Vec2 relTo = pos - to.spec.a;
        double latTo = relTo.x * to.perp.x + relTo.y * to.perp.y;
        double centering =
            std::clamp(-latTo / std::max(1.0, to.spec.halfWidth), -1.0, 1.0);
        Vec2 desired = axisDir + to.perp * (centering * 0.8);
        double norm = desired.length();
        if (norm < 1e-9) return;
        Vec2 want = desired / norm * speed;

        b2Vec2 vel = body->GetLinearVelocity();
        // Stronger than the in-pipe assist (1.5): the fitting has no pressure
        // gradient of its own, and an underfed mouth makes the flow downstream
        // arrive in bursts (temporal-uniformity test). But not too strong:
        // overfeeding hardens granular arches at the throats downstream.
        float gain = 2.0f;
        body->ApplyForceToCenter(
            b2Vec2((static_cast<float>(want.x * kToSim) - vel.x) * body->GetMass() * gain,
                   (static_cast<float>(want.y * kToSim) - vel.y) * body->GetMass() * gain),
            true);
    }

    void applyDriveForces() {
        std::vector<double> pressureTargets = pumpPressureTargets();
        uint32_t noiseSeed = static_cast<uint32_t>(stepCount * 2246822519u);
        size_t bodyIndex = 0;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            auto& channel = channels[ci];
            b2Vec2 axis = toSim(channel.unit);
            axis.Normalize();
            double effectiveTarget = channel.spec.targetSpeed;
            if (channel.spec.connected && std::abs(effectiveTarget) < 1e-9)
                effectiveTarget = pressureTargets[ci];
            // Idle channel (no current, no spinning pump): no kicks, no
            // assist — the bodies settle and Box2D puts the island to sleep,
            // so still water costs (almost) nothing instead of burning a core
            // on noise that wakes every body each substep.
            bool active = std::abs(effectiveTarget) > 1e-9 ||
                          std::abs(channel.spec.paddleSpeed) > 1e-6;
            if (!active) {
                bodyIndex += channel.bodies.size();
                if (channel.paddleBody)
                    channel.paddleBody->SetAngularVelocity(0.0f);
                continue;
            }
            float target = static_cast<float>(effectiveTarget) * kToSim;
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

                    // No axis field inside junction chambers — but the water
                    // must keep moving THROUGH the fitting: steer it toward
                    // the mouth of the pipe it is headed into. (The previous
                    // teleport handoff either crushed balls into the packed
                    // pipe or, gated on free spots, released them in bursts.)
                    Vec2 pos = fromSim(body->GetPosition());
                    Vec2 rel = pos - channel.spec.a;
                    double t = rel.x * channel.unit.x + rel.y * channel.unit.y;
                    if (t < channel.trimA || t > channel.length - channel.trimB) {
                        int nodeId = t < channel.trimA ? channel.spec.nodeA
                                                       : channel.spec.nodeB;
                        steerThroughChamber(body, static_cast<int>(ci), nodeId, pos);
                        continue;
                    }
                }
                b2Vec2 vel = body->GetLinearVelocity();
                float along = b2Dot(vel, axis);
                // Proportional drive toward the calibrated drift speed.
                float force = (target - along) * body->GetMass() * gain;
                body->ApplyForceToCenter(b2Vec2(axis.x * force, axis.y * force), true);

                if (channel.paddleBody && std::abs(channel.spec.paddleSpeed) > 1e-6) {
                    Vec2 pos = fromSim(body->GetPosition());
                    Vec2 center = fromSim(channel.paddleBody->GetPosition());
                    Vec2 relW = pos - center;
                    double axial = relW.x * channel.unit.x + relW.y * channel.unit.y;
                    double lateral = relW.x * channel.perp.x + relW.y * channel.perp.y;
                    double influenceR = pumpImpellerRadius(channel.spec.halfWidth) +
                                        particleRadius * 2.5;
                    if (std::abs(axial) <= influenceR &&
                        std::abs(lateral) <= channel.spec.halfWidth + particleRadius) {
                        // A rotating blade is a local pump, not a global
                        // velocity clamp. Positive omega must drive negative
                        // a->b flow (see pumpOmegaForFlow).
                        float pumpTarget = static_cast<float>(
                            -channel.spec.paddleSpeed * 10.0 * kToSim);
                        float pumpForce = (pumpTarget - along) * body->GetMass() * 12.0f;
                        body->ApplyForceToCenter(
                            b2Vec2(axis.x * pumpForce, axis.y * pumpForce), true);

                        float churn = static_cast<float>(
                            std::sin(stepCount * 0.37 + bodyIndex * 1.91) *
                            std::abs(channel.spec.paddleSpeed) * 0.9 * kToSim);
                        b2Vec2 side = toSim(channel.perp);
                        side.Normalize();
                        body->ApplyForceToCenter(
                            b2Vec2(side.x * churn * body->GetMass(),
                                   side.y * churn * body->GetMass()), true);
                    }
                }
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
                if (inJunctionChamber(pos)) continue;

                // Escaped the plumbing entirely (tunnelled): put it back into
                // its own pipe, keeping the along-axis station. Prefer a free
                // disc (no crushing), but a ball may NEVER linger outside the
                // pipes (user-visible leak through the wall) — the last
                // candidate is taken even if tight; contacts then relax it.
                double tFix = std::clamp(t, particleRadius, channel.length - particleRadius);
                double latFix = std::clamp(lateral,
                                           -(channel.spec.halfWidth - particleRadius),
                                           channel.spec.halfWidth - particleRadius);
                const double offsets[] = {0.0, 2.2, -2.2, 4.4, -4.4, 6.6, -6.6, 8.8};
                Vec2 lastResort = channel.spec.a + channel.unit * tFix +
                                  channel.perp * latFix;
                bool placed = false;
                for (double off : offsets) {
                    double tTry = std::clamp(tFix + off * particleRadius,
                                             particleRadius,
                                             channel.length - particleRadius);
                    Vec2 fixedPos = channel.spec.a + channel.unit * tTry +
                                    channel.perp * latFix;
                    if (!spotIsFree(fixedPos, body)) continue;
                    body->SetTransform(toSim(fixedPos), 0.0f);
                    placed = true;
                    break;
                }
                if (!placed)
                    body->SetTransform(toSim(lastResort), 0.0f);
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
            double roll = (pickState >> 16) / 65536.0;
            if (roll < weight / totalWeight) best = static_cast<int>(i);
        }
        return best;
    }

    uint32_t pickState = 12345u;

    // Any teleport into densely packed water must land on a FREE disc —
    // materialising inside another ball crushes the pair ~1.5 radii deep
    // (user finding: «область сжатия, утрамбовываются на полрадиуса»).
    class FreeSpotQuery : public b2QueryCallback {
    public:
        b2Vec2 center;
        const b2Body* self = nullptr;
        float minDist = 0.0f;
        bool occupied = false;
        bool ReportFixture(b2Fixture* fixture) override {
            const b2Body* other = fixture->GetBody();
            if (other == self || other->GetType() != b2_dynamicBody) return true;
            if ((other->GetPosition() - center).Length() < minDist) {
                occupied = true;
                return false;
            }
            return true;
        }
    };

    bool spotIsFree(Vec2 pos, const b2Body* self) {
        FreeSpotQuery query;
        query.center = toSim(pos);
        query.self = self;
        query.minDist = static_cast<float>(particleRadius * 1.9 * kToSim);
        b2AABB box;
        b2Vec2 extent(query.minDist, query.minDist);
        box.lowerBound = query.center - extent;
        box.upperBound = query.center + extent;
        world->QueryAABB(&query, box);
        return !query.occupied;
    }

    bool inJunctionChamber(Vec2 pos) const {
        for (const auto& j : junctions)
            if ((pos - j.pos).length() <= j.radius + particleRadius * 2.0)
                return true;
        return false;
    }

    // Returns false (and leaves the body untouched) when every candidate
    // entry spot is occupied — the particle stays in the chamber and the
    // contacts push it through on a later substep instead of a crush.
    bool relocate(b2Body* body, Channel& to, double lateralFrac, double speed,
                  bool requireFreeSpot) {
        bool forward = to.spec.targetSpeed >= 0.0;
        double entryT = forward ? particleRadius * 1.5 : to.length - particleRadius * 1.5;
        double maxLat = std::max(0.0, to.spec.halfWidth - particleRadius - 0.3);
        const double fracs[] = {lateralFrac, 0.0, 0.5, -0.5, 0.9, -0.9};
        for (double frac : fracs) {
            Vec2 pos = to.spec.a + to.unit * entryT + to.perp * (frac * maxLat);
            if (requireFreeSpot && !spotIsFree(pos, body)) continue;
            Vec2 vel = to.unit * (forward ? speed : -speed);
            body->SetTransform(toSim(pos), 0.0f);
            body->SetLinearVelocity(b2Vec2(static_cast<float>(vel.x * kToSim),
                                           static_cast<float>(vel.y * kToSim)));
            return true;
        }
        return false;
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

                bool connected = channel.spec.connected;
                // Connected water transits fittings PHYSICALLY: the chamber
                // assist (steerThroughChamber) pushes it into the next pipe and
                // flowOwnership re-tags it by position. A teleport handoff here
                // either crushes balls into the packed pipe (the user-visible
                // «область сжатия») or, gated on free spots, releases bursts.
                if (connected && inJunctionChamber(pos)) continue;

                int exitNode = -1;
                if (t > channel.length) exitNode = channel.spec.nodeB;
                else if (t < 0.0) exitNode = channel.spec.nodeA;

                double speed = fromSim(body->GetLinearVelocity()).length();
                double lateralFrac = maxLat > 1e-9 ? std::clamp(lateral / maxLat, -1.0, 1.0) : 0.0;
                if (exitNode >= 0 && !connected) {
                    int next = pickOutgoing(exitNode, static_cast<int>(ci));
                    if (next >= 0) {
                        Channel& to = channels[next];
                        relocate(body, to, lateralFrac, std::max(speed, 2.0),
                                 /*requireFreeSpot=*/false);
                        to.bodies.push_back(body);
                        channel.bodies.erase(channel.bodies.begin() + bi);
                        --bi;
                        continue;
                    }
                }

                bool moved = false;
                if ((t > channel.length || t < 0.0) && !connected) {
                    // wrap within the same channel (either dead-end node or no
                    // node defined). Connected pipes never wrap onto themselves:
                    // out-of-plumbing escapees are rescued by flowOwnership.
                    t = t > channel.length ? t - channel.length : t + channel.length;
                    moved = true;
                }
                if (std::abs(lateral) > maxLat) {
                    lateral = std::clamp(lateral, -maxLat, maxLat);
                    moved = true;
                }
                if (moved) {
                    Vec2 fixedPos = channel.spec.a + channel.unit * t + channel.perp * lateral;
                    if (connected && !spotIsFree(fixedPos, body)) continue;
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

void ParticleSim::setTargets(const std::vector<ChannelSpec>& channels, double particleRadius) {
    if (!m_impl->world) return;
    double newRadius = std::max(0.5, particleRadius);
    if (layoutSignature(channels) != m_impl->signature) {
        configure(channels, newRadius);
        return;
    }
    m_impl->particleRadius = newRadius;
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
        if (!m_impl->connectedMode)
            m_impl->wrapParticles(); // legacy teleport wrap/transfer
        m_impl->accumulator -= kSubStep;
        ++steps;
    }
    if (m_impl->connectedMode && steps > 0) {
        // Bookkeeping, not physics (ownership re-tags, escapee rescue, chamber
        // skips) — once per frame is enough and it is O(bodies x channels).
        m_impl->flowOwnership();
        m_impl->wrapParticles();
    }
}

std::vector<SimParticle> ParticleSim::particles() const {
    std::vector<SimParticle> out;
    for (const auto& channel : m_impl->channels) {
        for (const b2Body* body : channel.bodies) {
            SimParticle particle;
            particle.id = static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(body));
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
