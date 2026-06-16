#pragma once
//
// MagnetismPanel — двухпанельный режим магнетизма (по тому же лекалу, что ЭМ-волны):
// СЛЕВА честная физика (катушка + магнит + график ЭДС из InductionModel), СПРАВА
// наглядная аналогия — двигаешь магнит, лампочка вспыхивает; стоит магнит — темно.
// Сюда же ляжет мотор/генератор (вкладка) — ядро MotorGenerator.
//
// GL/ImGui-зависимый рисунок — в .cpp (вне тестов). Физика — чистый InductionModel.

#include "physics/InductionModel.h"

namespace current_lab::ui {

class MagnetismPanel {
public:
    MagnetismPanel();
    void draw(bool* open);

private:
    void drawFaraday();

    current_lab::physics::InductionModel m_induction;
    // Позиция магнита вдоль оси (катушка в 0). Скорость берём из изменения позиции.
    float m_magPos = 2.5f;
    float m_magPrev = 2.5f;
    bool  m_autoMove = true;   // магнит сам колеблется (демо без рук)
    float m_t = 0.0f;
    float m_lastEmf = 0.0f;
    // Кольцевой буфер истории ЭДС для бегущего графика.
    static constexpr int kHist = 240;
    float m_emfHist[kHist] = {};
    int   m_histHead = 0;
};

} // namespace current_lab::ui
