#include "ui/EmPanel.h"

#include "gl_setup.h"
#include "imgui.h"
#include "physics/EmScene.h"

#include <cstdint>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace current_lab::ui {

using current_lab::physics::EmDemo;
using current_lab::render::EmFieldView;

namespace {
// Сцены + русские подписи и короткая «детская» аналогия (правая панель — потом).
struct SceneEntry { EmDemo demo; const char* label; const char* analogy; };
constexpr SceneEntry kScenes[] = {
    {EmDemo::DipoleRadiator,      "Излучатель (диполь)", "камень бросили в воду — круги"},
    {EmDemo::DoubleSlit,         "Две щели",            "волны через два прохода — полосы"},
    {EmDemo::DielectricInterface,"Преломление",         "волна входит в мелководье — загиб"},
    {EmDemo::Mirror,             "Зеркало",             "волна на верёвке от стены — стоячая"},
    {EmDemo::Waveguide,          "Волновод",            "вода в жёлобе — идёт вдоль"},
    {EmDemo::PlaneWave,          "Плоская волна",       "ровный фронт бежит вперёд"},
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));

int sceneIndex(EmDemo d) {
    for (int i = 0; i < kSceneCount; ++i)
        if (kScenes[i].demo == d) return i;
    return 0;
}
} // namespace

EmPanel::EmPanel() = default;

EmPanel::~EmPanel() {
    if (m_tex) {
        GLuint t = m_tex;
        glDeleteTextures(1, &t);
        m_tex = 0;
    }
}

void EmPanel::uploadTexture(const current_lab::render::EmImage& img) {
    if (img.w <= 0 || img.h <= 0) return;
    if (!m_tex) glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    if (img.w != m_texW || img.h != m_texH) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.bytes());
        m_texW = img.w; m_texH = img.h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.w, img.h,
                        GL_RGBA, GL_UNSIGNED_BYTE, img.bytes());
    }
}

void EmPanel::draw(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ЭМ-волны (live)", open)) {
        ImGui::End();
        return;
    }

    // --- контролы ---
    int cur = sceneIndex(m_scene.scene());
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("сцена", kScenes[cur].label)) {
        for (int i = 0; i < kSceneCount; ++i) {
            bool sel = (i == cur);
            if (ImGui::Selectable(kScenes[i].label, sel))
                m_scene.setScene(kScenes[i].demo);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "Пауза" : "Старт")) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("Сброс")) m_scene.reset();

    bool emag = (m_field == EmFieldView::EMag);
    if (ImGui::Checkbox("|E| (интенсивность)", &emag))
        m_field = emag ? EmFieldView::EMag : EmFieldView::EzSigned;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("скорость", &m_substeps, 0, 8);

    // --- шаг поля + заливка текстуры ---
    if (m_playing) m_scene.advance(m_substeps);
    uploadTexture(m_scene.image(m_plane, m_field));

    // --- картинка поля ---
    float side = ImGui::GetContentRegionAvail().x;
    if (side > 520.0f) side = 520.0f;
    if (m_tex && side > 16.0f)
        ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_tex)),
                     ImVec2(side, side));

    ImGui::TextDisabled("Цвет = знак поля Ez; фронты бегут со скоростью света. t = %.2f нс, шагов: %d",
                        m_scene.simTime() * 1e9, m_scene.stepCount());
    ImGui::TextWrapped("Аналогия: %s", kScenes[cur].analogy);

    ImGui::End();
}

} // namespace current_lab::ui
