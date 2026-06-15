// Тест рендера среза ЭМ-поля в RGBA (чистая функция, без GL).
#include <gtest/gtest.h>
#include "render/EmSliceImage.h"
#include "physics/FdtdField.h"
#include "physics/EmScene.h"

using namespace current_lab;

TEST(EmSlice, DimensionsAndZeroFieldUniform) {
    physics::FdtdConfig cfg; cfg.nx = 40; cfg.ny = 30; cfg.nz = 20;
    physics::FdtdField sim(cfg);

    // Срез XY на k=10: размеры nx×ny.
    auto img = render::renderEmSlice(sim, render::EmPlane::XY, 10,
                                     render::EmFieldView::EzSigned, 1.0);
    ASSERT_EQ(img.w, 40);
    ASSERT_EQ(img.h, 30);
    ASSERT_EQ(img.rgba.size(), static_cast<size_t>(40 * 30));

    // Нулевое поле, EzSigned → все пиксели = цвет t=0.5, alpha 255.
    uint32_t mid = render::colormapSample(render::Colormap::Viridis, 0.5, 255);
    for (uint32_t px : img.rgba) ASSERT_EQ(px, mid);
    EXPECT_EQ((mid >> 24) & 0xFF, 255u); // alpha
}

TEST(EmSlice, NonUniformAfterSourceRuns) {
    physics::FdtdConfig cfg; cfg.nx = cfg.ny = cfg.nz = 40;
    physics::FdtdField sim(cfg);
    auto src = physics::buildEmScene(sim, physics::EmDemo::DipoleRadiator);
    for (int n = 0; n < 60; ++n) { physics::injectEmSource(sim, src, n); sim.step(); }

    auto img = render::renderEmSlice(sim, render::EmPlane::XY, 20,
                                     render::EmFieldView::EzSigned, 0.5);
    uint32_t first = img.rgba[0];
    bool varies = false;
    for (uint32_t px : img.rgba) if (px != first) { varies = true; break; }
    EXPECT_TRUE(varies) << "срез поля однородный — волна не отрисовалась";
}
