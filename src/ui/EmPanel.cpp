#include "ui/EmPanel.h"

#include "gl_setup.h"
#include "imgui.h"
#include "physics/EmScene.h"
#include "render/ColorMaps.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace current_lab::ui {

using current_lab::physics::EmDemo;
using current_lab::physics::RippleField;
using current_lab::physics::StringWave;
using current_lab::render::EmFieldView;

namespace {
// Какой «язык» аналогии в правой панели.
enum class Analogy { Water, Rope };

// Сцена + двойная подача: КРУПНАЯ детская подпись (≤8 слов, ребёнок понимает за 10 с)
// и «ага» для взрослого. Тексты — из docs/EM_ANALOGY_SPEC_2026-06-16.md.
struct SceneEntry {
    EmDemo demo;
    const char* label;       // подпись в комбо
    const char* child;       // КРУПНО: для ребёнка
    const char* aha;         // мелко: для взрослого
    Analogy analogy;
};
constexpr SceneEntry kScenes[] = {
    {EmDemo::DipoleRadiator,      "Излучатель (диполь)",
     "Брось камень — побегут круги",
     "Колеблющийся заряд излучает бегущие волны во все стороны — так работает антенна.",
     Analogy::Water},
    {EmDemo::DoubleSlit,         "Две щели",
     "Две волны: где встретились — горбик",
     "За двумя щелями не равномерно, а полосами: гребень+гребень — ярко, гребень+впадина — пусто (интерференция).",
     Analogy::Water},
    {EmDemo::DielectricInterface,"Преломление",
     "На мелком волны медленнее и теснее",
     "В плотной среде скорость и длина волны падают (v=c/n) — поэтому свет преломляется: линзы, очки, призма.",
     Analogy::Water},
    {EmDemo::Mirror,             "Зеркало",
     "Волна вернулась — верёвка прыгает на месте",
     "Падающая и отражённая волны дают стоячую: узлы стоят, пучности скачут (струна, орган, СВЧ-резонатор).",
     Analogy::Rope},
    {EmDemo::Waveguide,          "Волновод",
     "В канавке волна не разбегается",
     "Стенки ведут волну вдоль канала, наружу почти ничего — так работают оптоволокно и СВЧ-волноводы.",
     Analogy::Water},
    {EmDemo::PlaneWave,          "Плоская волна",
     "Волны бегут ровными рядами",
     "Далёкая волна: фронт плоский, гребни — прямые линии, всё движется синхронно со скоростью света.",
     Analogy::Water},
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));

int sceneIndex(EmDemo d) {
    for (int i = 0; i < kSceneCount; ++i)
        if (kScenes[i].demo == d) return i;
    return 0;
}
Analogy analogyOf(EmDemo d) { return kScenes[sceneIndex(d)].analogy; }

// Цвет воды: знаковая высота t∈[-1,1] → впадина (тёмно-синий) … гладь … гребень (пена).
// Общий «цветовой словарь» с полем слева (синее = в одну сторону).
uint32_t waterColor(float t) {
    using current_lab::render::blendColor;
    using current_lab::render::packColor;
    if (t < -1.0f) t = -1.0f; if (t > 1.0f) t = 1.0f;
    const uint32_t deep = packColor(10, 26, 64, 255);
    const uint32_t mid  = packColor(34, 102, 168, 255);
    const uint32_t foam = packColor(222, 244, 255, 255);
    return (t < 0.0f) ? blendColor(mid, deep, -t) : blendColor(mid, foam, t);
}
} // namespace

EmPanel::EmPanel() = default;

EmPanel::~EmPanel() {
    if (m_tex)      { GLuint t = m_tex;      glDeleteTextures(1, &t); }
    if (m_waterTex) { GLuint t = m_waterTex; glDeleteTextures(1, &t); }
}

void EmPanel::uploadRGBA(unsigned int& tex, int& tw, int& th,
                         int w, int h, const void* pixels) {
    if (w <= 0 || h <= 0) return;
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (w != tw || h != th) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        tw = w; th = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

// Построить рябь/верёвку под конкретную сцену (вызывается при смене сцены).
void EmPanel::configureAnalogy(EmDemo scene) {
    m_analogyScene = scene;
    m_waterNorm = 0.0;  // новая сцена — пересчитать нормировку цвета воды
    if (analogyOf(scene) == Analogy::Water) {
        m_string.reset();
        const int N = 130;
        m_ripple = std::make_unique<RippleField>(N);
        const double f = 0.06;
        switch (scene) {
            case EmDemo::DipoleRadiator:
                m_ripple->addDrivenSource(N / 2, N / 2, f, 1.0);
                break;
            case EmDemo::PlaneWave:
                for (int i = 1; i < N - 1; ++i) m_ripple->addDrivenSource(i, 3, 0.05, 1.0);
                break;
            case EmDemo::DoubleSlit: {
                for (int i = 1; i < N - 1; ++i) m_ripple->addDrivenSource(i, 6, 0.06, 1.0);
                const int jb = static_cast<int>(N * 0.42);   // стенка поперёк потока
                const int c = N / 2, sep = 13, half = 3;
                for (int i = 0; i < N; ++i) {
                    bool slitA = std::abs(i - (c - sep)) <= half;
                    bool slitB = std::abs(i - (c + sep)) <= half;
                    if (!slitA && !slitB) m_ripple->setBarrier(i, jb, true);
                }
                break;
            }
            case EmDemo::DielectricInterface:
                for (int i = 1; i < N - 1; ++i) m_ripple->addDrivenSource(i, 3, 0.05, 1.0);
                for (int i = 0; i < N; ++i)
                    for (int j = N / 2; j < N; ++j) m_ripple->setSpeedScale(i, j, 0.5f);
                break;
            case EmDemo::Waveguide: {
                const int c = N / 2, halfCh = 10;            // канал между двумя стенками
                for (int j = 0; j < N; ++j) {
                    m_ripple->setBarrier(c - halfCh, j, true);
                    m_ripple->setBarrier(c + halfCh, j, true);
                }
                m_ripple->addDrivenSource(c, 6, 0.07, 1.0);
                break;
            }
            default: break;
        }
    } else {
        m_ripple.reset();
        m_string = std::make_unique<StringWave>(220);
        m_string->setDrive(0.05, 1.0);
        m_string->setFarEnd(StringWave::FarEnd::Fixed); // зеркало: жёсткая стена → стоячая
        m_ropeEnv.assign(220, 0.0f);
    }
}

void EmPanel::drawWaterPane(float side) {
    if (!m_ripple) return;
    const int N = m_ripple->grid();
    // Нормировка по 95-му перцентилю |высоты|, а НЕ по максимуму: у источника
    // (капля/диполь) амплитуда на порядки больше ряби и «съела» бы палитру —
    // тогда круги не видны. Перцентиль насыщает источник, проявляет волны.
    // Для ДВУХ ЩЕЛЕЙ нормируем по ПРОШЕДШЕЙ за барьер области: иначе яркая входная
    // волна слева забивает контраст, и интерференция (главное в опыте!) бледная.
    int j0 = 0;
    if (m_analogyScene == EmDemo::DoubleSlit) j0 = static_cast<int>(N * 0.42) + 3;
    static std::vector<float> mags;
    mags.clear();
    mags.reserve(static_cast<size_t>(N) * N);
    for (int i = 0; i < N; ++i)
        for (int j = j0; j < N; ++j) mags.push_back(std::fabs(m_ripple->height(i, j)));
    double target = 1e-6;
    if (!mags.empty()) {
        size_t q = static_cast<size_t>(0.95 * (mags.size() - 1));
        std::nth_element(mags.begin(), mags.begin() + q, mags.end());
        target = std::max(1e-6f, mags[q]);
    }
    if (m_waterNorm <= 0.0) m_waterNorm = target;
    else m_waterNorm += ((target > m_waterNorm) ? 0.5 : 0.05) * (target - m_waterNorm);
    float norm = static_cast<float>(std::max(1e-6, m_waterNorm));
    static std::vector<uint32_t> px;
    px.resize(static_cast<size_t>(N) * N);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            px[static_cast<size_t>(i) * N + j] = waterColor(m_ripple->height(i, j) / norm);
    uploadRGBA(m_waterTex, m_waterW, m_waterH, N, N, px.data());

    ImVec2 p = ImGui::GetCursorScreenPos();
    if (m_waterTex)
        ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_waterTex)),
                     ImVec2(side, side));

    // Поверх воды — подсказки-стены/среды, чтобы ребёнок видел барьер, а не «гладь».
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto sx = [&](int j) { return p.x + side * (j + 0.5f) / N; };
    auto sy = [&](int i) { return p.y + side * (i + 0.5f) / N; };
    const ImU32 wall = IM_COL32(20, 24, 30, 235);
    EmDemo s = m_analogyScene;
    if (s == EmDemo::DoubleSlit) {
        const int jb = static_cast<int>(N * 0.42);
        const int c = N / 2, sep = 13, half = 3;
        float x = sx(jb);
        dl->AddRectFilled(ImVec2(x - 2, p.y), ImVec2(x + 2, p.y + side), wall);
        // прорезаем две щели прозрачностью (рисуем стену сегментами)
        for (int i = 0; i < N; ++i) {
            bool slit = std::abs(i - (c - sep)) <= half || std::abs(i - (c + sep)) <= half;
            if (slit) dl->AddRectFilled(ImVec2(x - 2.5f, sy(i) - side / N), ImVec2(x + 2.5f, sy(i) + side / N),
                                        IM_COL32(34, 102, 168, 255));
        }
    } else if (s == EmDemo::Waveguide) {
        const int c = N / 2, halfCh = 10;
        dl->AddRectFilled(ImVec2(p.x, sy(c - halfCh) - 2), ImVec2(p.x + side, sy(c - halfCh) + 2), wall);
        dl->AddRectFilled(ImVec2(p.x, sy(c + halfCh) - 2), ImVec2(p.x + side, sy(c + halfCh) + 2), wall);
    } else if (s == EmDemo::DielectricInterface) {
        float x = sx(N / 2);
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(p.x + side, p.y + side), IM_COL32(120, 90, 40, 40));
        dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + side), IM_COL32(200, 180, 120, 160), 1.5f);
        dl->AddText(ImVec2(x + 4, p.y + 4), IM_COL32(230, 215, 170, 220), "мелко");
    }
}

void EmPanel::drawRopePane(float side) {
    if (!m_string) return;
    const int n = m_string->n();
    double norm = m_string->maxAbs();
    if (norm < 1e-6) norm = 1e-6;
    // Огибающая |y| (затухающий максимум) — чтобы узлы стоячей волны были видны.
    if (static_cast<int>(m_ropeEnv.size()) != n) m_ropeEnv.assign(n, 0.0f);
    for (int i = 0; i < n; ++i) {
        float a = static_cast<float>(std::fabs(m_string->y(i)) / norm);
        m_ropeEnv[i] = std::max(m_ropeEnv[i] * 0.99f, a);
    }

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(16, 20, 26, 255));
    float midY = p.y + side * 0.5f;
    float amp = side * 0.34f;
    auto X = [&](int i) { return p.x + side * i / (n - 1); };

    // Стена справа (зеркало) и рука слева.
    dl->AddRectFilled(ImVec2(p.x + side - 6, p.y), ImVec2(p.x + side, p.y + side), IM_COL32(90, 60, 40, 255));
    dl->AddText(ImVec2(p.x + 6, midY - amp - 16), IM_COL32(180, 190, 200, 220), "рука трясёт");

    // Огибающая (бледная) — «коридор», в котором ходит верёвка; пучности широкие, узлы — щипки.
    for (int i = 0; i + 1 < n; ++i) {
        float e0 = m_ropeEnv[i] * amp, e1 = m_ropeEnv[i + 1] * amp;
        dl->AddLine(ImVec2(X(i), midY - e0), ImVec2(X(i + 1), midY - e1), IM_COL32(70, 110, 150, 120), 1.0f);
        dl->AddLine(ImVec2(X(i), midY + e0), ImVec2(X(i + 1), midY + e1), IM_COL32(70, 110, 150, 120), 1.0f);
    }
    // Сама верёвка (яркая).
    for (int i = 0; i + 1 < n; ++i) {
        float y0 = midY - static_cast<float>(m_string->y(i) / norm) * amp;
        float y1 = midY - static_cast<float>(m_string->y(i + 1) / norm) * amp;
        dl->AddLine(ImVec2(X(i), y0), ImVec2(X(i + 1), y1), IM_COL32(120, 210, 255, 255), 2.0f);
    }
    // Узлы: локальные минимумы огибающей около нуля — кружки «здесь не двигается».
    for (int i = 3; i < n - 3; ++i) {
        if (m_ropeEnv[i] < 0.18f && m_ropeEnv[i] <= m_ropeEnv[i - 2] && m_ropeEnv[i] <= m_ropeEnv[i + 2]) {
            dl->AddCircle(ImVec2(X(i), midY), 4.0f, IM_COL32(255, 220, 120, 230), 12, 1.5f);
        }
    }
}

void EmPanel::draw(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ЭМ-волны (live)", open)) {
        ImGui::End();
        return;
    }

    int cur = sceneIndex(m_scene.scene());
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("сцена", kScenes[cur].label)) {
        for (int i = 0; i < kSceneCount; ++i) {
            bool sel = (i == cur);
            if (ImGui::Selectable(kScenes[i].label, sel)) m_scene.setScene(kScenes[i].demo);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "Пауза" : "Старт")) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("Сброс")) { m_scene.reset(); configureAnalogy(m_scene.scene()); }
    ImGui::SameLine();
    bool stepOnce = ImGui::Button("Шаг");   // один кадр при паузе — застыть на гребне/узле
    ImGui::SameLine();
    bool emag = (m_field == EmFieldView::EMag);
    if (ImGui::Checkbox("|E|", &emag)) m_field = emag ? EmFieldView::EMag : EmFieldView::EzSigned;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderInt("скорость", &m_substeps, 0, 8);

    const SceneEntry& sc = kScenes[cur];

    // Пересобрать аналогию, если сменилась сцена.
    if (m_scene.scene() != m_analogyScene) configureAnalogy(m_scene.scene());

    // Один слайдер — обе панели идут синхронно (правило детопонятности №1-2).
    int steps = m_playing ? m_substeps : 0;
    if (stepOnce && !m_playing) steps = (m_substeps > 0 ? m_substeps : 2); // «Шаг» при паузе
    if (steps > 0) {
        m_scene.advance(steps);
        if (m_ripple) m_ripple->advance(steps);
        if (m_string) m_string->advance(steps);
    }
    current_lab::render::EmImage fieldImg = m_scene.image(m_plane, m_field);
    uploadRGBA(m_tex, m_texW, m_texH, fieldImg.w, fieldImg.h, fieldImg.bytes());

    float avail = ImGui::GetContentRegionAvail().x;
    float gap = 12.0f;
    float side = (avail - gap) * 0.5f;
    if (side > 380.0f) side = 380.0f;

    // СЛЕВА — настоящее поле.
    ImGui::BeginGroup();
    ImGui::TextDisabled("СЛЕВА: настоящая волна (поле Максвелла)");
    if (m_tex)
        ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_tex)), ImVec2(side, side));
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, gap);

    // СПРАВА — аналогия.
    ImGui::BeginGroup();
    ImGui::TextDisabled(sc.analogy == Analogy::Water ? "СПРАВА: как рябь на воде"
                                                     : "СПРАВА: как волна на верёвке");
    if (sc.analogy == Analogy::Water) drawWaterPane(side);
    else                              drawRopePane(side);
    ImGui::EndGroup();

    // КРУПНАЯ детская подпись.
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 210, 255, 255));
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextWrapped("%s", sc.child);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    // «Ага» для взрослого + телеметрия.
    ImGui::TextWrapped("Главное: %s", sc.aha);
    ImGui::TextDisabled("Слева цвет = знак поля; справа та же волна на воде/верёвке. t = %.2f нс, шагов: %d",
                        m_scene.simTime() * 1e9, m_scene.stepCount());

    ImGui::End();
}

} // namespace current_lab::ui
