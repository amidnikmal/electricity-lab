// analogy_gallery — автономная офлайн-«галерея аналогий».
//
// Рендерит КАЖДУЮ сцену правой панели ЭМ-режима (src/ui/EmPanel.cpp,
// EmPanel::configureAnalogy) в PNG БЕЗ ImGui/GL — чистая физика + цвет,
// чтобы глазами проверить «детский тест» из docs/EM_ANALOGY_SPEC_2026-06-16.md
// и подобрать лучшие параметры сцен.
//
// 6 водяных сцен (RippleField): диполь, плоская волна, две щели, преломление,
// волновод, + (вода же) и стоячая волна на верёвке (StringWave, FarEnd::Fixed).
//
// Цвет — ТОЧНО как в EmPanel::waterColor:
//   знаковая высота t∈[-1,1] → deep(10,26,64) / mid(34,102,168) / foam(222,244,255).
// Нормировка по 95-му перцентилю |height|; для двух щелей — по области за барьером.
// Апскейл ×4, nearest. Стены/среды подрисовываем поверх (как в drawWaterPane).
//
// Сборка: g++ -std=c++20 -O2 -I src tools/analogy_gallery.cpp -o /tmp/gallery
//
// ВАЖНО: это диагностический инструмент. Рекомендованные константы — в
// docs/EM_GALLERY_FINDINGS_2026-06-16.md, интеграцию в EmPanel делает человек.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"
#include "physics/RippleField.h"
#include "physics/StringWave.h"
#include "render/ColorMaps.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace current_lab;
using current_lab::render::blendColor;
using current_lab::render::packColor;

// ---- цвет воды (копия EmPanel::waterColor, источник истины) ----
static uint32_t waterColor(float t) {
    if (t < -1.f) t = -1.f;
    if (t > 1.f) t = 1.f;
    const uint32_t deep = packColor(10, 26, 64, 255);
    const uint32_t mid  = packColor(34, 102, 168, 255);
    const uint32_t foam = packColor(222, 244, 255, 255);
    return (t < 0.f) ? blendColor(mid, deep, -t) : blendColor(mid, foam, t);
}

static const std::string kOut = "docs/em-gallery/";

// Апскейл ×4 nearest + запись PNG. px — N×N в формате packColor (RGBA-байты).
static void writeUpscaled(const std::vector<uint32_t>& px, int N, const std::string& name) {
    const int U = 4, W = N * U, H = N * U;
    std::vector<uint32_t> big((size_t)W * H);
    for (int b = 0; b < H; ++b)
        for (int a = 0; a < W; ++a)
            big[(size_t)b * W + a] = px[(size_t)(b / U) * N + (a / U)];
    stbi_write_png((kOut + name).c_str(), W, H, 4, big.data(), W * 4);
}

// Рендер RippleField в N×N пиксельный буфер с перцентильной нормировкой.
// j0 — левая граница области для нормировки (для двух щелей = за барьером).
static std::vector<uint32_t> renderWater(physics::RippleField& f, int j0 = 0) {
    const int N = f.grid();
    std::vector<float> mags;
    mags.reserve((size_t)N * N);
    for (int i = 0; i < N; ++i)
        for (int j = j0; j < N; ++j) mags.push_back(std::fabs(f.height(i, j)));
    size_t q = (size_t)(0.95 * (mags.size() - 1));
    std::nth_element(mags.begin(), mags.begin() + q, mags.end());
    float norm = std::max(1e-6f, mags[q]);

    std::vector<uint32_t> px((size_t)N * N);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            px[(size_t)i * N + j] = waterColor(f.height(i, j) / norm);
    return px;
}

// Затемнить пиксель (для подрисовки стен — как тёмный wall в drawWaterPane).
static void darken(std::vector<uint32_t>& px, int N, int i, int j) {
    if (i < 0 || i >= N || j < 0 || j >= N) return;
    px[(size_t)i * N + j] = packColor(20, 24, 30, 255);
}

// =====================================================================
//                          ВОДЯНЫЕ СЦЕНЫ
// =====================================================================
//
// Каждая функция принимает параметры (чтобы итеративно подбирать) и пишет PNG.

// 1. Диполь: точечный источник в центре → расходящиеся круги.
static void sceneDipole(int N, double f, int steps, const std::string& name) {
    physics::RippleField fld(N);
    fld.addDrivenSource(N / 2, N / 2, f, 1.0);
    fld.advance(steps);
    auto px = renderWater(fld);
    writeUpscaled(px, N, name);
}

// 2. Плоская волна: линейка источников у верхнего края → ровные ряды.
static void scenePlane(int N, double f, int srcRow, int steps, const std::string& name) {
    physics::RippleField fld(N);
    for (int i = 1; i < N - 1; ++i) fld.addDrivenSource(i, srcRow, f, 1.0);
    fld.advance(steps);
    auto px = renderWater(fld);
    writeUpscaled(px, N, name);
}

// 3. Две щели: плоская волна + стенка с двумя прорезями → интерференция.
static void sceneDoubleSlit(int N, double f, int srcRow, double jbFrac,
                            int sep, int half, int steps, const std::string& name) {
    physics::RippleField fld(N);
    for (int i = 1; i < N - 1; ++i) fld.addDrivenSource(i, srcRow, f, 1.0);
    const int jb = (int)(N * jbFrac);
    const int c = N / 2;
    for (int i = 0; i < N; ++i) {
        bool slitA = std::abs(i - (c - sep)) <= half;
        bool slitB = std::abs(i - (c + sep)) <= half;
        if (!slitA && !slitB) fld.setBarrier(i, jb, true);
    }
    fld.advance(steps);
    auto px = renderWater(fld, jb + 3);  // нормировка ЗА барьером
    // подрисовать стенку (тёмная), оставив щели цветом воды
    for (int i = 0; i < N; ++i) {
        bool slit = std::abs(i - (c - sep)) <= half || std::abs(i - (c + sep)) <= half;
        if (!slit) { darken(px, N, i, jb - 1); darken(px, N, i, jb); darken(px, N, i, jb + 1); }
    }
    writeUpscaled(px, N, name);
}

// 4. Преломление: нижняя половина (j>=N/2) медленнее → гребни теснее.
static void sceneRefract(int N, double f, int srcRow, float slow, int steps,
                         const std::string& name) {
    physics::RippleField fld(N);
    for (int i = 1; i < N - 1; ++i) fld.addDrivenSource(i, srcRow, f, 1.0);
    for (int i = 0; i < N; ++i)
        for (int j = N / 2; j < N; ++j) fld.setSpeedScale(i, j, slow);
    fld.advance(steps);
    auto px = renderWater(fld);
    // линия границы среды (бледная тёплая) — j = N/2
    for (int i = 0; i < N; ++i) {
        size_t id = (size_t)i * N + (N / 2);
        px[id] = blendColor(px[id], packColor(200, 180, 120, 255), 0.5);
    }
    writeUpscaled(px, N, name);
}

// 5. Волновод: канал между двумя стенками, источник в канале.
static void sceneWaveguide(int N, double f, int halfCh, int srcRow, int steps,
                           const std::string& name) {
    physics::RippleField fld(N);
    const int c = N / 2;
    for (int j = 0; j < N; ++j) {
        fld.setBarrier(c - halfCh, j, true);
        fld.setBarrier(c + halfCh, j, true);
    }
    fld.addDrivenSource(c, srcRow, f, 1.0);
    fld.advance(steps);
    auto px = renderWater(fld);
    for (int j = 0; j < N; ++j) { darken(px, N, c - halfCh, j); darken(px, N, c + halfCh, j); }
    writeUpscaled(px, N, name);
}

// =====================================================================
//                       ВЕРЁВКА (стоячая волна)
// =====================================================================
//
// Рисуем профиль y(i) как яркую линию + огибающую (макс |y| за прогон),
// чтобы узлы стоячей волны были видны. Вид сбоку, сетка W×H пикселей.
static void sceneRope(int n, double freq, int steps, int captureWin,
                      const std::string& name) {
    physics::StringWave s(n);
    s.setDrive(freq, 1.0);
    s.setFarEnd(physics::StringWave::FarEnd::Fixed);

    // огибающая: за последние captureWin шагов копим max|y(i)|
    std::vector<float> env(n, 0.f);
    int warmup = steps - captureWin;
    if (warmup < 0) warmup = 0;
    s.advance(warmup);
    for (int t = 0; t < captureWin; ++t) {
        s.advance(1);
        for (int i = 0; i < n; ++i) env[i] = std::max(env[i], (float)std::fabs(s.y(i)));
    }

    double norm = s.maxAbs();
    if (norm < 1e-6) norm = 1e-6;
    float envMax = 1e-6f;
    for (float e : env) envMax = std::max(envMax, e);

    // полотно: ширина по точкам верёвки (×scale), высота фикс.
    const int N = n;                 // условный «N» для апскейла не годится (n=220) — рисуем напрямую
    const int W = N;                 // 1 px на точку
    const int H = 200;               // высота кадра
    std::vector<uint32_t> img((size_t)W * H, packColor(16, 20, 26, 255));

    auto setpx = [&](int x, int y, uint32_t c) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        img[(size_t)y * W + x] = c;
    };
    auto vline = [&](int x, int y0, int y1, uint32_t c) {
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y) setpx(x, y, c);
    };

    const int midY = H / 2;
    const float amp = H * 0.40f;

    // огибающая (бледно-синяя) — «коридор» стоячей волны
    for (int i = 0; i < N; ++i) {
        int e = (int)(env[i] / envMax * amp);
        setpx(i, midY - e, packColor(70, 110, 150, 255));
        setpx(i, midY + e, packColor(70, 110, 150, 255));
    }
    // сама верёвка (яркая), соединяем соседей вертикальной заливкой
    for (int i = 0; i + 1 < N; ++i) {
        int y0 = midY - (int)(s.y(i) / norm * amp);
        int y1 = midY - (int)(s.y(i + 1) / norm * amp);
        vline(i, y0, y1, packColor(120, 210, 255, 255));
    }
    // узлы: где огибающая близка к нулю — жёлтый штрих сверху донизу (бледный)
    for (int i = 3; i < N - 3; ++i) {
        float r = env[i] / envMax;
        bool localMin = env[i] <= env[i - 2] && env[i] <= env[i + 2];
        if (r < 0.18f && localMin) {
            for (int y = midY - 6; y <= midY + 6; ++y)
                setpx(i, y, packColor(255, 220, 120, 255));
        }
    }
    // стена справа (зеркало)
    for (int x = W - 3; x < W; ++x) vline(x, 0, H - 1, packColor(90, 60, 40, 255));

    // апскейл ×4
    const int U = 4, BW = W * U, BH = H * U;
    std::vector<uint32_t> big((size_t)BW * BH);
    for (int b = 0; b < BH; ++b)
        for (int a = 0; a < BW; ++a)
            big[(size_t)b * BW + a] = img[(size_t)(b / U) * W + (a / U)];
    stbi_write_png((kOut + name).c_str(), BW, BH, 4, big.data(), BW * 4);
}

int main() {
    const int N = 130;

    // ===== БАЗОВЫЕ (как сейчас в EmPanel::configureAnalogy) =====
    sceneDipole(N, 0.06, 420, "01_dipole_base.png");
    scenePlane(N, 0.05, 3, 360, "02_plane_base.png");
    sceneDoubleSlit(N, 0.06, 6, 0.42, 13, 3, 520, "03_slit_base.png");
    sceneRefract(N, 0.05, 3, 0.5f, 600, "04_refract_base.png");
    sceneWaveguide(N, 0.07, 10, 6, 520, "05_waveguide_base.png");
    sceneRope(220, 0.05, 1400, 400, "06_rope_base.png");

    // ===== ВЕРЁВКА: подбор резонансной частоты для видимых узлов =====
    // L=n-1, c=0.5; число полуволн ≈ 2*L*f/c = 4*L*f. Хотим ~4-6 узлов.
    // Резонанс закреплённой струны: f_m = m*c/(2L). Для n=160, L=159, c=0.5:
    //   f_m = m*0.5/318 = m*0.001572.  m=4 → 0.00629, m=5 → 0.00786, m=6 → 0.00943.
    sceneRope(160, 0.00629, 6000, 1000, "06_rope_f4.png");   // 4 пучности
    sceneRope(160, 0.00786, 6000, 1000, "06_rope_f5.png");   // 5 пучностей
    sceneRope(160, 0.00943, 6000, 1000, "06_rope_f6.png");   // 6 пучностей

    // ===== ВОЛНОВОД: шире канал + ниже частота → ровные гребни вдоль =====
    sceneWaveguide(N, 0.05, 14, 4, 600, "05_wg_w14_f05.png");
    sceneWaveguide(N, 0.04, 16, 4, 700, "05_wg_w16_f04.png");
    sceneWaveguide(N, 0.05, 16, 4, 700, "05_wg_w16_f05.png");

    // ===== ПЛОСКАЯ ВОЛНА: проверить ровность рядов при разных шагах/частоте =====
    scenePlane(N, 0.05, 3, 220, "02_plane_f05_s220.png");  // меньше шагов: фронт не дошёл до края (нет искажений отражения)
    scenePlane(N, 0.045, 3, 240, "02_plane_f045.png");
    scenePlane(N, 0.06, 3, 200, "02_plane_f06.png");

    // ===== ДВЕ ЩЕЛИ: подбор расстояния между щелями для чётких полос =====
    sceneDoubleSlit(N, 0.06, 6, 0.40, 11, 2, 560, "03_slit_sep11.png");
    sceneDoubleSlit(N, 0.06, 6, 0.40, 16, 2, 560, "03_slit_sep16.png");
    sceneDoubleSlit(N, 0.05, 6, 0.40, 13, 2, 620, "03_slit_f05.png");

    return 0;
}
