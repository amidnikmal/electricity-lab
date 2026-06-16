#include "ui/MagnetismPanel.h"

#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace current_lab::ui {

namespace {
constexpr float kAmp = 3.0f;       // размах хода магнита вдоль оси
constexpr float kAutoFreq = 0.35f; // Гц, авто-колебание магнита
constexpr float kPi = 3.14159265358979323846f;
} // namespace

MagnetismPanel::MagnetismPanel() {
    // Параметры катушки/магнита: лампа уверенно вспыхивает при движении (e0=2e-3 в ядре).
    m_induction.setCoil(120, 0.6);
    m_induction.setMagnet(8.0);
}

void MagnetismPanel::drawFaraday() {
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f; // защита от пауз/первого кадра
    m_t += dt;

    // --- управление магнитом ---
    ImGui::Checkbox("магнит сам двигается", &m_autoMove);
    if (m_autoMove) {
        m_magPos = kAmp * std::sin(2.0f * kPi * kAutoFreq * m_t);
    } else {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240);
        ImGui::SliderFloat("позиция магнита", &m_magPos, -kAmp, kAmp, "%.2f");
    }

    // Скорость магнита из изменения позиции (работает и в авто, и при перетаскивании).
    float v = (m_magPos - m_magPrev) / dt;
    m_magPrev = m_magPos;

    m_lastEmf = static_cast<float>(m_induction.emf(m_magPos, v));
    float brightness = static_cast<float>(m_induction.lampBrightness(m_magPos, v));
    m_emfHist[m_histHead] = m_lastEmf;
    m_histHead = (m_histHead + 1) % kHist;

    // --- КРУПНАЯ детская подпись ---
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 120, 255));
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextWrapped("Двигаешь магнит — лампочка вспыхивает. Стоит — темно.");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 4));

    float avail = ImGui::GetContentRegionAvail().x;
    float gap = 12.0f;
    float side = (avail - gap) * 0.5f;
    if (side > 380.0f) side = 380.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto magScreenX = [&](ImVec2 p, float w) {
        return p.x + w * 0.5f + (m_magPos / kAmp) * (w * 0.42f);
    };
    auto drawMagnet = [&](ImVec2 c, float h) {
        float mw = 26, mh = h;
        // N (красный) | S (синий)
        dl->AddRectFilled(ImVec2(c.x - mw, c.y - mh / 2), ImVec2(c.x, c.y + mh / 2), IM_COL32(200, 60, 60, 255));
        dl->AddRectFilled(ImVec2(c.x, c.y - mh / 2), ImVec2(c.x + mw, c.y + mh / 2), IM_COL32(70, 110, 220, 255));
        dl->AddText(ImVec2(c.x - mw + 4, c.y - 8), IM_COL32(255, 255, 255, 230), "N");
        dl->AddText(ImVec2(c.x + 6, c.y - 8), IM_COL32(255, 255, 255, 230), "S");
    };
    auto drawCoil = [&](ImVec2 c, float h) {
        for (int k = -2; k <= 2; ++k) {
            float x = c.x + k * 7.0f;
            dl->AddLine(ImVec2(x, c.y - h / 2), ImVec2(x, c.y + h / 2), IM_COL32(180, 180, 190, 200), 2.0f);
            dl->AddBezierQuadratic(ImVec2(x, c.y - h / 2), ImVec2(x + 5, c.y - h / 2 - 6),
                                   ImVec2(x + 7, c.y - h / 2), IM_COL32(180, 180, 190, 200), 1.5f);
        }
    };

    // ===== СЛЕВА: честно (катушка + магнит + график ЭДС) =====
    ImGui::BeginGroup();
    ImGui::TextDisabled("СЛЕВА: честно — катушка, магнит, ЭДС");
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(16, 20, 26, 255));
    float axisY = p.y + side * 0.34f;
    dl->AddLine(ImVec2(p.x + 8, axisY), ImVec2(p.x + side - 8, axisY), IM_COL32(60, 70, 84, 255));
    drawCoil(ImVec2(p.x + side * 0.5f, axisY), side * 0.30f);
    drawMagnet(ImVec2(magScreenX(p, side), axisY), side * 0.20f);
    // График ЭДС (бегущий) в нижней половине.
    float gY0 = p.y + side * 0.60f, gH = side * 0.34f, gMid = gY0 + gH * 0.5f;
    dl->AddLine(ImVec2(p.x + 8, gMid), ImVec2(p.x + side - 8, gMid), IM_COL32(60, 70, 84, 200));
    dl->AddText(ImVec2(p.x + 8, gY0 - 16), IM_COL32(150, 160, 170, 220), "ЭДС = -N dФ/dt");
    float emax = 1e-6f;
    for (float e : m_emfHist) emax = std::max(emax, std::fabs(e));
    for (int k = 0; k + 1 < kHist; ++k) {
        int i0 = (m_histHead + k) % kHist, i1 = (m_histHead + k + 1) % kHist;
        float x0 = p.x + 8 + (side - 16) * k / (kHist - 1);
        float x1 = p.x + 8 + (side - 16) * (k + 1) / (kHist - 1);
        float y0 = gMid - (m_emfHist[i0] / emax) * gH * 0.45f;
        float y1 = gMid - (m_emfHist[i1] / emax) * gH * 0.45f;
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(120, 210, 255, 255), 1.8f);
    }
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, gap);

    // ===== СПРАВА: аналогия (магнит → лампочка) =====
    ImGui::BeginGroup();
    ImGui::TextDisabled("СПРАВА: как лампочка от магнита");
    ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(q, ImVec2(q.x + side, q.y + side), IM_COL32(14, 16, 22, 255));
    float rowY = q.y + side * 0.42f;
    drawCoil(ImVec2(q.x + side * 0.34f, rowY), side * 0.26f);
    drawMagnet(ImVec2(magScreenX(q, side * 0.68f), rowY), side * 0.18f);

    // Лампочка: свечение ∝ яркости; тёплый при +ЭДС, холодный при −ЭДС (знак Ленца).
    ImVec2 lamp(q.x + side * 0.80f, q.y + side * 0.42f);
    float lr = side * 0.12f;
    bool warm = (m_lastEmf >= 0.0f);
    int rr = warm ? 255 : 150, gg = warm ? 220 : 200, bb = warm ? 90 : 255;
    int a = static_cast<int>(40 + 215 * std::clamp(brightness, 0.0f, 1.0f));
    // ореол
    for (int g = 3; g >= 1; --g)
        dl->AddCircleFilled(lamp, lr * (1.0f + 0.5f * g),
                            IM_COL32(rr, gg, bb, static_cast<int>(a * 0.12f)));
    dl->AddCircleFilled(lamp, lr, IM_COL32(rr, gg, bb, a));
    dl->AddCircle(lamp, lr, IM_COL32(230, 230, 235, 200), 24, 2.0f);
    dl->AddText(ImVec2(lamp.x - 24, lamp.y + lr + 6), IM_COL32(200, 205, 215, 220), "лампочка");
    ImGui::EndGroup();

    // «Главное» для взрослого + телеметрия.
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::TextWrapped("Главное: ток рождает не близость магнита, а его ДВИЖЕНИЕ (изменение потока). "
                       "Быстрее → ярче; стоит → ноль; назад → меняет знак. На этом вся электроэнергетика.");
    ImGui::TextDisabled("ЭДС = %.3f В,  яркость = %.0f%%  (магнит x=%.2f, v=%.2f)",
                        m_lastEmf, brightness * 100.0f, m_magPos, v);
}

void MagnetismPanel::draw(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Магнетизм (live)", open)) {
        ImGui::End();
        return;
    }
    drawFaraday();   // позже — вкладки: Фарадей | Мотор/генератор
    ImGui::End();
}

} // namespace current_lab::ui
