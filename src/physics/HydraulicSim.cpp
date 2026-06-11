// ВАРИАНТ Б гидравлики — карантин, в приложение не подключён.
// Статус, ограничения и критерии удаления — в HydraulicSim.h.
#include "physics/HydraulicSim.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace current_lab::physics {

namespace {

constexpr float kToSim = 0.05f;
constexpr float kFromSim = 1.0f / kToSim;
constexpr float kSubStep = 1.0f / 60.0f;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxParticles = 60;
constexpr double kMaxSpeed = 50.0;

b2Vec2 toSim(Vec2 v) {
    return b2Vec2(static_cast<float>(v.x) * kToSim,
                  static_cast<float>(v.y) * kToSim);
}
Vec2 fromSim(b2Vec2 v) {
    return Vec2(v.x * kFromSim, v.y * kFromSim);
}

static uint64_t mixHash(uint64_t h, uint64_t v) { h ^= v; h *= 1099511628211ull; return h; }

} // namespace

struct HydraulicSim::Impl {
    std::unique_ptr<b2World> world;
    double pipeRadius = 6.0;
    double accumulator = 0.0;
    uint64_t signature = 0;
    double flowSpeed = 0.0; // signed, from solver

    std::vector<b2Body*> wallBodies;
    std::vector<b2Body*> paddleBodies;
    std::vector<b2Body*> particleBodies;

    struct LoopSegment {
        Vec2 a, b;
        int componentId = -1;
        bool resistor = false;
        bool source = false;
    };
    std::vector<LoopSegment> loops;

    // ---- loop detection ----
    struct GraphEdge {
        int nodeA, nodeB;
        int compId;
        ComponentType type;
        double current = 0.0;
    };

    static bool isConductive(ComponentType t) {
        return t == ComponentType::Wire || t == ComponentType::Resistor ||
               t == ComponentType::VoltageSource;
    }
    void buildLoop(const Circuit& circuit, const CircuitSolution* solution) {
        loops.clear();
        if (circuit.nodes.empty()) return;

        // Build edge list from conductive components.
        std::vector<GraphEdge> edges;
        for (const auto& comp : circuit.components) {
            if (!isConductive(comp.type)) continue;
            if (comp.type == ComponentType::Switch && comp.value < 0.5) continue;
            double cur = 0.0;
            if (solution) {
                for (const auto& br : solution->branches)
                    if (br.componentId == comp.id) { cur = br.current; break; }
            }
            edges.push_back({comp.nodeA, comp.nodeB, comp.id, comp.type, cur});
        }
        if (edges.empty()) return;

        // Build adjacency: nodeId -> list of (otherNode, edgeIndex).
        std::unordered_map<int, std::vector<std::pair<int, int>>> adj;
        for (int ei = 0; ei < (int)edges.size(); ++ei) {
            adj[edges[ei].nodeA].push_back({edges[ei].nodeB, ei});
            adj[edges[ei].nodeB].push_back({edges[ei].nodeA, ei});
        }

        int gnd = circuit.groundNodeId;
        if (gnd < 0 || adj.find(gnd) == adj.end()) {
            for (const auto& [nid, nb] : adj)
                if (nb.size() == 2) { gnd = nid; break; }
        }
        if (gnd < 0) return;

        // Traverse: store (edgeIndex, fromNode, toNode) in loop order.
        struct Hop { int ei; int fromNode; int toNode; };
        std::vector<Hop> hops;
        std::vector<bool> used(edges.size(), false);

        int cur = gnd;
        int prev = -1;

        auto pickFirst = [&](int nid) -> int {
            int best = -1;
            double bestCur = -1e9;
            for (auto [nb, ei] : adj[nid]) {
                if (used[ei]) continue;
                double absCur = std::abs(edges[ei].current);
                if (absCur > bestCur) { bestCur = absCur; best = ei; }
            }
            return best;
        };

        for (;;) {
            int ei = pickFirst(cur);
            if (ei < 0) break;
            used[ei] = true;
            const auto& e = edges[ei];
            int nxt = e.nodeA == cur ? e.nodeB : e.nodeA;
            hops.push_back({ei, cur, nxt});
            if (nxt == gnd) break;
            if (nxt == prev) break;
            prev = cur;
            cur = nxt;
        }

        if (!hops.empty()) {
            cur = hops.back().toNode;
            if (cur != gnd) {
                for (;;) {
                    int ei = pickFirst(cur);
                    if (ei < 0) break;
                    used[ei] = true;
                    const auto& e = edges[ei];
                    int nxt = e.nodeA == cur ? e.nodeB : e.nodeA;
                    hops.push_back({ei, cur, nxt});
                    if (nxt == gnd) break;
                    cur = nxt;
                    if (hops.size() > edges.size() * 2) break;
                }
            }
        }

        for (int ei = 0; ei < (int)edges.size(); ++ei) {
            if (!used[ei]) {
                const auto& e = edges[ei];
                hops.push_back({ei, e.nodeA, e.nodeB});
                used[ei] = true;
            }
        }

        for (const auto& h : hops) {
            const Node* nf = circuit.findNode(h.fromNode);
            const Node* nt = circuit.findNode(h.toNode);
            if (!nf || !nt) continue;
            const auto& e = edges[h.ei];
            loops.push_back({nf->position, nt->position, e.compId,
                             e.type == ComponentType::Resistor,
                             e.type == ComponentType::VoltageSource});
        }
    }

    // ---- Box2D pipe builder ----
    void buildWalls() {
        if (loops.empty()) return;

        const uintptr_t wallTag = 1;
        double gap = pipeRadius;
        double cornerStep = 0.3; // rad step for rounding corners

        // Collect all path points in order (start points + final end point).
        std::vector<Vec2> path;
        for (const auto& seg : loops) path.push_back(seg.a);
        if (!loops.empty()) path.push_back(loops.back().b);

        // Build walls: offset the path by ±gap.
        auto offsetPoints = [&](const std::vector<Vec2>& pts, double off, int sign) {
            std::vector<Vec2> out;
            int n = (int)pts.size();
            if (n < 2) return out;

            for (int i = 0; i < n; ++i) {
                Vec2 dir;
                if (i == 0)
                    dir = (pts[1] - pts[0]).normalized();
                else if (i == n - 1)
                    dir = (pts[n-1] - pts[n-2]).normalized();
                else
                    dir = ((pts[i+1] - pts[i]).normalized() +
                           (pts[i] - pts[i-1]).normalized()).normalized();
                Vec2 perp(-dir.y, dir.x);
                out.push_back(pts[i] + perp * off * sign);
                (void)cornerStep;
            }
            return out;
        };

        // Just build a simple wall body for both sides.
        auto buildSide = [&](const std::vector<Vec2>& pts) {
            if (pts.size() < 2) return;
            b2BodyDef bodyDef;
            b2Body* wall = world->CreateBody(&bodyDef);
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                b2EdgeShape edge;
                edge.SetTwoSided(toSim(pts[i]), toSim(pts[i+1]));
                b2FixtureDef fd;
                fd.shape = &edge;
                fd.friction = 0.0f;
                fd.restitution = 0.5f;
                fd.userData.pointer = wallTag;
                wall->CreateFixture(&fd);
            }
            wallBodies.push_back(wall);
        };

        // Join path as a closed loop: connect last to first.
        std::vector<Vec2> closed = path;
        closed.push_back(path[0]);

        auto inner = offsetPoints(closed, gap, -1); // inner wall
        auto outer = offsetPoints(closed, gap, +1); // outer wall

        buildSide(inner);
        buildSide(outer);

        // Close the gap at the seam by connecting inner/outer endpoints.
        for (size_t i = 0; i < loops.size(); ++i) {
            const auto& seg = loops[i];
            const auto& nextSeg = loops[(i + 1) % loops.size()];
            // Junction node: circular buffer region.
            Vec2 center = seg.b; // node position at junction
            double juncR = gap * 1.8;

            // Approximate circular boundary around the junction.
            b2BodyDef juncDef;
            b2Body* junc = world->CreateBody(&juncDef);
            int segs = 16;
            Vec2 prev;
            for (int s = 0; s <= segs; ++s) {
                double a = kPi * 2.0 * s / segs;
                Vec2 p = center + Vec2(std::cos(a), std::sin(a)) * juncR;
                if (s > 0) {
                    b2EdgeShape edge;
                    edge.SetTwoSided(toSim(prev), toSim(p));
                    b2FixtureDef fd;
                    fd.shape = &edge;
                    fd.friction = 0.02f;
                    fd.restitution = 0.1f;
                    fd.userData.pointer = wallTag;
                    junc->CreateFixture(&fd);
                }
                prev = p;
            }
            wallBodies.push_back(junc);
            (void)nextSeg;
        }
    }

    void seedParticles() {
        if (loops.empty()) return;
        for (int i = 0; i < kMaxParticles; ++i) {
            // Distribute along the loop.
            double t = static_cast<double>(i) / kMaxParticles;
            Vec2 pos = samplePath(t);
            b2BodyDef bodyDef;
            bodyDef.type = b2_dynamicBody;
            bodyDef.position = toSim(pos);
            bodyDef.fixedRotation = true;
            bodyDef.linearDamping = 0.3f;
            b2Body* body = world->CreateBody(&bodyDef);

            b2CircleShape circle;
            circle.m_radius = static_cast<float>(pipeRadius * 0.25) * kToSim;

            b2FixtureDef fd;
            fd.shape = &circle;
            fd.density = 1.0f;
            fd.friction = 0.0f;
            fd.restitution = 0.3f;
            fd.userData.pointer = 2;
            body->CreateFixture(&fd);
            particleBodies.push_back(body);
        }
    }

    double pathLength() const {
        double len = 0.0;
        for (const auto& seg : loops) len += (seg.b - seg.a).length();
        return len;
    }

    Vec2 samplePath(double t) const {
        t = std::fmod(t, 1.0);
        if (t < 0.0) t += 1.0;
        double total = pathLength();
        if (total < 1e-6) return {0, 0};
        double target = t * total;
        double acc = 0.0;
        for (const auto& seg : loops) {
            double segLen = (seg.b - seg.a).length();
            if (acc + segLen >= target || segLen < 1e-6)
                return seg.a + (seg.b - seg.a).normalized() * (target - acc);
            acc += segLen;
        }
        return loops.back().b;
    }

    void applyDrive() {
        if (loops.empty() || particleBodies.empty()) return;
        float target = static_cast<float>(flowSpeed) * kToSim;

        for (b2Body* body : particleBodies) {
            Vec2 pos = fromSim(body->GetPosition());
            // Find which segment the particle is on and get its tangent.
            Vec2 tangent = nearestTangent(pos);
            b2Vec2 tang(static_cast<float>(tangent.x), static_cast<float>(tangent.y));
            b2Vec2 vel = body->GetLinearVelocity();
            float vTang = b2Dot(vel, tang);

            float force = (target - vTang) * body->GetMass() * 25.0f;
            body->ApplyForceToCenter(b2Vec2(tang.x * force, tang.y * force), true);
        }
    }

    Vec2 nearestTangent(Vec2 pos) const {
        double bestDist = 1e12;
        Vec2 bestTangent{1, 0};
        for (const auto& seg : loops) {
            Vec2 ab = seg.b - seg.a;
            double len = ab.length();
            if (len < 0.5) continue;
            Vec2 unit = ab / len;

            double t = (pos.x - seg.a.x) * unit.x + (pos.y - seg.a.y) * unit.y;
            t = std::clamp(t, 0.0, len);
            Vec2 closest = seg.a + unit * t;
            double dx = pos.x - closest.x, dy = pos.y - closest.y;
            double dist = dx * dx + dy * dy;

            if (dist < bestDist) {
                bestDist = dist;
                bestTangent = unit;
            }
        }
        return bestTangent;
    }

    void buildPaddles() {
        for (const auto& seg : loops) {
            if (!seg.source) continue;
            Vec2 ab = seg.b - seg.a;
            double len = ab.length();
            if (len < 1.0) continue;
            Vec2 unit = ab / len;
            Vec2 perp(-unit.y, unit.x);
            Vec2 center = seg.a + unit * len * 0.5;

            // Paddle blade: a thin rectangular body rotating at center.
            double bladeLen = pipeRadius * 0.75;
            double bladeW = pipeRadius * 0.12;

            b2BodyDef bodyDef;
            bodyDef.type = b2_kinematicBody;
            bodyDef.position = toSim(center);
            b2Body* body = world->CreateBody(&bodyDef);

            b2PolygonShape blade;
            blade.SetAsBox(static_cast<float>(bladeLen) * kToSim,
                          static_cast<float>(bladeW) * kToSim);

            b2FixtureDef fd;
            fd.shape = &blade;
            fd.density = 1.0f;
            fd.friction = 0.8f;
            fd.restitution = 0.1f;
            fd.userData.pointer = 3;
            body->CreateFixture(&fd);

            paddleBodies.push_back(body);
        }
    }

    void rotatePaddles() {
        for (b2Body* body : paddleBodies) {
            float speed = static_cast<float>(flowSpeed * 0.15) * kToSim;
            body->SetAngularVelocity(speed);
        }
    }
};

uint64_t HydraulicSim::layoutSignature(const Circuit& circuit) {
    uint64_t hash = 14695981039346656037ull;
    for (const auto& comp : circuit.components) {
        hash = mixHash(hash, static_cast<uint64_t>(comp.id));
        hash = mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(comp.type)));
        hash = mixHash(hash, static_cast<uint64_t>(comp.nodeA));
        hash = mixHash(hash, static_cast<uint64_t>(comp.nodeB));
    }
    for (const auto& node : circuit.nodes) {
        hash = mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(node.position.x * 8)));
        hash = mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(node.position.y * 8)));
    }
    return hash;
}

HydraulicSim::HydraulicSim() : m_impl(std::make_unique<Impl>()) {}
HydraulicSim::~HydraulicSim() = default;

void HydraulicSim::configure(const Circuit& circuit, const CircuitSolution* solution,
                             double pipeRadius) {
    m_impl->world = std::make_unique<b2World>(b2Vec2(0, 0));
    m_impl->pipeRadius = pipeRadius;
    m_impl->accumulator = 0.0;
    m_impl->wallBodies.clear();
    m_impl->paddleBodies.clear();
    m_impl->particleBodies.clear();
    m_impl->loops.clear();

    m_impl->buildLoop(circuit, solution);
    m_impl->buildWalls();
    m_impl->buildPaddles();
    m_impl->seedParticles();
    m_impl->signature = layoutSignature(circuit);
    m_configured = !m_impl->loops.empty();

    // Set initial flow and snapshot.
    setFlow(circuit, solution);
    step(0.0);
}

void HydraulicSim::setFlow(const Circuit& circuit, const CircuitSolution* solution) {
    if (!m_impl->world) return;
    if (layoutSignature(circuit) != m_impl->signature) {
        configure(circuit, solution, m_impl->pipeRadius);
        return;
    }
    if (!solution) { m_impl->flowSpeed = 0.0; return; }
    double total = 0.0;
    int count = 0;
    for (const auto& br : solution->branches) {
        for (const auto& seg : m_impl->loops) {
            if (seg.componentId == br.componentId) {
                total += br.current;
                ++count;
                break;
            }
        }
    }
    if (count > 0) total /= count;
    m_impl->flowSpeed = std::clamp(total * 3000.0, -kMaxSpeed, kMaxSpeed);
}

void HydraulicSim::step(double dt) {
    if (!m_impl->world) return;
    m_impl->accumulator += std::min(dt, 0.1);
    int steps = 0;
    while (m_impl->accumulator >= kSubStep && steps < 12) {
        m_impl->applyDrive();
        m_impl->rotatePaddles();
        m_impl->world->Step(kSubStep, 8, 3);
        m_impl->accumulator -= kSubStep;
        ++steps;
    }

    // Extract particle data.
    m_particles.clear();
    for (b2Body* b : m_impl->particleBodies)
        m_particles.push_back({fromSim(b->GetPosition()), fromSim(b->GetLinearVelocity())});

    m_paddles.clear();
    for (size_t pi = 0; pi < m_impl->paddleBodies.size(); ++pi) {
        HydraulicPaddle p;
        p.angle = m_impl->paddleBodies[pi]->GetAngle();
        p.loopIndex = static_cast<int>(pi);
        // Find which source segment this paddle belongs to.
        int srcIdx = 0;
        for (const auto& seg : m_impl->loops) {
            if (seg.source) {
                if (srcIdx == static_cast<int>(pi)) {
                    p.componentId = seg.componentId;
                    break;
                }
                ++srcIdx;
            }
        }
        m_paddles.push_back(p);
    }
}

} // namespace current_lab::physics
