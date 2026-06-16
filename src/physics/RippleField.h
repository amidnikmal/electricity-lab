#pragma once
//
// RippleField — 2D-симулятор поверхностной волны («ripple tank», рябь на воде).
//
// Это наглядная АНАЛОГИЯ электромагнитной волны для правой панели: точечный
// осциллятор рождает расходящиеся круги, барьеры с двумя щелями дают
// интерференцию, а область с пониженной скоростью — преломление (короче длина
// волны). К цепному MNA-солверу отношения не имеет; чистый класс без UI/GL.
//
// Физика — скалярное волновое уравнение
//
//     ∂²u/∂t² = c² ∇²u
//
// Явная схема «чехарда» (leapfrog) по трём слоям времени:
//
//     u^{n+1}_{ij} = 2u^n_{ij} − u^{n-1}_{ij} + (c_{ij}·dt/ds)² · ∇²u^n_{ij}
//
// где лапласиан — 5-точечный шаблон. Шаг dt фиксирован условием Куранта для 2D:
// c·dt/ds ≤ 1/√2; берём запас (множитель 0.5), так что C = c·dt/ds = 0.5 и
// устойчивость гарантирована при любом локальном множителе скорости ≤ 1.
//
// Локальная скорость c_{ij} = cBase · speedScale_{ij} (speedScale ≤ 1 ⇒ медленнее,
// короче длина волны ⇒ преломление). Коэффициент схемы хранится как поячеечный
// c2_{ij} = (C·speedScale)² ≤ C² = 0.25.
//
// Поглощающая рамка: на самых внешних кольцах сетки применяем условие Мура
// 1-го порядка (u^{n+1}_border = u^n_inner + ((C−1)/(C+1))·(u^{n+1}_inner − u^n_border)),
// чтобы расходящиеся круги не отражались от краёв сетки.
//
// Барьеры: ячейки с жёстко удерживаемым u = 0 (отражающая/гасящая стенка).
//
// Без аллокаций в advance(); все буферы выделены в конструкторе.

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace current_lab::physics {

class RippleField {
public:
    using Real = float;

    explicit RippleField(int grid)
        : grid_(std::max(1, grid)),
          n_(static_cast<size_t>(std::max(1, grid)) * std::max(1, grid)) {
        u_.assign(n_, 0.0f);
        uPrev_.assign(n_, 0.0f);
        uNext_.assign(n_, 0.0f);
        barrier_.assign(n_, 0);
        // c2 = (C·speedScale)²; по умолчанию speedScale = 1 ⇒ c2 = C².
        c2_.assign(n_, static_cast<Real>(kCourant * kCourant));
        // Коэффициент Мура для базовой скорости (C = kCourant): (C−1)/(C+1).
        murCoef_ = static_cast<Real>((kCourant - 1.0) / (kCourant + 1.0));
    }

    // ---- сброс состояния ----
    void reset() {
        std::fill(u_.begin(), u_.end(), 0.0f);
        std::fill(uPrev_.begin(), uPrev_.end(), 0.0f);
        std::fill(uNext_.begin(), uNext_.end(), 0.0f);
        t_ = 0.0;
    }

    void clearBarriers() {
        std::fill(barrier_.begin(), barrier_.end(), uint8_t{0});
    }

    void setBarrier(int i, int j, bool on) {
        if (!inBounds(i, j)) return;
        barrier_[idx(i, j)] = on ? 1 : 0;
        if (on) {
            // стена держит нулевое смещение
            size_t id = idx(i, j);
            u_[id] = uPrev_[id] = uNext_[id] = 0.0f;
        }
    }

    // Локальный множитель скорости (1 = норма, <1 — медленнее ⇒ преломление).
    // Ограничиваем 0..1, чтобы не нарушить условие Куранта.
    void setSpeedScale(int i, int j, float s) {
        if (!inBounds(i, j)) return;
        float cs = std::clamp(s, 0.0f, 1.0f);
        float C = static_cast<float>(kCourant);
        c2_[idx(i, j)] = (C * cs) * (C * cs);
    }

    // ---- источники ----
    // Постоянный гармонический осциллятор в ячейке. amp — амплитуда вброса
    // (порядка единицы); частота в «герцах» относительно внутреннего времени.
    void addDrivenSource(int i, int j, double freqHz, double amp) {
        if (!inBounds(i, j)) return;
        sources_.push_back(Source{i, j, freqHz, amp});
    }

    void clearSources() { sources_.clear(); }

    // ---- интегрирование ----
    // Шагнуть поле substeps подшагов; на каждом подшаге впрыскиваются источники.
    void advance(int substeps) {
        for (int s = 0; s < substeps; ++s) step();
    }

    // ---- доступ ----
    float height(int i, int j) const {
        if (!inBounds(i, j)) return 0.0f;
        return u_[idx(i, j)];
    }

    float maxAbsHeight() const {
        float m = 0.0f;
        for (float v : u_) m = std::max(m, std::fabs(v));
        return m;
    }

    int grid() const { return grid_; }
    double time() const { return t_; }

private:
    struct Source {
        int i, j;
        double freqHz;
        double amp;
    };

    // π без зависимости от _USE_MATH_DEFINES (MinGW не даёт M_PI без него).
    static constexpr double kPi = 3.14159265358979323846;
    // Запас по устойчивости: C = c·dt/ds = 0.5 ≤ 1/√2 ≈ 0.707.
    static constexpr double kCourant = 0.5;
    // Внутренний шаг времени (единичные ds, c=1 ⇒ dt = C). Условный масштаб.
    static constexpr double kDt = kCourant;

    inline size_t idx(int i, int j) const {
        return static_cast<size_t>(i) * grid_ + j;
    }
    inline bool inBounds(int i, int j) const {
        return i >= 0 && i < grid_ && j >= 0 && j < grid_;
    }

    void step() {
        const int N = grid_;
        // 1) Обновление внутренних узлов по схеме чехарды.
        //    Граничное кольцо (i или j == 0 или N-1) обрабатываем отдельно (Мур).
        for (int i = 1; i < N - 1; ++i) {
            for (int j = 1; j < N - 1; ++j) {
                size_t id = idx(i, j);
                if (barrier_[id]) { uNext_[id] = 0.0f; continue; }
                float lap = u_[idx(i + 1, j)] + u_[idx(i - 1, j)]
                          + u_[idx(i, j + 1)] + u_[idx(i, j - 1)]
                          - 4.0f * u_[id];
                uNext_[id] = 2.0f * u_[id] - uPrev_[id] + c2_[id] * lap;
            }
        }

        // 2) Поглощающие границы (Mur 1-го порядка) по четырём рёбрам.
        //    u^{n+1}_b = u^n_in + k (u^{n+1}_in − u^n_b),  k = (C−1)/(C+1).
        const float k = murCoef_;
        if (N >= 2) {
            for (int j = 0; j < N; ++j) {
                murEdge(idx(0, j),       idx(1, j),       k);
                murEdge(idx(N - 1, j),   idx(N - 2, j),   k);
            }
            for (int i = 0; i < N; ++i) {
                murEdge(idx(i, 0),       idx(i, 1),       k);
                murEdge(idx(i, N - 1),   idx(i, N - 2),   k);
            }
        }

        // 3) Источники: добавляем в новый слой (вброс на каждом подшаге).
        for (const Source& src : sources_) {
            size_t id = idx(src.i, src.j);
            if (barrier_[id]) continue;
            double tNext = t_ + kDt;
            uNext_[id] += static_cast<float>(
                src.amp * std::sin(2.0 * kPi * src.freqHz * tNext));
        }

        // 4) Ротация слоёв: prev <- u, u <- next.
        uPrev_.swap(u_);
        u_.swap(uNext_);
        t_ += kDt;
    }

    // Мур 1-го порядка для граничной ячейки b, опираясь на внутреннюю in.
    // uNext_ для in уже посчитан; uNext_ для b перезаписываем.
    inline void murEdge(size_t b, size_t in, float k) {
        if (barrier_[b]) { uNext_[b] = 0.0f; return; }
        uNext_[b] = u_[in] + k * (uNext_[in] - u_[b]);
    }

    int grid_;
    size_t n_;
    double t_ = 0.0;
    Real murCoef_ = 0.0f;

    std::vector<Real> u_, uPrev_, uNext_, c2_;
    std::vector<uint8_t> barrier_;
    std::vector<Source> sources_;
};

} // namespace current_lab::physics
