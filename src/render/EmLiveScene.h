#pragma once
//
// EmLiveScene — ЖИВАЯ (анимируемая в реальном времени) ЭМ-сцена поверх FdtdField.
//
// В отличие от headless-капчера (render/EmCapture.h, один прогон → PNG), здесь поле
// ЖИВЁТ между кадрами: advance() шагает FDTD на несколько подшагов, image() отдаёт
// срез в RGBA (EmImage) для заливки в GL-текстуру (ImGui::Image). Нормировка палитры
// сглаживается между кадрами (EMA по перцентилю), иначе цвет «дышит»/мерцает, пока
// фронт растёт. Класс чистый: без GL/ImGui, тестируется по EmImage (см. test_em_live).
//
// Это левая («настоящее поле») панель двухпанельного ЭМ-режима; правая — наглядная
// аналогия (рябь на воде / волна на верёвке), которая делит с этой сценой параметры.

#include "physics/FdtdField.h"
#include "physics/EmScene.h"
#include "render/EmSliceImage.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>

namespace current_lab::render {

class EmLiveScene {
public:
    EmLiveScene(physics::EmDemo demo, int grid) { build(demo, grid); }

    // Перестроить с нуля (сброс поля и времени) — для кнопки «↺ reset».
    void reset() { build(demo_, grid_); }

    // Сменить сцену (диполь/щели/...) — перестраивает геометрию и материалы.
    void setScene(physics::EmDemo demo) { if (demo != demo_) build(demo, grid_); }

    // Сменить разрешение сетки — дороже шаг, чётче картинка.
    void setGrid(int grid) { if (grid != grid_) build(demo_, grid); }

    // Шагнуть поле на substeps подшагов (источник впрыскивается на каждом).
    void advance(int substeps) {
        for (int s = 0; s < substeps && substeps > 0; ++s) {
            physics::injectEmSource(*sim_, src_, step_);
            sim_->step();
            ++step_;
        }
    }

    // Срез поля в RGBA по средней плоскости. scale нормируется по 97-му перцентилю
    // |поля| и сглаживается между кадрами, чтобы волны были контрастны, а ближнее
    // поле источника насыщалось (та же логика, что в EmCapture, но «живая»).
    EmImage image(EmPlane plane, EmFieldView field) {
        const int slice = (plane == EmPlane::XY ? sim_->nz()
                         : plane == EmPlane::XZ ? sim_->ny() : sim_->nx()) / 2;
        double target = percentileScale(plane, field, slice);
        // EMA-сглаживание: подъём быстрый (видно зарождение фронта), спад медленный.
        if (scaleSmooth_ <= 0.0) scaleSmooth_ = target;
        else {
            double a = (target > scaleSmooth_) ? 0.5 : 0.05;
            scaleSmooth_ += a * (target - scaleSmooth_);
        }
        return renderEmSlice(*sim_, plane, slice, field, std::max(1e-30, scaleSmooth_));
    }

    int stepCount() const { return step_; }
    double simTime() const { return sim_->time(); }
    int grid() const { return grid_; }
    physics::EmDemo scene() const { return demo_; }

private:
    void build(physics::EmDemo demo, int grid) {
        demo_ = demo;
        grid_ = std::max(8, grid);
        physics::FdtdConfig cfg; cfg.nx = cfg.ny = cfg.nz = grid_;
        sim_ = std::make_unique<physics::FdtdField>(cfg);
        src_ = physics::buildEmScene(*sim_, demo_);
        step_ = 0;
        scaleSmooth_ = 0.0;
    }

    double percentileScale(EmPlane plane, EmFieldView field, int slice) const {
        int w = (plane == EmPlane::YZ ? sim_->ny() : sim_->nx());
        int h = (plane == EmPlane::XY ? sim_->ny() : sim_->nz());
        std::vector<double> vals;
        vals.reserve(static_cast<size_t>(w) * h);
        for (int b = 0; b < h; ++b)
            for (int a = 0; a < w; ++a) {
                int i = 0, j = 0, k = 0;
                switch (plane) {
                    case EmPlane::XY: i = a; j = b; k = slice; break;
                    case EmPlane::XZ: i = a; j = slice; k = b; break;
                    case EmPlane::YZ: i = slice; j = a; k = b; break;
                }
                double v = (field == EmFieldView::EzSigned)
                             ? std::fabs(static_cast<double>(sim_->ez(i, j, k)))
                             : sim_->eMag(i, j, k);
                vals.push_back(v);
            }
        if (vals.empty()) return 1e-30;
        size_t q = static_cast<size_t>(0.97 * (vals.size() - 1));
        std::nth_element(vals.begin(), vals.begin() + q, vals.end());
        return std::max(1e-30, vals[q]);
    }

    std::unique_ptr<physics::FdtdField> sim_;
    physics::EmSource src_;
    physics::EmDemo demo_ = physics::EmDemo::DipoleRadiator;
    int grid_ = 0;
    int step_ = 0;
    double scaleSmooth_ = 0.0;
};

} // namespace current_lab::render
