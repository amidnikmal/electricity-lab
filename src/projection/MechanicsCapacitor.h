#pragma once

#include "math/Vec2.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Spring-capacitor kinematics for the Mechanics view (Spintronics analogy).
//
// A capacitor is a SPRING between two crank arms on two INDEPENDENT, counter-
// rotating shafts (not a shared axle — that is what makes it a capacitor). The
// relative shaft angle theta is the charge; the spring's restoring moment is the
// voltage; stiffness k = 1/C. Stretch and compression are mirror images of the
// charge sign.
//
// This header is pure kinematics — no ImGui, no render, no time integration.
// theta is supplied from OUTSIDE (mapped from the solver's capacitor voltage);
// the model only turns it into geometry. Everything is in LOCAL units (x along
// the component axis, y along the +perp "up" direction); the renderer maps local
// -> world with origin + unit*x + perp*y. Kept testable in isolation
// (test_mechanics_capacitor.cpp). See also [[mechanics-projection]] doc.
namespace current_lab::mechanics {

struct SpringCapacitorParams {
    double halfSpan = 170.0; // shaft offset from centre along the axis (L0 = 2*halfSpan)
    double armLen = 44.0;    // crank arm length
    int coils = 12;          // spring coils
    double thetaMax = 1.0471975512; // 60 deg, charge normalisation
    double leadFrac = 0.12;  // straight lead at each spring end, fraction of length
    double baseAmp = 9.0;    // neutral zigzag amplitude (local units)
};

// theta>0 compresses the spring (charge<0), theta<0 stretches it (charge>0),
// theta==0 is neutral. The sign invariant is locked by a unit test.
struct SpringCapacitorModel {
    double theta = 0.0;
    SpringCapacitorParams p;

    Vec2 shaftL() const { return {-p.halfSpan, 0.0}; }
    Vec2 shaftR() const { return {+p.halfSpan, 0.0}; }

    // Crank-arm tips: the two springs attach here. Shafts spin counter to each
    // other, so the tips swing toward each other (compress) for theta>0.
    Vec2 crankL() const {
        return {-p.halfSpan + p.armLen * std::sin(theta), p.armLen * std::cos(theta)};
    }
    Vec2 crankR() const {
        return {+p.halfSpan - p.armLen * std::sin(theta), p.armLen * std::cos(theta)};
    }

    double restLength() const { return 2.0 * p.halfSpan; } // L0
    double springLength() const { return (crankR() - crankL()).length(); }

    // >0 compression, <0 stretch (mirror by charge sign).
    double deflection() const { return restLength() - springLength(); }

    // Charge normalised to [-1, 1]; theta<0 (stretch) -> charge>0.
    double charge() const {
        return p.thetaMax > 1e-9 ? std::clamp(-theta / p.thetaMax, -1.0, 1.0) : 0.0;
    }

    // Energy ~ (x/L0)^2, the mechanical image of E = 1/2 k x^2.
    double energy() const {
        double x = deflection() / std::max(restLength(), 1e-9);
        return x * x;
    }

    enum class Mode { Neutral, Stretched, Compressed };
    Mode mode() const {
        double d = deflection();
        if (d > 1e-6) return Mode::Compressed;
        if (d < -1e-6) return Mode::Stretched;
        return Mode::Neutral;
    }

    // Procedural zigzag spring in LOCAL coordinates along crankL -> crankR. The
    // coil step is springLength/coils, so coils physically spread apart when
    // stretched and bunch up when compressed; the amplitude moves OPPOSITE the
    // deflection (a stretched spring narrows, a compressed one bulges).
    std::vector<Vec2> springPath() const {
        std::vector<Vec2> pts;
        Vec2 cl = crankL(), cr = crankR();
        Vec2 d = cr - cl;
        double len = d.length();
        if (len < 1e-6) { pts.push_back(cl); pts.push_back(cr); return pts; }

        Vec2 u = d / len;
        Vec2 perp(-u.y, u.x);
        double defl = deflection();
        double lead = std::min(p.leadFrac * len, len * 0.45);
        double body = len - 2.0 * lead;
        int coils = std::max(2, p.coils);
        double step = body / coils;

        double amp = defl < 0.0
            ? p.baseAmp - std::clamp(std::fabs(defl) / 14.0, 0.0, 4.0) // stretch: narrow
            : p.baseAmp + std::clamp(defl / 10.0, 0.0, 7.0);           // compress: bulge

        pts.reserve(coils * 2 + 3);
        pts.push_back(cl);
        pts.push_back(cl + u * lead);
        for (int i = 0; i < coils; ++i) {
            double s = lead + step * (i + 0.5);
            double e = lead + step * (i + 1.0);
            double side = (i % 2 == 0) ? +1.0 : -1.0;
            pts.push_back(cl + u * s + perp * (amp * side));
            pts.push_back(cl + u * e);
        }
        return pts;
    }
};

// Map the solver capacitor voltage to the shaft angle. Positive Vc charges the
// spring into COMPRESSION (theta>0), so the existing "spring contracts as the
// capacitor charges" contract holds. vRange normalises to the scene's voltage
// span; theta saturates at +/-thetaMax.
inline double capacitorThetaFromVoltage(double vc, double vRange,
                                        double thetaMax = 1.0471975512) {
    double r = std::max(std::fabs(vRange), 1e-9);
    return std::clamp(vc / r, -1.0, 1.0) * thetaMax;
}

} // namespace current_lab::mechanics
