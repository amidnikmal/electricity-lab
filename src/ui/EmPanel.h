#pragma once
//
// EmPanel — ImGui-окно живого ЭМ-режима: СЛЕВА настоящее поле (FDTD, EmLiveScene),
// анимируемое в реальном времени и залитое в GL-текстуру. Контролы: сцена, старт/пауза,
// сброс, поле Ez/|E|, скорость (подшагов на кадр). Правая панель-аналогия (рябь на воде /
// волна на верёвке) добавляется следующим куском — здесь готовится левая половина.
//
// GL-зависимый (glGenTextures/glTexImage2D из gl_setup.h) — поэтому в .cpp, вне тестов.

#include "render/EmLiveScene.h"

namespace current_lab::ui {

class EmPanel {
public:
    EmPanel();
    ~EmPanel();

    // Рисует окно внутри уже открытого ImGui-кадра. open управляет видимостью.
    void draw(bool* open);

private:
    void uploadTexture(const current_lab::render::EmImage& img);

    current_lab::render::EmLiveScene m_scene{current_lab::physics::EmDemo::DipoleRadiator, 64};
    unsigned int m_tex = 0;          // GLuint
    int m_texW = 0, m_texH = 0;
    bool m_playing = true;
    int m_substeps = 3;              // шагов FDTD на кадр (скорость волны на экране)
    current_lab::render::EmPlane m_plane = current_lab::render::EmPlane::XY;
    current_lab::render::EmFieldView m_field = current_lab::render::EmFieldView::EzSigned;
};

} // namespace current_lab::ui
