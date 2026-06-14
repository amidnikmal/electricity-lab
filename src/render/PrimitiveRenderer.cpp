#include "render/PrimitiveRenderer.h"
#include "render/ColorMaps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <vector>

namespace current_lab::render {

namespace {

struct Mapper {
    const CanvasCamera& camera;
    ImVec2 origin;

    ImVec2 toScreen(Vec2 w) const {
        ImVec2 ws = camera.worldToScreen(w);
        return ImVec2(ws.x + origin.x, ws.y + origin.y);
    }
    float px(double worldUnits) const { return static_cast<float>(worldUnits * camera.scale); }
};

void drawArrowHead(ImDrawList* dl, const Mapper& m, Vec2 pos, Vec2 dir, double size, ImU32 color) {
    Vec2 right(-dir.y, dir.x);
    Vec2 tip = pos + dir * size;
    Vec2 leftWing = pos - dir * size * 0.4 + right * size * 0.45;
    Vec2 rightWing = pos - dir * size * 0.4 - right * size * 0.45;
    dl->AddTriangleFilled(m.toScreen(tip), m.toScreen(leftWing), m.toScreen(rightWing), color);
}

void drawGradient(ImDrawList* dl, const Mapper& m, const PrimGradient& g) {
    Vec2 ab = g.b - g.a;
    double len = ab.length();
    if (len < 0.5) return;
    Vec2 unit = ab / len;
    Vec2 perp(-unit.y, unit.x);
    double halfW = g.width * 0.5;

    // Cap segments: a smooth V-ramp needs no more than ~96 bands even across a
    // screen-wide conductor; zoomed in, len*scale exploded this to 1000 quads
    // per gradient (same blow-up class as the auto-tessellated circles).
    int nSeg = std::max(2, std::min(96, static_cast<int>(len * m.camera.scale / 2.5)));
    for (int i = 0; i < nSeg; ++i) {
        double t0 = static_cast<double>(i) / nSeg;
        double t1 = static_cast<double>(i + 1) / nSeg;
        uint32_t c = potentialColor(g.vA + (g.vB - g.vA) * t0, g.vMin, g.vMax);
        c = withAlpha(c, g.alpha);
        Vec2 p0 = g.a + unit * (len * t0);
        Vec2 p1 = g.a + unit * (len * t1);
        dl->AddQuadFilled(m.toScreen(p0 + perp * halfW), m.toScreen(p0 - perp * halfW),
                          m.toScreen(p1 - perp * halfW), m.toScreen(p1 + perp * halfW), c);
    }
}

void drawLegend(ImDrawList* dl, ImVec2 origin, ImVec2 size, const PotentialLegend& legend) {
    if (!legend.show || std::abs(legend.vMax - legend.vMin) < 1e-12) return;

    float barX = origin.x + size.x - 28;
    float barY0 = origin.y + 12;
    float barH = std::min(150.0f, size.y * 0.4f);
    float barW = 12.0f;

    dl->AddRectFilled(ImVec2(barX - 1, barY0 - 1), ImVec2(barX + barW + 1, barY0 + barH + 1),
                      IM_COL32(20, 20, 25, 200));
    dl->AddRect(ImVec2(barX - 1, barY0 - 1), ImVec2(barX + barW + 1, barY0 + barH + 1),
                IM_COL32(100, 100, 110, 255));

    int N = 40;
    for (int i = 0; i < N; ++i) {
        double t = 1.0 - static_cast<double>(i) / N;
        float y0 = barY0 + static_cast<float>(i) / N * barH;
        float y1 = barY0 + static_cast<float>(i + 1) / N * barH;
        ImU32 col = potentialColor(legend.vMin + t * (legend.vMax - legend.vMin),
                                   legend.vMin, legend.vMax);
        dl->AddRectFilled(ImVec2(barX, y0), ImVec2(barX + barW, y1), col);
    }

    std::string buf = std::format("{:.2f} V", legend.vMax);
    dl->AddText(ImVec2(barX + barW + 4, barY0 - 6), IM_COL32(200, 200, 200, 255), buf.c_str());
    buf = std::format("{:.2f} V", (legend.vMin + legend.vMax) * 0.5);
    dl->AddText(ImVec2(barX + barW + 4, barY0 + barH * 0.5f - 6), IM_COL32(180, 180, 180, 255), buf.c_str());
    buf = std::format("{:.2f} V", legend.vMin);
    dl->AddText(ImVec2(barX + barW + 4, barY0 + barH - 6), IM_COL32(200, 200, 200, 255), buf.c_str());

    dl->AddText(ImVec2(barX - 6, barY0 - 18), IM_COL32(150, 185, 210, 255), "Potential");
    dl->AddText(ImVec2(barX - 2, barY0 + barH + 4), IM_COL32(150, 150, 160, 255), "V");
}

} // namespace

void drawGrid(ImDrawList* dl, const CanvasCamera& camera, ImVec2 origin, ImVec2 size) {
    float gridSpacing = 50.0f * camera.scale;
    if (gridSpacing < 10.0f) gridSpacing = 10.0f;

    double ox = std::fmod(camera.offset.x, static_cast<double>(gridSpacing));
    double oy = std::fmod(camera.offset.y, static_cast<double>(gridSpacing));

    ImU32 color = IM_COL32(33, 48, 58, 255);
    float xEnd = origin.x + size.x;
    float yEnd = origin.y + size.y;

    for (float x = origin.x + static_cast<float>(ox); x < xEnd; x += gridSpacing)
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, yEnd), color);
    for (float y = origin.y + static_cast<float>(oy); y < yEnd; y += gridSpacing)
        dl->AddLine(ImVec2(origin.x, y), ImVec2(xEnd, y), color);
}

// ImGui auto-tessellates circles to a ~0.3px error → up to 512 segments for a
// big radius. Zooming in makes world-space particles/wheels huge on screen, so
// hundreds of auto circles exploded into millions of triangles (the "zoom lags"
// report). Cap the segment count: dots stay cheap, gears stay round enough.
inline int particleSegs(float r) {
    return static_cast<int>(std::clamp(r * 0.8f, 6.0f, 14.0f));
}
inline int circleSegs(float r) {
    return static_cast<int>(std::clamp(r * 0.5f, 10.0f, 40.0f));
}

void drawPrimitives(ImDrawList* dl, const RenderPrimitives& prims,
                    const CanvasCamera& camera, ImVec2 origin, ImVec2 size) {
    Mapper m{camera, origin};
    const ImVec2 clipMin(origin.x, origin.y);
    const ImVec2 clipMax(origin.x + size.x, origin.y + size.y);
    // True when a screen-space bbox lies fully outside the pane: zoomed in, most
    // of a circuit is off-screen, but every primitive was still tessellated and
    // uploaded. Skipping them is the same win as the particle/circle culling.
    auto offscreen = [&](float minx, float miny, float maxx, float maxy) {
        return maxx < clipMin.x || minx > clipMax.x ||
               maxy < clipMin.y || miny > clipMax.y;
    };
    auto offscreenSeg = [&](ImVec2 p, ImVec2 q, float pad) {
        return offscreen(std::min(p.x, q.x) - pad, std::min(p.y, q.y) - pad,
                         std::max(p.x, q.x) + pad, std::max(p.y, q.y) + pad);
    };

    // Backmost: smooth scalar-field heatmap (bilinear per cell).
    for (const auto& cell : prims.fieldCells) {
        ImVec2 pmin = m.toScreen(cell.min);
        ImVec2 pmax = m.toScreen(cell.max);
        if (offscreen(pmin.x, pmin.y, pmax.x, pmax.y)) continue;
        dl->AddRectFilledMultiColor(pmin, pmax, cell.cMinXMinY, cell.cMaxXMinY,
                                    cell.cMaxXMaxY, cell.cMinXMaxY);
    }

    for (const auto& glow : prims.glows) {
        float r = m.px(glow.radius);
        ImVec2 c = m.toScreen(glow.center);
        // Intensity scales each layer's OWN (deliberately soft) alpha — it must
        // not force the core opaque. glow.color carries the intended softness
        // (e.g. a field source is alpha ~16); dim it by intensity for weak/strong
        // sources, never replace it with a solid fill (that turned the smooth
        // potential field into bright filled discs).
        const float k = static_cast<float>(std::clamp(glow.intensity, 0.0, 1.0));
        const unsigned baseA = (glow.color >> 24) & 0xFFu;
        auto fade = [&](unsigned a) {
            return withAlpha(glow.color, static_cast<unsigned>(std::lround(a * k)));
        };
        dl->AddCircleFilled(c, r * 1.8f, fade(6), 48);     // outer halo
        dl->AddCircleFilled(c, r, fade(baseA), 48);        // soft core (own alpha)
        for (int ring = 1; ring <= 4; ++ring) {
            float rr = r * (0.45f + 0.32f * ring);
            dl->AddCircle(c, rr, fade(static_cast<unsigned>(std::max(0, 28 - ring * 4))), 64, 1.0f);
        }
    }

    for (const auto& grad : prims.gradients) {
        if (offscreenSeg(m.toScreen(grad.a), m.toScreen(grad.b),
                         m.px(grad.width * 0.5) + 2.0f))
            continue;
        drawGradient(dl, m, grad);
    }

    for (const auto& quad : prims.quads) {
        if (!quad.filled) continue;
        ImVec2 a = m.toScreen(quad.p1), b = m.toScreen(quad.p2);
        ImVec2 c = m.toScreen(quad.p3), d = m.toScreen(quad.p4);
        if (offscreen(std::min({a.x, b.x, c.x, d.x}), std::min({a.y, b.y, c.y, d.y}),
                      std::max({a.x, b.x, c.x, d.x}), std::max({a.y, b.y, c.y, d.y})))
            continue;
        dl->AddQuadFilled(a, b, c, d, quad.color);
    }

    // Particles sit inside the conductors: above the fills, below the outlines
    // so element bodies stay readable.
    for (const auto& particle : prims.particles) {
        float r = particle.screenSpaceRadius ? static_cast<float>(particle.radius)
                                             : m.px(particle.radius);
        ImVec2 p = m.toScreen(particle.pos);
        // Cull off-screen particles: zoomed in, most of a dense water loop is
        // outside the pane and would otherwise still be tessellated + uploaded.
        if (p.x + r < clipMin.x || p.x - r > clipMax.x ||
            p.y + r < clipMin.y || p.y - r > clipMax.y)
            continue;
        dl->AddCircleFilled(p, r, particle.color, particleSegs(r));
    }

    for (const auto& quad : prims.quads) {
        if (quad.filled) continue;
        ImVec2 a = m.toScreen(quad.p1), b = m.toScreen(quad.p2);
        ImVec2 c = m.toScreen(quad.p3), d = m.toScreen(quad.p4);
        if (offscreen(std::min({a.x, b.x, c.x, d.x}), std::min({a.y, b.y, c.y, d.y}),
                      std::max({a.x, b.x, c.x, d.x}), std::max({a.y, b.y, c.y, d.y})))
            continue;
        dl->AddQuad(a, b, c, d, quad.color, static_cast<float>(quad.outlineThickness));
    }

    for (const auto& line : prims.lines) {
        float w = line.screenSpaceWidth ? static_cast<float>(line.width) : m.px(line.width);
        ImVec2 a = m.toScreen(line.a), b = m.toScreen(line.b);
        if (offscreenSeg(a, b, w + 1.0f)) continue;
        dl->AddLine(a, b, line.color, w);
    }

    for (const auto& poly : prims.polylines) {
        if (poly.pts.size() < 2) continue;
        float w = poly.screenSpaceWidth ? static_cast<float>(poly.width) : m.px(poly.width);
        std::vector<ImVec2> pts;
        pts.reserve(poly.pts.size());
        float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
        for (const auto& p : poly.pts) {
            ImVec2 s = m.toScreen(p);
            pts.push_back(s);
            minx = std::min(minx, s.x); maxx = std::max(maxx, s.x);
            miny = std::min(miny, s.y); maxy = std::max(maxy, s.y);
        }
        if (offscreen(minx - w, miny - w, maxx + w, maxy + w)) continue;
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), poly.color, ImDrawFlags_None, w);
    }

    for (const auto& circle : prims.circles) {
        float r = circle.screenSpaceRadius ? static_cast<float>(circle.radius) : m.px(circle.radius);
        ImVec2 c = m.toScreen(circle.center);
        if (c.x + r < clipMin.x || c.x - r > clipMax.x ||
            c.y + r < clipMin.y || c.y - r > clipMax.y)
            continue;
        if (circle.filled)
            dl->AddCircleFilled(c, r, circle.color, circleSegs(r));
        else
            dl->AddCircle(c, r, circle.color, circleSegs(r), static_cast<float>(circle.thickness));
    }

    for (const auto& arrow : prims.arrows) {
        ImVec2 p = m.toScreen(arrow.pos);
        float r = m.px(arrow.size) + 2.0f;
        if (offscreen(p.x - r, p.y - r, p.x + r, p.y + r)) continue;
        drawArrowHead(dl, m, arrow.pos, arrow.dir, arrow.size, arrow.color);
    }

    for (const auto& label : prims.labels) {
        ImVec2 p = m.toScreen(label.pos);
        if (offscreen(p.x, p.y, p.x + 120.0f, p.y + 16.0f)) continue; // ~text extent
        dl->AddText(p, label.color, label.text.c_str());
    }

    drawLegend(dl, origin, size, prims.legend);
}

} // namespace current_lab::render
