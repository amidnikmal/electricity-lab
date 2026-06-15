#pragma once
//
// FdtdField — 3D-решатель уравнений Максвелла методом FDTD (сетка Йи, чехарда E/H).
//
// Это ОТДЕЛЬНЫЙ движок ЭМ-поля; к цепному MNA-солверу отношения не имеет.
// Решает полную систему Максвелла (роторные уравнения с током смещения):
//
//     ∂H/∂t = -(1/μ₀) ∇×E
//     ∂E/∂t =  (1/(ε₀ε_r)) (∇×H - σE)
//
// Сетка Йи: компоненты E и H разнесены на полшага, что даёт устойчивую
// центрально-разностную схему второго порядка. Шаг по времени берётся из условия
// Куранта: dt = courant·ds/(c·√3), c = 1/√(μ₀ε₀).
//
// Материалы заданы поячеечно: относительная ε_r, проводимость σ (она же несёт
// поглощающий слой у границ — graded-σ sponge) и маска идеального проводника (PEC).
// Чистый класс без UI/GL, покрыт tests/test_fdtd.cpp.
//
// СЛЕДУЮЩИЕ ШАГИ (docs/EM_FDTD_PLAN.md): TFSF-плоская волна, CPML, демо-сцены, рендер.

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace current_lab::physics {

struct FdtdConfig {
    int nx = 60;
    int ny = 60;
    int nz = 60;
    double cellSize = 1e-3;     // ds, метры (шаг сетки)
    double courant  = 0.5;      // запас по устойчивости (< 1/√3 ≈ 0.577 для 3D)
    int    absorbCells = 8;     // толщина поглощающего слоя у каждой грани
    double absorbSigmaMax = 0.0;// если 0 — подбирается автоматически в finalize()
};

class FdtdField {
public:
    using Real = float;

    // Физические константы (СИ).
    static constexpr double kEps0 = 8.8541878128e-12;
    static constexpr double kMu0  = 1.25663706212e-6;
    static double lightSpeed() { return 1.0 / std::sqrt(kMu0 * kEps0); }

    explicit FdtdField(const FdtdConfig& cfg) : cfg_(cfg) {
        nx_ = cfg.nx; ny_ = cfg.ny; nz_ = cfg.nz;
        n_  = static_cast<size_t>(nx_) * ny_ * nz_;
        ds_ = cfg.cellSize;
        dt_ = cfg.courant * ds_ / (lightSpeed() * std::sqrt(3.0));

        Ex_.assign(n_, 0); Ey_.assign(n_, 0); Ez_.assign(n_, 0);
        Hx_.assign(n_, 0); Hy_.assign(n_, 0); Hz_.assign(n_, 0);
        epsR_.assign(n_, 1.0f);
        sigma_.assign(n_, 0.0f);
        pec_.assign(n_, 0);
        ca_.assign(n_, 1.0f);
        cb_.assign(n_, 0.0f);
        finalizeMaterials();
    }

    int nx() const { return nx_; }
    int ny() const { return ny_; }
    int nz() const { return nz_; }
    int absorbCells() const { return cfg_.absorbCells; }
    double dt() const { return dt_; }
    double ds() const { return ds_; }
    double time() const { return t_; }

    inline size_t idx(int i, int j, int k) const {
        return (static_cast<size_t>(i) * ny_ + j) * nz_ + k;
    }

    // ---- материалы (вызывать до finalizeMaterials) ----
    void setEpsR(int i, int j, int k, double v)  { epsR_[idx(i,j,k)] = static_cast<Real>(v); }
    void setSigma(int i, int j, int k, double v) { sigma_[idx(i,j,k)] = static_cast<Real>(v); }
    void setPec(int i, int j, int k, bool v)     { pec_[idx(i,j,k)] = v ? 1 : 0; }

    // Пересчитать коэффициенты обновления E из ε_r и σ + врезать поглощающий слой.
    // db (для H) — скаляр, μ однородна.
    void finalizeMaterials() {
        // Авто-подбор максимальной проводимости поглощающего слоя (полиномиальный
        // профиль 3-й степени): σ_max ≈ -(m+1)ln(R)/(2·η·N·ds), R — целевой коэф.
        // отражения, η = √(μ₀/ε₀). Тут эмпирически достаточно.
        double sigmaMax = cfg_.absorbSigmaMax;
        if (sigmaMax <= 0.0) {
            const double eta = std::sqrt(kMu0 / kEps0);
            const double R0 = 1e-4;
            const int    m  = 3;
            const int    N  = std::max(1, cfg_.absorbCells);
            sigmaMax = -(m + 1) * std::log(R0) / (2.0 * eta * N * ds_);
        }
        const int A = cfg_.absorbCells;
        for (int i = 0; i < nx_; ++i)
            for (int j = 0; j < ny_; ++j)
                for (int k = 0; k < nz_; ++k) {
                    // глубина в поглощающий слой (0 — снаружи)
                    int d = 0;
                    d = std::max(d, A - i);          d = std::max(d, A - (nx_ - 1 - i));
                    d = std::max(d, A - j);          d = std::max(d, A - (ny_ - 1 - j));
                    d = std::max(d, A - k);          d = std::max(d, A - (nz_ - 1 - k));
                    size_t id = idx(i,j,k);
                    double sig = sigma_[id];
                    if (d > 0 && A > 0) {
                        double x = static_cast<double>(d) / A; // 0..1
                        sig += sigmaMax * x * x * x;           // кубический профиль
                    }
                    double er = std::max(1e-6f, epsR_[id]);
                    double denom = 1.0 + sig * dt_ / (2.0 * kEps0 * er);
                    ca_[id] = static_cast<Real>((1.0 - sig * dt_ / (2.0 * kEps0 * er)) / denom);
                    cb_[id] = static_cast<Real>((dt_ / (kEps0 * er * ds_)) / denom);
                }
        dbH_ = static_cast<Real>(dt_ / (kMu0 * ds_));
    }

    // ---- источники ----
    // Мягкий источник: добавляет к компоненте поля (0=Ex,1=Ey,2=Ez) в ячейке.
    void addSoftSource(int i, int j, int k, int comp, double value) {
        size_t id = idx(i,j,k);
        Real v = static_cast<Real>(value);
        if (comp == 0) Ex_[id] += v;
        else if (comp == 1) Ey_[id] += v;
        else Ez_[id] += v;
    }

    // ---- один шаг по времени: H, затем E ----
    void step() { updateH(); updateE(); t_ += dt_; }

    // ---- доступ к полям / срезам ----
    Real ex(int i,int j,int k) const { return Ex_[idx(i,j,k)]; }
    Real ey(int i,int j,int k) const { return Ey_[idx(i,j,k)]; }
    Real ez(int i,int j,int k) const { return Ez_[idx(i,j,k)]; }
    double eMag(int i,int j,int k) const {
        size_t id = idx(i,j,k);
        return std::sqrt(double(Ex_[id])*Ex_[id] + double(Ey_[id])*Ey_[id] + double(Ez_[id])*Ez_[id]);
    }

    // Полная энергия поля (Дж): ∫(ε₀ε_r|E|²/2 + μ₀|H|²/2) dV.
    double totalEnergy() const {
        double w = 0.0;
        const double cell = ds_ * ds_ * ds_;
        for (size_t id = 0; id < n_; ++id) {
            double e2 = double(Ex_[id])*Ex_[id] + double(Ey_[id])*Ey_[id] + double(Ez_[id])*Ez_[id];
            double h2 = double(Hx_[id])*Hx_[id] + double(Hy_[id])*Hy_[id] + double(Hz_[id])*Hz_[id];
            w += 0.5 * kEps0 * epsR_[id] * e2 * cell + 0.5 * kMu0 * h2 * cell;
        }
        return w;
    }

private:
    void updateH() {
        // Hx нужен Ez[j+1], Ey[k+1];  Hy нужен Ex[k+1], Ez[i+1];  Hz нужен Ey[i+1], Ex[j+1].
        for (int i = 0; i < nx_; ++i)
            for (int j = 0; j < ny_; ++j)
                for (int k = 0; k < nz_; ++k) {
                    size_t id = idx(i,j,k);
                    if (j + 1 < ny_ && k + 1 < nz_) {
                        Real curl = (Ez_[idx(i,j+1,k)] - Ez_[id]) - (Ey_[idx(i,j,k+1)] - Ey_[id]);
                        Hx_[id] -= dbH_ * curl;
                    }
                    if (k + 1 < nz_ && i + 1 < nx_) {
                        Real curl = (Ex_[idx(i,j,k+1)] - Ex_[id]) - (Ez_[idx(i+1,j,k)] - Ez_[id]);
                        Hy_[id] -= dbH_ * curl;
                    }
                    if (i + 1 < nx_ && j + 1 < ny_) {
                        Real curl = (Ey_[idx(i+1,j,k)] - Ey_[id]) - (Ex_[idx(i,j+1,k)] - Ex_[id]);
                        Hz_[id] -= dbH_ * curl;
                    }
                }
    }

    void updateE() {
        // Ex нужен Hz[j]-Hz[j-1], Hy[k]-Hy[k-1]; и т.д. (обратные разности).
        for (int i = 0; i < nx_; ++i)
            for (int j = 0; j < ny_; ++j)
                for (int k = 0; k < nz_; ++k) {
                    size_t id = idx(i,j,k);
                    if (pec_[id]) { Ex_[id] = Ey_[id] = Ez_[id] = 0; continue; }
                    if (j - 1 >= 0 && k - 1 >= 0) {
                        Real curl = (Hz_[id] - Hz_[idx(i,j-1,k)]) - (Hy_[id] - Hy_[idx(i,j,k-1)]);
                        Ex_[id] = ca_[id] * Ex_[id] + cb_[id] * curl;
                    }
                    if (k - 1 >= 0 && i - 1 >= 0) {
                        Real curl = (Hx_[id] - Hx_[idx(i,j,k-1)]) - (Hz_[id] - Hz_[idx(i-1,j,k)]);
                        Ey_[id] = ca_[id] * Ey_[id] + cb_[id] * curl;
                    }
                    if (i - 1 >= 0 && j - 1 >= 0) {
                        Real curl = (Hy_[id] - Hy_[idx(i-1,j,k)]) - (Hx_[id] - Hx_[idx(i,j-1,k)]);
                        Ez_[id] = ca_[id] * Ez_[id] + cb_[id] * curl;
                    }
                }
    }

    FdtdConfig cfg_;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    size_t n_ = 0;
    double ds_ = 0, dt_ = 0, t_ = 0;
    Real dbH_ = 0;

    std::vector<Real> Ex_, Ey_, Ez_, Hx_, Hy_, Hz_;
    std::vector<Real> epsR_, sigma_, ca_, cb_;
    std::vector<uint8_t> pec_;
};

} // namespace current_lab::physics
