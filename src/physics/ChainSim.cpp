#include "physics/ChainSim.h"
#include "physics/ChainGeometry.h"

#include <algorithm>
#include <cmath>

// The chain is fully guided: link positions come straight from the loop phase
// on the oval racetrack (zero drift by construction). The earlier Box2D body
// ring (rails, distance joints, drive forces) was stepped and then every
// transform was overwritten by the guided positions anyway — pure CPU waste
// at 120 Hz; it is gone. Same positions, same API, no physics world.
namespace current_lab::physics {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBrakeDamping = 5.0; // 1/с — коэффициент вязкого торможения (∝ скорости)

// Oval racetrack around the segment a->b at distance `off`:
// param t in [0, perimeter) -> point (counter-clockwise).
struct Oval {
    Vec2 a, b, unit, perp;
    double len = 0.0, off = 0.0;
    chain_geometry::SourceDrivePath sourceDrive;

    double perimeter() const {
        return sourceDrive.valid ? sourceDrive.perimeter : 2.0 * len + 2.0 * kPi * off;
    }

    Vec2 at(double t) const {
        t = std::fmod(t, perimeter());
        if (t < 0.0) t += perimeter();

        if (sourceDrive.valid)
            return chain_geometry::sourceDrivePointAt(sourceDrive, t);

        double straight = len;
        double arc = kPi * off;
        if (t < straight) // top straight: a->b side at +off
            return a + unit * t + perp * off;
        if (t < straight + arc) { // arc around b
            double phi = (t - straight) / off; // 0..pi
            double angle = kPi * 0.5 - phi;    // from +perp to -perp around b
            return b + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
        }
        if (t < 2.0 * straight + arc) { // bottom straight: b->a at -off
            double s = t - straight - arc;
            return b - unit * s - perp * off;
        }
        double phi = (t - 2.0 * straight - arc) / off; // arc around a
        double angle = -kPi * 0.5 - phi;
        return a + perp * (off * std::sin(angle)) + unit * (off * std::cos(angle));
    }
};

struct Loop {
    ChainSpec spec;
    Oval oval;
    int count = 0;
    double phase = 0.0;
    double spacing = 0.0;
};

} // namespace

struct ChainSim::Impl {
    std::vector<Loop> loops;
    double linkRadius = 1.1;
    bool configured = false;
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
        // The loop arcs around each node exactly on the sprocket pitch circle,
        // so the simulated chain stays on the drawn gear teeth.
        loop.oval.off = chain_geometry::sprocketPitchRadius(spec.halfWidth, linkRadius);
        if (spec.driveSprocket) {
            double driveR =
                chain_geometry::driveSprocketPitchRadius(spec.halfWidth, linkRadius);
            loop.oval.sourceDrive =
                chain_geometry::sourceDrivePath(spec.a, spec.b, loop.oval.off, driveR);
        }

        // Links: spaced by the chain pitch, ring closed exactly.
        double spacing = chain_geometry::linkPitch(linkRadius);
        loop.count = std::max(8, static_cast<int>(loop.oval.perimeter() / spacing));
        loop.spacing = loop.oval.perimeter() / loop.count;

        loops.push_back(std::move(loop));
    }

    void advance(double dt) {
        for (auto& loop : loops) {
            double speed = loop.spec.targetSpeed;
            if (loop.spec.brake)
                speed *= std::max(0.0, 1.0 - kBrakeDamping * dt); // вязкое торможение ∝ скорости
            loop.phase += speed * dt;

            double p = loop.oval.perimeter();
            loop.phase = std::fmod(loop.phase, p);
            if (loop.phase < 0.0) loop.phase += p;
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
        mix(spec.driveSprocket ? 13u : 5u);
    }
    return hash;
}

void ChainSim::configure(const std::vector<ChainSpec>& specs, double linkRadius) {
    m_impl->loops.clear();
    m_impl->linkRadius = std::max(0.5, linkRadius);
    for (const auto& spec : specs)
        m_impl->buildLoop(spec);
    m_impl->signature = layoutSignature(specs);
    m_impl->configured = true;
}

void ChainSim::setTargets(const std::vector<ChainSpec>& specs) {
    if (!m_impl->configured) return;
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
    if (!m_impl->configured) return;
    m_impl->advance(std::min(dt, 0.1));
}

std::vector<ChainLink> ChainSim::links() const {
    std::vector<ChainLink> out;
    for (const auto& loop : m_impl->loops) {
        for (int i = 0; i < loop.count; ++i) {
            Vec2 point = loop.oval.at(loop.phase + loop.spacing * static_cast<double>(i));
            out.push_back({point, loop.spec.componentId, i, loop.count});
        }
    }
    return out;
}

bool ChainSim::configured() const { return m_impl->configured; }

} // namespace current_lab::physics
