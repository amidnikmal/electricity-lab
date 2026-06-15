#include <gtest/gtest.h>
#include "app/CaptureConfig.h"
#include "render/CaptureHelpers.h"
#include "circuit/DemoCircuits.h"
#include "circuit/Circuit.h"
#include "math/Vec2.h"
#include <vector>
#include <cstring>

namespace {

using namespace current_lab;
using namespace current_lab::demos;
using namespace current_lab::projection;
using namespace current_lab::visualization;

// ---------------------------------------------------------------------------
// parseDemoName
// ---------------------------------------------------------------------------
TEST(CaptureConfig, ParseDemoNameValid) {
    EXPECT_EQ(app::parseDemoName("SourceResistor"), DemoCircuit::SourceResistor);
    EXPECT_EQ(app::parseDemoName("RcCapacitor"), DemoCircuit::RcCapacitor);
    EXPECT_EQ(app::parseDemoName("RlInductor"), DemoCircuit::RlInductor);
    EXPECT_EQ(app::parseDemoName("DiodeResistor"), DemoCircuit::DiodeResistor);
    EXPECT_EQ(app::parseDemoName("SwitchResistor"), DemoCircuit::SwitchResistor);
    EXPECT_EQ(app::parseDemoName("SwitchedRc"), DemoCircuit::SwitchedRc);
    EXPECT_EQ(app::parseDemoName("RlcSeries"), DemoCircuit::RlcSeries);
    EXPECT_EQ(app::parseDemoName("RlcCirculating"), DemoCircuit::RlcCirculating);
    EXPECT_EQ(app::parseDemoName("PeakDetector"), DemoCircuit::PeakDetector);
    EXPECT_EQ(app::parseDemoName("AcRectifier"), DemoCircuit::AcRectifier);
    EXPECT_EQ(app::parseDemoName("ResistorDivider"), DemoCircuit::ResistorDivider);
}

TEST(CaptureConfig, ParseDemoNameDisplayName) {
    EXPECT_EQ(app::parseDemoName("Demo: source + resistor"), DemoCircuit::SourceResistor);
    EXPECT_EQ(app::parseDemoName("Demo: RC charging"), DemoCircuit::RcCapacitor);
}

TEST(CaptureConfig, ParseDemoNameInvalid) {
    EXPECT_EQ(app::parseDemoName(""), DemoCircuit::Count);
    EXPECT_EQ(app::parseDemoName("NoSuchDemo"), DemoCircuit::Count);
    EXPECT_EQ(app::parseDemoName("garbage"), DemoCircuit::Count);
}

// ---------------------------------------------------------------------------
// parseView
// ---------------------------------------------------------------------------
TEST(CaptureConfig, ParseView) {
    EXPECT_EQ(app::parseView("electrical"), projection::ProjectionKind::Physics);
    EXPECT_EQ(app::parseView("mechanical"), projection::ProjectionKind::Mechanical);
    EXPECT_EQ(app::parseView("hydraulic"), projection::ProjectionKind::Hydraulic);
    EXPECT_EQ(app::parseView(""), projection::ProjectionKind::Physics);  // default
    EXPECT_EQ(app::parseView("unknown"), projection::ProjectionKind::Physics);
}

// ---------------------------------------------------------------------------
// parseLayers
// ---------------------------------------------------------------------------
TEST(CaptureConfig, ParseLayersEmpty) {
    auto lv = app::parseLayers("");
    EXPECT_TRUE(lv.current);
    EXPECT_TRUE(lv.potential);
    EXPECT_TRUE(lv.canvasReadouts);
    EXPECT_FALSE(lv.heat);
    EXPECT_FALSE(lv.drift);
}

TEST(CaptureConfig, ParseLayersSingle) {
    auto lv = app::parseLayers("potential");
    EXPECT_TRUE(lv.potential);
    EXPECT_FALSE(lv.current);
    EXPECT_FALSE(lv.heat);
}

TEST(CaptureConfig, ParseLayersMultiple) {
    auto lv = app::parseLayers("potential,current,heat,drift");
    EXPECT_TRUE(lv.potential);
    EXPECT_TRUE(lv.current);
    EXPECT_TRUE(lv.heat);
    EXPECT_TRUE(lv.drift);
    EXPECT_FALSE(lv.magnetic);
    EXPECT_FALSE(lv.electricField);
}

TEST(CaptureConfig, ParseLayersAll) {
    auto lv = app::parseLayers(
        "current,electronFlow,potential,drift,electricField,heat,power,"
        "magnetic,surfaceCharge,lic,canvasReadouts,debugMarkers");
    EXPECT_TRUE(lv.current);
    EXPECT_TRUE(lv.electronFlow);
    EXPECT_TRUE(lv.potential);
    EXPECT_TRUE(lv.drift);
    EXPECT_TRUE(lv.electricField);
    EXPECT_TRUE(lv.heat);
    EXPECT_TRUE(lv.power);
    EXPECT_TRUE(lv.magnetic);
    EXPECT_TRUE(lv.surfaceCharge);
    EXPECT_TRUE(lv.lic);
    EXPECT_TRUE(lv.canvasReadouts);
    EXPECT_TRUE(lv.debugMarkers);
}

// ---------------------------------------------------------------------------
// parseCaptureArgs
// ---------------------------------------------------------------------------
TEST(CaptureConfig, ParseArgsBasic) {
    const char* argv[] = {
        "current-lab",
        "--capture",
        "--demo", "RcCapacitor",
        "--out", "/tmp/test.png"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    std::vector<std::string> storage;
    storage.reserve(argc);
    std::vector<char*> mutableArgv;
    for (int i = 0; i < argc; ++i) {
        storage.emplace_back(argv[i]);
        mutableArgv.push_back(storage.back().data());
    }
    auto cfg = app::parseCaptureArgs(argc, mutableArgv.data());
    EXPECT_TRUE(cfg.capture);
    EXPECT_EQ(cfg.demoName, "RcCapacitor");
    EXPECT_EQ(cfg.outputFile, "/tmp/test.png");
    EXPECT_EQ(cfg.width, 1600);
    EXPECT_EQ(cfg.height, 1000);
    EXPECT_EQ(cfg.view, "");
    EXPECT_DOUBLE_EQ(cfg.time, -1.0);
}

TEST(CaptureConfig, ParseArgsFull) {
    const char* argv[] = {
        "current-lab",
        "--capture",
        "--demo", "RlcSeries",
        "--view", "mechanical",
        "--layers", "potential,current",
        "--out", "screenshot.png",
        "--width", "1920",
        "--height", "1080",
        "--time", "2.5"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    std::vector<std::string> storage;
    storage.reserve(argc);
    std::vector<char*> mutableArgv;
    for (int i = 0; i < argc; ++i) {
        storage.emplace_back(argv[i]);
        mutableArgv.push_back(storage.back().data());
    }
    auto cfg = app::parseCaptureArgs(argc, mutableArgv.data());
    EXPECT_TRUE(cfg.capture);
    EXPECT_EQ(cfg.demoName, "RlcSeries");
    EXPECT_EQ(cfg.view, "mechanical");
    EXPECT_EQ(cfg.layers, "potential,current");
    EXPECT_EQ(cfg.outputFile, "screenshot.png");
    EXPECT_EQ(cfg.width, 1920);
    EXPECT_EQ(cfg.height, 1080);
    EXPECT_DOUBLE_EQ(cfg.time, 2.5);
}

TEST(CaptureConfig, ParseArgsNoCapture) {
    const char* argv[] = {"current-lab", "somefile.txt"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    std::vector<std::string> storage;
    storage.reserve(argc);
    std::vector<char*> mutableArgv;
    for (int i = 0; i < argc; ++i) {
        storage.emplace_back(argv[i]);
        mutableArgv.push_back(storage.back().data());
    }
    auto cfg = app::parseCaptureArgs(argc, mutableArgv.data());
    EXPECT_FALSE(cfg.capture);
}

// ---------------------------------------------------------------------------
// flipRowsVertically
// ---------------------------------------------------------------------------
TEST(CaptureHelpers, FlipRowsVerticallyEven) {
    // 2x2 RGBA image: two rows, check inversion
    // Row 0: R0,G0,B0,A0, R1,G1,B1,A1
    // Row 1: R2,G2,B2,A2, R3,G3,B3,A3
    std::vector<unsigned char> pixels = {
        10,20,30,40,   50,60,70,80,   // row 0
        90,100,110,120, 130,140,150,160 // row 1
    };
    auto orig = pixels;
    render::flipRowsVertically(pixels, 2, 2, 4);

    // After flip, row 0 <-> row 1
    EXPECT_EQ(pixels[0], orig[8]);
    EXPECT_EQ(pixels[1], orig[9]);
    EXPECT_EQ(pixels[2], orig[10]);
    EXPECT_EQ(pixels[3], orig[11]);
    EXPECT_EQ(pixels[4], orig[12]);
    EXPECT_EQ(pixels[5], orig[13]);
    EXPECT_EQ(pixels[6], orig[14]);
    EXPECT_EQ(pixels[7], orig[15]);

    EXPECT_EQ(pixels[8], orig[0]);
    EXPECT_EQ(pixels[9], orig[1]);
    EXPECT_EQ(pixels[10], orig[2]);
    EXPECT_EQ(pixels[11], orig[3]);
}

TEST(CaptureHelpers, FlipRowsVerticallyOdd) {
    // 3 rows, 1 column, 1 channel
    std::vector<unsigned char> pixels = {1, 2, 3};
    render::flipRowsVertically(pixels, 1, 3, 1);
    EXPECT_EQ(pixels[0], 3);
    EXPECT_EQ(pixels[1], 2); // middle stays
    EXPECT_EQ(pixels[2], 1);
}

TEST(CaptureHelpers, FlipRowsVerticallyNoop) {
    std::vector<unsigned char> pixels = {42};
    render::flipRowsVertically(pixels, 1, 1, 1);
    EXPECT_EQ(pixels[0], 42);
}

// ---------------------------------------------------------------------------
// computeCameraForCircuit
// ---------------------------------------------------------------------------
TEST(CaptureHelpers, ComputeCameraBasic) {
    Circuit c;
    c.addNode(Vec2(100, 100), "A");
    c.addNode(Vec2(500, 400), "B");

    auto cam = render::computeCameraForCircuit(c, 800, 600);

    // Scale should fit the circuit in the canvas
    EXPECT_GT(cam.scale, 0.0f);
    // The min node should be at a positive screen position (with padding)
    ImVec2 screenA = cam.worldToScreen(Vec2(100, 100));
    EXPECT_GE(screenA.x, 0.0f);
    EXPECT_GE(screenA.y, 0.0f);
    // The max node should be within the canvas
    ImVec2 screenB = cam.worldToScreen(Vec2(500, 400));
    EXPECT_LE(screenB.x, 800.0f);
    EXPECT_LE(screenB.y, 600.0f);
}

TEST(CaptureHelpers, ComputeCameraEmptyCircuit) {
    Circuit c;
    auto cam = render::computeCameraForCircuit(c, 800, 600);
    EXPECT_FLOAT_EQ(cam.scale, 1.0f);
    EXPECT_FLOAT_EQ(cam.offset.x, 0.0f);
    EXPECT_FLOAT_EQ(cam.offset.y, 0.0f);
}

TEST(CaptureHelpers, ComputeCameraSingleNode) {
    Circuit c;
    c.addNode(Vec2(200, 300), "N1");

    auto cam = render::computeCameraForCircuit(c, 1600, 1000);
    EXPECT_GT(cam.scale, 0.0f);

    ImVec2 screenPos = cam.worldToScreen(Vec2(200, 300));
    EXPECT_GE(screenPos.x, 0.0f);
    EXPECT_LE(screenPos.x, 1600.0f);
    EXPECT_GE(screenPos.y, 0.0f);
    EXPECT_LE(screenPos.y, 1000.0f);
}

} // namespace
