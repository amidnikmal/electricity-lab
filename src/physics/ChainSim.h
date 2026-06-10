#pragma once

#include "math/Vec2.h"
#include <cstdint>
#include <memory>
#include <vector>

// Box2D chain for the Mechanics view: every component carries a CLOSED loop
// of rigid-jointed links running around an oval guide (two gears = the two
// nodes). The joints make the chain inextensible — the mechanical image of
// "one series loop, one current": brake one spot and the WHOLE loop slows.
// Link spacing is enforced by the joints, drive is a force toward the
// solver-calibrated loop speed, the resistor section adds heavy damping
// (friction brake) the drive has to work against.
namespace current_lab::physics {

struct ChainSpec {
    int componentId = -1;
    Vec2 a, b;                // component axis
    double halfWidth = 4.0;   // guide offset from the axis
    double targetSpeed = 0.0; // tangential, signed (calibrated from I)
    bool brake = false;       // resistor: friction zone on the loop
};

struct ChainLink {
    Vec2 pos;
    int componentId = -1;
    int indexInLoop = 0;
    int loopSize = 0;
};

class ChainSim {
public:
    ChainSim();
    ~ChainSim();
    ChainSim(const ChainSim&) = delete;
    ChainSim& operator=(const ChainSim&) = delete;

    void configure(const std::vector<ChainSpec>& specs, double linkRadius);
    void setTargets(const std::vector<ChainSpec>& specs);
    void step(double dt);

    std::vector<ChainLink> links() const;
    bool configured() const;
    static uint64_t layoutSignature(const std::vector<ChainSpec>& specs);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace current_lab::physics
