// Тест живой ЭМ-сцены (render/EmLiveScene.h): поле оживает между кадрами,
// срез отдаётся как RGBA нужного размера, reset/setScene перестраивают сцену.
#include <gtest/gtest.h>
#include "render/EmLiveScene.h"

#include <set>

using current_lab::render::EmLiveScene;
using current_lab::render::EmPlane;
using current_lab::render::EmFieldView;
using current_lab::physics::EmDemo;

namespace {

// Сколько различных цветов в срезе — мера «есть ли картина поля».
size_t distinctColors(const current_lab::render::EmImage& img) {
    std::set<uint32_t> s(img.rgba.begin(), img.rgba.end());
    return s.size();
}

} // namespace

TEST(EmLiveScene, ImageMatchesGridSize) {
    EmLiveScene scene(EmDemo::DipoleRadiator, 40);
    auto img = scene.image(EmPlane::XY, EmFieldView::EzSigned);
    EXPECT_EQ(img.w, 40);
    EXPECT_EQ(img.h, 40);
    EXPECT_EQ(img.rgba.size(), static_cast<size_t>(40 * 40));
}

TEST(EmLiveScene, FieldComesAliveAfterStepping) {
    EmLiveScene scene(EmDemo::DipoleRadiator, 44);
    // В самом начале поле нулевое — срез почти однородный.
    size_t before = distinctColors(scene.image(EmPlane::XY, EmFieldView::EzSigned));

    scene.advance(120);
    EXPECT_EQ(scene.stepCount(), 120);
    EXPECT_GT(scene.simTime(), 0.0);

    // После прогона у диполя есть расходящиеся кольца — много разных цветов.
    size_t after = distinctColors(scene.image(EmPlane::XY, EmFieldView::EzSigned));
    EXPECT_GT(after, before);
    EXPECT_GT(after, 8u);
}

TEST(EmLiveScene, ResetClearsTimeAndField) {
    EmLiveScene scene(EmDemo::DipoleRadiator, 40);
    scene.advance(80);
    ASSERT_GT(scene.simTime(), 0.0);

    scene.reset();
    EXPECT_EQ(scene.stepCount(), 0);
    EXPECT_DOUBLE_EQ(scene.simTime(), 0.0);
}

TEST(EmLiveScene, SetSceneRebuildsAndResets) {
    EmLiveScene scene(EmDemo::DipoleRadiator, 40);
    scene.advance(50);
    scene.setScene(EmDemo::DoubleSlit);
    EXPECT_EQ(scene.scene(), EmDemo::DoubleSlit);
    EXPECT_EQ(scene.stepCount(), 0);       // смена сцены = свежее поле
    EXPECT_DOUBLE_EQ(scene.simTime(), 0.0);
}

TEST(EmLiveScene, SettingSameSceneKeepsRunning) {
    EmLiveScene scene(EmDemo::Mirror, 40);
    scene.advance(30);
    scene.setScene(EmDemo::Mirror);        // та же сцена — не сбрасываем
    EXPECT_EQ(scene.stepCount(), 30);
}
