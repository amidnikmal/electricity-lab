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
    // Рамка мотора/генератора.
    m_motor.setCoil(100, 0.02);
    m_motor.setField(0.6);
    m_motor.setInertia(0.02);
}

// Рамка в поле, видимая «с торца»: два полюса (N слева, S справа) и диаметр-рамка,
// повёрнутый на angle; концы рамки — точки (ток втекает/вытекает). brightness>=0 —
// подсветка (для генератора), иначе серая.
void MagnetismPanel::drawRotatingLoop(const void* drawList, float cx, float cy, float r,
                                      float angle, int brightness, bool warm) {
    ImDrawList* dl = (ImDrawList*)drawList;
    // Полюса поля.
    dl->AddRectFilled(ImVec2(cx - r * 2.0f, cy - r), ImVec2(cx - r * 1.5f, cy + r), IM_COL32(200, 70, 70, 255));
    dl->AddRectFilled(ImVec2(cx + r * 1.5f, cy - r), ImVec2(cx + r * 2.0f, cy + r), IM_COL32(70, 110, 220, 255));
    dl->AddText(ImVec2(cx - r * 1.9f, cy - 8), IM_COL32(255, 255, 255, 230), "N");
    dl->AddText(ImVec2(cx + r * 1.6f, cy - 8), IM_COL32(255, 255, 255, 230), "S");
    // Ось/корпус ротора.
    dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(120, 130, 145, 200), 28, 1.5f);
    // Рамка (диаметр под углом).
    float ca = std::cos(angle), sa = std::sin(angle);
    ImVec2 e0(cx - r * ca, cy - r * sa), e1(cx + r * ca, cy + r * sa);
    dl->AddLine(e0, e1, IM_COL32(210, 180, 90, 255), 3.0f);
    dl->AddCircleFilled(e0, 5.0f, IM_COL32(230, 90, 90, 255));   // одна сторона (ток сюда)
    dl->AddCircleFilled(e1, 5.0f, IM_COL32(90, 150, 230, 255));  // другая (ток оттуда)
    (void)brightness; (void)warm;
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

void MagnetismPanel::drawGenerator() {
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

    ImGui::Checkbox("крутить самому (авто)", &m_genAuto);
    if (!m_genAuto) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240);
        ImGui::SliderFloat("скорость кручения", &m_crankOmega, 0.0f, 14.0f, "%.1f рад/с");
    }
    float omega = m_genAuto ? 8.0f : m_crankOmega;
    m_motor.stepGenerator(omega, dt);
    float emf = static_cast<float>(m_motor.lastEmf());
    float brightness = std::clamp(std::fabs(emf) / (std::fabs(emf) + 1.0f), 0.0f, 1.0f);
    m_genHist[m_genHead] = emf;
    m_genHead = (m_genHead + 1) % 240;

    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 120, 255));
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextWrapped("Крутишь рамку — лампочка горит.");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    float avail = ImGui::GetContentRegionAvail().x, gap = 12.0f;
    float side = std::min((avail - gap) * 0.5f, 380.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::BeginGroup();
    ImGui::TextDisabled("СЛЕВА: честно — рамка в поле + ЭДС");
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(16, 20, 26, 255));
    drawRotatingLoop(dl, p.x + side * 0.5f, p.y + side * 0.30f, side * 0.16f,
                     static_cast<float>(m_motor.angle()), -1, true);
    // График ЭДС (синус).
    float gMid = p.y + side * 0.72f, gH = side * 0.34f;
    dl->AddLine(ImVec2(p.x + 8, gMid), ImVec2(p.x + side - 8, gMid), IM_COL32(60, 70, 84, 200));
    float emax = 1e-6f;
    for (float e : m_genHist) emax = std::max(emax, std::fabs(e));
    for (int k = 0; k + 1 < 240; ++k) {
        int i0 = (m_genHead + k) % 240, i1 = (m_genHead + k + 1) % 240;
        float x0 = p.x + 8 + (side - 16) * k / 239.0f, x1 = p.x + 8 + (side - 16) * (k + 1) / 239.0f;
        dl->AddLine(ImVec2(x0, gMid - m_genHist[i0] / emax * gH * 0.45f),
                    ImVec2(x1, gMid - m_genHist[i1] / emax * gH * 0.45f), IM_COL32(120, 210, 255, 255), 1.8f);
    }
    dl->AddText(ImVec2(p.x + 8, gMid - gH * 0.5f - 16), IM_COL32(150, 160, 170, 220), "ЭДС ~ sin (переменный ток)");
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    ImGui::TextDisabled("СПРАВА: динамо → лампочка");
    ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(q, ImVec2(q.x + side, q.y + side), IM_COL32(14, 16, 22, 255));
    // ручка-кривошип крутится
    ImVec2 hub(q.x + side * 0.34f, q.y + side * 0.42f);
    float hr = side * 0.16f, ang = static_cast<float>(m_motor.angle());
    dl->AddCircle(hub, hr, IM_COL32(150, 160, 175, 220), 24, 2.0f);
    ImVec2 handle(hub.x + hr * std::cos(ang), hub.y + hr * std::sin(ang));
    dl->AddLine(hub, handle, IM_COL32(210, 180, 90, 255), 3.0f);
    dl->AddCircleFilled(handle, 6.0f, IM_COL32(230, 200, 120, 255));
    dl->AddText(ImVec2(hub.x - 20, hub.y + hr + 6), IM_COL32(200, 205, 215, 220), "крути");
    // лампа
    ImVec2 lamp(q.x + side * 0.78f, q.y + side * 0.42f);
    float lr = side * 0.12f; int a = static_cast<int>(40 + 215 * brightness);
    for (int g = 3; g >= 1; --g)
        dl->AddCircleFilled(lamp, lr * (1.0f + 0.5f * g), IM_COL32(255, 220, 90, static_cast<int>(a * 0.12f)));
    dl->AddCircleFilled(lamp, lr, IM_COL32(255, 220, 90, a));
    dl->AddCircle(lamp, lr, IM_COL32(230, 230, 235, 200), 24, 2.0f);
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::TextWrapped("Главное: вращение рамки в поле наводит переменную ЭДС (ε=N·B·A·ω·sin). "
                       "Это генератор: динамо фонарика, велогенератор, все электростанции.");
    ImGui::TextDisabled("ЭДС = %.2f В, яркость %.0f%%, ω = %.1f рад/с", emf, brightness * 100.0f, omega);
}

void MagnetismPanel::drawMotor() {
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

    ImGui::Checkbox("батарейка включена (ток)", &m_batteryOn);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("ток", &m_motorCurrent, 0.0f, 5.0f, "%.1f А");
    float current = m_batteryOn ? m_motorCurrent : 0.0f;

    // Кинематика: целевая скорость ∝ току, плавный разгон/торможение (как с трением).
    float targetOmega = current * 2.2f;
    m_motorOmega += (targetOmega - m_motorOmega) * std::min(1.0f, dt * 2.5f);
    m_motorAngle += m_motorOmega * dt;
    float tq = static_cast<float>(m_motor.torque(current, m_motorAngle));

    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 120, 255));
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextWrapped("Дашь ток — рамка крутится.");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    float avail = ImGui::GetContentRegionAvail().x, gap = 12.0f;
    float side = std::min((avail - gap) * 0.5f, 380.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::BeginGroup();
    ImGui::TextDisabled("СЛЕВА: честно — ток в рамке → момент");
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(16, 20, 26, 255));
    drawRotatingLoop(dl, p.x + side * 0.5f, p.y + side * 0.42f, side * 0.18f, m_motorAngle, -1, true);
    dl->AddText(ImVec2(p.x + 8, p.y + side - 24), IM_COL32(150, 160, 170, 220), "момент tau = N*I*A*B*sin(theta)");
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    ImGui::TextDisabled("СПРАВА: батарейка → вентилятор");
    ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(q, ImVec2(q.x + side, q.y + side), IM_COL32(14, 16, 22, 255));
    // батарейка
    ImVec2 bat(q.x + side * 0.20f, q.y + side * 0.42f);
    ImU32 batCol = m_batteryOn ? IM_COL32(90, 200, 110, 255) : IM_COL32(90, 100, 110, 255);
    dl->AddRectFilled(ImVec2(bat.x - 14, bat.y - 22), ImVec2(bat.x + 14, bat.y + 22), batCol, 3.0f);
    dl->AddText(ImVec2(bat.x - 6, bat.y - 8), IM_COL32(20, 24, 30, 255), m_batteryOn ? "+" : "o");
    dl->AddText(ImVec2(bat.x - 28, bat.y + 26), IM_COL32(200, 205, 215, 220), "батарейка");
    // вентилятор-крыльчатка крутится с m_motorOmega
    ImVec2 fan(q.x + side * 0.66f, q.y + side * 0.42f);
    float fr = side * 0.20f;
    for (int b = 0; b < 4; ++b) {
        float a = m_motorAngle + b * 1.5707963f;
        dl->AddLine(fan, ImVec2(fan.x + fr * std::cos(a), fan.y + fr * std::sin(a)),
                    IM_COL32(120, 190, 240, 255), 5.0f);
    }
    dl->AddCircleFilled(fan, 6.0f, IM_COL32(200, 210, 220, 255));
    dl->AddText(ImVec2(fan.x - 28, fan.y + fr + 6), IM_COL32(200, 205, 215, 220), "вентилятор");
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::TextWrapped("Главное: ток в рамке в поле даёт момент (tau=N·I·A·B·sin) — рамка крутится. "
                       "Это мотор: вентилятор, дрель, электромобиль. То же устройство, что генератор, наоборот.");
    ImGui::TextDisabled("ток = %.1f А, момент = %.3f, ω = %.1f рад/с", current, tq, m_motorOmega);
}

void MagnetismPanel::drawLorentz() {
    using current_lab::physics::borisStep;
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

    bool reset = false;
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("поле B", &m_lorB, 0.3f, 3.0f, "%.1f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("эл. поле E", &m_lorE, 0.0f, 1.5f, "%.2f")) {}
    ImGui::SameLine();
    if (ImGui::Button("Сброс")) reset = true;

    if (!m_lorInit || reset) {
        m_lor.pos = Vec2(2.0, 0.0);
        m_lor.vel = Vec2(0.0, 2.0);
        m_lorTrailHead = 0; m_lorTrailCount = 0;
        m_lorInit = true;
    }
    // Несколько подшагов Boris для гладкой орбиты (q=1, m=1).
    for (int s = 0; s < 5; ++s) {
        m_lor = borisStep(m_lor, 1.0, 1.0, Vec2(m_lorE, 0.0), m_lorB, 0.02);
        m_lorTrail[m_lorTrailHead] = m_lor.pos;
        m_lorTrailHead = (m_lorTrailHead + 1) % 160;
        if (m_lorTrailCount < 160) ++m_lorTrailCount;
    }

    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 120, 255));
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextWrapped("Тянет вбок — шарик кружит.");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    float avail = ImGui::GetContentRegionAvail().x, gap = 12.0f;
    float side = std::min((avail - gap) * 0.5f, 380.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float scale = side * 0.16f;

    // СЛЕВА: честно — заряд в поле B, скорость и сила.
    ImGui::BeginGroup();
    ImGui::TextDisabled("СЛЕВА: честно — заряд в поле B");
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side), IM_COL32(16, 20, 26, 255));
    float cx = p.x + side * 0.5f, cy = p.y + side * 0.5f;
    auto W2S = [&](Vec2 w) { return ImVec2(cx + static_cast<float>(w.x) * scale,
                                           cy + static_cast<float>(w.y) * scale); };
    // Поле B «в плоскость» — сетка крестиков.
    for (int gx = -2; gx <= 2; ++gx)
        for (int gy = -2; gy <= 2; ++gy) {
            ImVec2 c(cx + gx * side * 0.18f, cy + gy * side * 0.18f);
            dl->AddLine(ImVec2(c.x - 3, c.y - 3), ImVec2(c.x + 3, c.y + 3), IM_COL32(70, 80, 95, 160));
            dl->AddLine(ImVec2(c.x - 3, c.y + 3), ImVec2(c.x + 3, c.y - 3), IM_COL32(70, 80, 95, 160));
        }
    // След.
    for (int k = 1; k < m_lorTrailCount; ++k) {
        int i0 = (m_lorTrailHead - m_lorTrailCount + k - 1 + 320) % 160;
        int i1 = (m_lorTrailHead - m_lorTrailCount + k + 320) % 160;
        int a = 40 + 180 * k / m_lorTrailCount;
        dl->AddLine(W2S(m_lorTrail[i0]), W2S(m_lorTrail[i1]), IM_COL32(120, 200, 255, a), 1.5f);
    }
    // Частица + стрелки скорости (синяя) и силы Лоренца F=q·v×B (красная, ⟂ скорости).
    ImVec2 pp = W2S(m_lor.pos);
    Vec2 vel = m_lor.vel;
    Vec2 force(vel.y * m_lorB, -vel.x * m_lorB); // q=1
    auto arrow = [&](ImVec2 from, Vec2 d, float len, ImU32 col) {
        double n = std::sqrt(d.x * d.x + d.y * d.y); if (n < 1e-6) return;
        ImVec2 to(from.x + static_cast<float>(d.x / n) * len, from.y + static_cast<float>(d.y / n) * len);
        dl->AddLine(from, to, col, 2.5f);
        dl->AddCircleFilled(to, 3.0f, col);
    };
    arrow(pp, vel, side * 0.14f, IM_COL32(110, 200, 255, 255));   // скорость
    arrow(pp, force, side * 0.14f, IM_COL32(240, 110, 110, 255)); // сила (вбок)
    dl->AddCircleFilled(pp, 6.0f, IM_COL32(255, 230, 120, 255));
    dl->AddText(ImVec2(p.x + 8, p.y + side - 22), IM_COL32(150, 160, 170, 220), "x x x  B (в плоскость)");
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, gap);
    // СПРАВА: аналогия — шарик на верёвочке вокруг колышка.
    ImGui::BeginGroup();
    ImGui::TextDisabled("СПРАВА: шарик на верёвочке");
    ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    dl->AddRectFilled(q, ImVec2(q.x + side, q.y + side), IM_COL32(14, 16, 22, 255));
    ImVec2 peg(q.x + side * 0.5f, q.y + side * 0.5f);
    // угол шарика берём из положения частицы (для синхронности с левой).
    float ang = std::atan2(static_cast<float>(m_lor.pos.y), static_cast<float>(m_lor.pos.x));
    float rr = side * 0.30f;
    ImVec2 ball(peg.x + rr * std::cos(ang), peg.y + rr * std::sin(ang));
    dl->AddCircle(peg, rr, IM_COL32(60, 70, 84, 160), 40, 1.0f);   // траектория-круг
    dl->AddCircleFilled(peg, 5.0f, IM_COL32(150, 160, 175, 255));  // колышек
    dl->AddLine(peg, ball, IM_COL32(210, 180, 90, 255), 2.5f);     // верёвочка (сила к центру)
    dl->AddCircleFilled(ball, 9.0f, IM_COL32(255, 230, 120, 255)); // шарик
    dl->AddText(ImVec2(peg.x - 24, peg.y + 8), IM_COL32(200, 205, 215, 210), "тянет к центру");
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::TextWrapped("Главное: магнитная сила всегда ⟂ скорости (F=q·v×B) — не разгоняет, а "
                       "закручивает. Радиус r=mv/(qB): сильнее поле → круг меньше. Так работают "
                       "циклотрон, масс-спектрометр, удержание плазмы в токамаке.");
    double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    ImGui::TextDisabled("|v| = %.2f, B = %.1f, радиус r=mv/(qB) ≈ %.2f", speed, m_lorB,
                        m_lorB > 1e-3 ? speed / m_lorB : 0.0);
}

void MagnetismPanel::draw(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(840, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Магнетизм (live)", open)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("magmodes")) {
        if (ImGui::BeginTabItem("Индукция (магнит)")) { drawFaraday();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Генератор")) { drawGenerator(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Мотор")) { drawMotor();     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Лоренц")) { drawLorentz();  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace current_lab::ui
