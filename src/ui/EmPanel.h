#pragma once
//
// EmPanel — ImGui-окно живого ЭМ-режима: СЛЕВА настоящее поле (FDTD, EmLiveScene),
// анимируемое в реальном времени и залитое в GL-текстуру. Контролы: сцена, старт/пауза,
// сброс, поле Ez/|E|, скорость (подшагов на кадр). Правая панель-аналогия (рябь на воде /
// волна на верёвке) добавляется следующим куском — здесь готовится левая половина.
//
// GL-зависимый (glGenTextures/glTexImage2D из gl_setup.h) — поэтому в .cpp, вне тестов.

#include "render/EmLiveScene.h"
#include "physics/RippleField.h"
#include "physics/StringWave.h"
#include <memory>
#include <vector>

struct ImDrawList;
struct ImVec2;

namespace current_lab::ui {

// Двухпанельный ЭМ-режим: СЛЕВА настоящее поле Максвелла (FDTD), СПРАВА наглядная
// аналогия — рябь на воде (RippleField) или волна на верёвке (StringWave). Обе панели
// приводятся ОДНИМ слайдером скорости и идут синхронно (правило детопонятности №1-2).
class EmPanel {
public:
    EmPanel();
    ~EmPanel();

    // Рисует окно внутри уже открытого ImGui-кадра. open управляет видимостью.
    void draw(bool* open);

private:
    void uploadRGBA(unsigned int& tex, int& tw, int& th,
                    int w, int h, const void* pixels);
    void configureAnalogy(current_lab::physics::EmDemo scene); // настроить рябь/верёвку под сцену
    void drawWaterPane(float side);  // правая панель: рябь на воде
    void drawRopePane(float side);   // правая панель: волна на верёвке

    current_lab::render::EmLiveScene m_scene{current_lab::physics::EmDemo::DipoleRadiator, 64};
    unsigned int m_tex = 0;          // GLuint поля
    int m_texW = 0, m_texH = 0;
    bool m_playing = true;
    int m_substeps = 3;              // шагов на кадр — ОБЩАЯ скорость обеих панелей
    current_lab::render::EmPlane m_plane = current_lab::render::EmPlane::XY;
    current_lab::render::EmFieldView m_field = current_lab::render::EmFieldView::EzSigned;

    // --- правая панель-аналогия ---
    std::unique_ptr<current_lab::physics::RippleField> m_ripple; // для «водяных» сцен
    std::unique_ptr<current_lab::physics::StringWave> m_string;  // для «верёвочных» сцен
    unsigned int m_waterTex = 0;     // GLuint ряби
    int m_waterW = 0, m_waterH = 0;
    std::vector<float> m_ropeEnv;    // огибающая |y| по верёвке — для показа узлов
    current_lab::physics::EmDemo m_analogyScene = current_lab::physics::EmDemo::Count; // настроено под
};

} // namespace current_lab::ui
