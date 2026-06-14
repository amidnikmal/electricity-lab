#include <gtest/gtest.h>
#include "render/ColorMaps.h"

namespace {

using namespace current_lab::render;

// Распаковка каналов из IM_COL32-формата (R | G<<8 | B<<16 | A<<24).
int chR(uint32_t c) { return static_cast<int>(c & 0xFF); }
int chG(uint32_t c) { return static_cast<int>((c >> 8) & 0xFF); }
int chB(uint32_t c) { return static_cast<int>((c >> 16) & 0xFF); }
int chA(uint32_t c) { return static_cast<int>((c >> 24) & 0xFF); }

// Перцептивная светлота (Rec.709 relative luminance).
double luminance(uint32_t c) {
    return 0.2126 * chR(c) + 0.7152 * chG(c) + 0.0722 * chB(c);
}

} // namespace

// Концы палитры должны соответствовать каноническим концам viridis:
// t=0 — тёмно-фиолетовый (#440154), t=1 — ярко-жёлтый (#fde725).
TEST(Colormap, ViridisEndpoints) {
    uint32_t low = colormapSample(Colormap::Viridis, 0.0, 255);
    EXPECT_LT(chR(low), 90);   // низкий R
    EXPECT_LT(chG(low), 40);   // низкий G
    EXPECT_GT(chB(low), 60);   // умеренно-высокий B (фиолетовый)

    uint32_t high = colormapSample(Colormap::Viridis, 1.0, 255);
    EXPECT_GT(chR(high), 220); // высокий R
    EXPECT_GT(chG(high), 200); // высокий G
    EXPECT_LT(chB(high), 80);  // низкий B (жёлтый)
}

// Ключевое свойство viridis: светлота монотонно не убывает по t.
// Это отличает её от rainbow/jet с немонотонной светлотой и ложными границами.
TEST(Colormap, ViridisLightnessMonotonic) {
    const int kSamples = 33;
    double prev = -1.0;
    const double tol = 1.0; // допуск на округление байтовых каналов
    for (int i = 0; i < kSamples; ++i) {
        double t = static_cast<double>(i) / (kSamples - 1);
        double L = luminance(colormapSample(Colormap::Viridis, t, 255));
        EXPECT_GE(L, prev - tol) << "светлота убывает на t=" << t;
        prev = L;
    }
}

// potentialColor должен использовать viridis с alpha=220.
TEST(Colormap, PotentialColorUsesViridisAlpha) {
    uint32_t c = potentialColor(0.0, 0.0, 10.0); // t=0
    EXPECT_EQ(chA(c), 220);
    uint32_t expected = colormapSample(Colormap::Viridis, 0.0, 220);
    EXPECT_EQ(c, expected);
}

// Клиппинг: значения вне [vMin, vMax] прижимаются к концам палитры.
TEST(Colormap, PotentialColorClamps) {
    uint32_t below = potentialColor(-100.0, 0.0, 10.0);
    uint32_t atMin = potentialColor(0.0, 0.0, 10.0);
    EXPECT_EQ(below, atMin);

    uint32_t above = potentialColor(999.0, 0.0, 10.0);
    uint32_t atMax = potentialColor(10.0, 0.0, 10.0);
    EXPECT_EQ(above, atMax);
}

// Вырожденный диапазон (vMax == vMin): нейтральный сине-серый цвет.
TEST(Colormap, PotentialColorDegenerateRange) {
    uint32_t c = potentialColor(5.0, 5.0, 5.0);
    EXPECT_EQ(c, packColor(93, 128, 196, 220));

    // и при крайне малом диапазоне < 1e-12
    uint32_t c2 = potentialColor(5.0, 5.0, 5.0 + 1e-15);
    EXPECT_EQ(c2, packColor(93, 128, 196, 220));
}
