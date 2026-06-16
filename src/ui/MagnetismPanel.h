#pragma once
//
// MagnetismPanel — двухпанельный режим магнетизма (по тому же лекалу, что ЭМ-волны):
// СЛЕВА честная физика (катушка + магнит + график ЭДС из InductionModel), СПРАВА
// наглядная аналогия — двигаешь магнит, лампочка вспыхивает; стоит магнит — темно.
// Сюда же ляжет мотор/генератор (вкладка) — ядро MotorGenerator.
//
// GL/ImGui-зависимый рисунок — в .cpp (вне тестов). Физика — чистый InductionModel.

#include "physics/InductionModel.h"
#include "physics/MotorGenerator.h"

namespace current_lab::ui {

class MagnetismPanel {
public:
    MagnetismPanel();
    void draw(bool* open);

private:
    void drawFaraday();
    void drawGenerator();   // крутишь рамку → горит лампа
    void drawMotor();       // подаёшь ток → рамка крутится
    void drawRotatingLoop(const void* drawList, float cx, float cy, float r,
                          float angle, int brightness, bool warm);

    current_lab::physics::InductionModel m_induction;
    current_lab::physics::MotorGenerator m_motor;
    // Генератор: угловая скорость кривошипа (рад/с), авто-кручение.
    float m_crankOmega = 6.0f;
    bool  m_genAuto = true;
    // Мотор: ток (батарейка вкл/выкл задаёт ток). Вращение кинематическое
    // (целевая скорость ∝ току, плавный разгон) — ядро MotorGenerator без трения
    // разгонялось бы без предела; для демки «ток → крутится» этого достаточно.
    bool  m_batteryOn = false;
    float m_motorCurrent = 2.0f;
    float m_motorOmega = 0.0f;
    float m_motorAngle = 0.0f;
    // Бегущий график для генератора (ЭДС синус).
    float m_genHist[240] = {};
    int   m_genHead = 0;
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
