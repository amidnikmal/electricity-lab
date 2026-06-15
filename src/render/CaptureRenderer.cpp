#include "render/CaptureRenderer.h"
#include "render/CaptureHelpers.h"
#include "render/PrimitiveRenderer.h"
#include "render/ColorMaps.h"
#include "circuit/DemoCircuits.h"
#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"
#include "simulation/LiveSim.h"
#include "app/CaptureConfig.h"
#include "third_party/stb_image_write.h"
#define GL_GLEXT_PROTOTYPES
#include "gl_setup.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

namespace current_lab::render {

using demos::DemoCircuit;

// FBO-based offscreen capture with 2x supersampling.
//
// Implementation: a hidden GLFW window is created and ImGui is initialised.
// Rendering is directed to an FBO at 2x the target resolution; after
// ImGui::Render() the 2x FBO is blitted to a 1x FBO with linear filtering,
// then glReadPixels reads the downsampled result.  The window stays hidden
// (GLFW_VISIBLE = GLFW_FALSE) and no swap-buffers is ever called.
//
// Fallback note: if FBO creation or blit fails we fall back to reading from
// the default framebuffer (the hidden window), which still works because the
// window is never shown.  The comment in createFbo() marks this.

struct Fbo {
    GLuint handle = 0;
    GLuint texture = 0;
    int w = 0, h = 0;
};

static void destroyFbo(Fbo& fbo) {
    if (fbo.texture) { glDeleteTextures(1, &fbo.texture); fbo.texture = 0; }
    if (fbo.handle)  { glDeleteFramebuffers(1, &fbo.handle); fbo.handle = 0; }
}

static bool createFbo(Fbo& fbo, int w, int h) {
    destroyFbo(fbo);
    fbo.w = w; fbo.h = h;

    glGenFramebuffers(1, &fbo.handle);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.handle);

    glGenTextures(1, &fbo.texture);
    glBindTexture(GL_TEXTURE_2D, fbo.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.texture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroyFbo(fbo);
        return false;
    }
    return true;
}

CaptureResult captureToPng(int width, int height,
                           const std::string& demoNameStr,
                           projection::ProjectionKind view,
                           const visualization::LayerVisibility& layers,
                           double simTime,
                           const std::string& outputPath) {
    CaptureResult result;

    if (width < 1 || height < 1) {
        result.error = "invalid dimensions";
        return result;
    }

    // ---- parse demo name ----
    DemoCircuit demoEnum = app::parseDemoName(demoNameStr);
    if (demoEnum == DemoCircuit::Count) {
        result.error = "unknown demo: " + demoNameStr;
        return result;
    }

    // ---- build circuit ----
    Circuit circuit = demos::buildDemo(demoEnum);
    CircuitSolver solver;
    simulation::LiveSimConfig simCfg;
    simCfg.storySeconds = 3.0;
    simulation::LiveSim sim(simCfg);
    sim.onCircuitEvent(circuit, solver);

    // ---- run simulation ----
    CircuitSolution solution;
    if (simTime >= 0.0) {
        double simSoFar = 0.0;
        while (simSoFar < simTime && !sim.settled()) {
            const double dt = 1.0 / 60.0;
            sim.advance(circuit, solver, dt, solution);
            simSoFar += dt;
            if (simSoFar > simTime + 10.0) break; // safety
        }
        if (simSoFar < simTime) {
            // snap to exact time via one more step
            sim.stepOnce(circuit, solver, solution);
        }
    } else {
        // run to settle
        for (int i = 0; i < 5000 && !sim.settled(); ++i)
            sim.advance(circuit, solver, 1.0/60.0, solution);
    }
    solution = sim.currentSolution(circuit, solver);

    // ---- build projection ----
    projection::ViewParams vp;
    vp.layers = layers;
    vp.wireThickness = 8.0;
    vp.time = sim.time();
    vp.viewMin = Vec2{-1e6, -1e6};
    vp.viewMax = Vec2{1e6, 1e6};

    auto projResult = projection::buildProjection(view, circuit, &solution, vp);
    projResult.prims.legend.show = true;
    projResult.prims.legend.vMin = 0.0;
    projResult.prims.legend.vMax = 5.0;

    // ---- camera ----
    CanvasCamera camera = computeCameraForCircuit(circuit, width, height);

    // ---- offscreen GL context assumed active ----
    int supersample = 2;
    int renderW = width * supersample;
    int renderH = height * supersample;

    // Create FBOs
    Fbo fbo2x;
    if (!createFbo(fbo2x, renderW, renderH)) {
        result.error = "failed to create 2x FBO; falling back to default framebuffer — "
                       "capture will read from the hidden window instead";
        // fallback: use default framebuffer at 1x
        renderW = width;
        renderH = height;
        supersample = 1;
    }

    Fbo fbo1x;
    if (supersample > 1) {
        if (!createFbo(fbo1x, width, height)) {
            destroyFbo(fbo2x);
            result.error = "failed to create 1x downscale FBO";
            return result;
        }
    }

    // ---- render one frame ----
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(renderW), static_cast<float>(renderH));

    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##capture", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();

        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                          IM_COL32(17, 22, 28, 255));

        drawPrimitives(dl, projResult.prims, camera, origin, size, 1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();

    // Render into FBO (or default framebuffer as fallback)
    if (fbo2x.handle) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo2x.handle);
    }
    glViewport(0, 0, renderW, renderH);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Downsample if needed
    GLuint readFbo = fbo2x.handle;
    int readW = renderW, readH = renderH;

    if (supersample > 1 && fbo1x.handle) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo2x.handle);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo1x.handle);
        glBlitFramebuffer(0, 0, renderW, renderH, 0, 0, width, height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        readFbo = fbo1x.handle;
        readW = width;
        readH = height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, readFbo);

    // Read pixels
    std::vector<unsigned char> pixels(readW * readH * 4);
    glReadPixels(0, 0, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (GL origin is bottom-left, PNG is top-left)
    flipRowsVertically(pixels, readW, readH, 4);

    // Write PNG
    if (!stbi_write_png(outputPath.c_str(), readW, readH, 4, pixels.data(), readW * 4)) {
        result.error = "stbi_write_png failed for: " + outputPath;
        destroyFbo(fbo2x);
        destroyFbo(fbo1x);
        return result;
    }

    destroyFbo(fbo2x);
    destroyFbo(fbo1x);

    result.ok = true;
    result.width = readW;
    result.height = readH;
    return result;
}

} // namespace current_lab::render
