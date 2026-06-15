#pragma once
//
// EmScene — демо-сцены для FdtdField (геометрия материалов + описание источника).
//
// Каждая сцена настраивает материалы поля (ε_r, PEC) и возвращает EmSource —
// описание возбуждения, которое драйвер подаёт на каждом шаге через injectEmSource().
// Все источники Ez-поляризованы и распространяются вдоль x; структура — по y.
//
// Поляризация/геометрия выбраны учебными: волну видно в срезе XY (Ez), щели и стенки
// задаются по y, проводники — маской PEC. Источник — мягкий (current sheet или точка):
// для непрерывных сцен синус с плавной раскачкой, для импульсных — окно Гаусса.
//
// См. docs/EM_FDTD_PLAN.md. Чистый заголовок, без UI/GL.

#include "physics/FdtdField.h"
#include <cmath>
#include <string>

namespace current_lab::physics {

enum class EmDemo {
    PlaneWave,            // плоская волна в вакууме (базовая)
    DipoleRadiator,       // точечный осциллятор — расходящиеся кольца
    DoubleSlit,           // плоская волна + PEC-экран с двумя щелями (интерференция)
    DielectricInterface,  // граница вакуум/диэлектрик (отражение + преломление)
    Mirror,               // PEC-зеркало справа (стоячая волна)
    Waveguide,            // канал между двумя PEC-пластинами (направляемые моды)
    Count
};

inline EmDemo emDemoFromName(const std::string& n) {
    if (n == "PlaneWave")            return EmDemo::PlaneWave;
    if (n == "DipoleRadiator")       return EmDemo::DipoleRadiator;
    if (n == "DoubleSlit")           return EmDemo::DoubleSlit;
    if (n == "DielectricInterface")  return EmDemo::DielectricInterface;
    if (n == "Mirror")               return EmDemo::Mirror;
    if (n == "Waveguide")            return EmDemo::Waveguide;
    return EmDemo::Count; // неизвестно
}

struct EmSource {
    enum Kind { Point, PlaneX } kind = Point;
    int i = 0, j = 0, k = 0;        // Point: ячейка; PlaneX: i — индекс плоскости
    int comp = 2;                   // компонента поля (2 = Ez)
    bool   continuous = true;       // синус с раскачкой; иначе импульс-окно
    double cellsPerWavelength = 20; // длина волны в ячейках (>~15 — мало дисперсии)
    double amp = 1.0;
    double n0 = 30.0, spread = 12.0;// окно импульсного источника
    double rampSteps = 40.0;        // раскачка непрерывного источника
};

// Настроить материалы сцены и вернуть описание источника.
// Вызывает sim.finalizeMaterials() внутри (после правки ε/PEC).
inline EmSource buildEmScene(FdtdField& sim, EmDemo demo) {
    const int nx = sim.nx(), ny = sim.ny(), nz = sim.nz();
    const int A = sim.absorbCells();
    const int margin = A + 3;
    const int kc = nz / 2, jc = ny / 2;

    EmSource src;
    src.comp = 2; // Ez

    switch (demo) {
    case EmDemo::PlaneWave: {
        src.kind = EmSource::PlaneX; src.i = margin;
        src.continuous = true; src.cellsPerWavelength = 20;
        break;
    }
    case EmDemo::DipoleRadiator: {
        src.kind = EmSource::Point; src.i = nx / 2; src.j = jc; src.k = kc;
        src.continuous = true; src.cellsPerWavelength = 16;
        break;
    }
    case EmDemo::DoubleSlit: {
        // PEC-экран в плоскости i = nx/2, кроме двух щелей по y (на всю толщину z).
        const int screenI = nx / 2;
        const int sep = ny / 6;          // полурасстояние между щелями
        const int slitHalf = 2;          // полуширина щели
        for (int j = 0; j < ny; ++j) {
            bool inSlitA = std::abs(j - (jc - sep)) <= slitHalf;
            bool inSlitB = std::abs(j - (jc + sep)) <= slitHalf;
            if (inSlitA || inSlitB) continue;
            for (int k = 0; k < nz; ++k) sim.setPec(screenI, j, k, true);
        }
        src.kind = EmSource::PlaneX; src.i = margin;
        src.continuous = true; src.cellsPerWavelength = 14;
        break;
    }
    case EmDemo::DielectricInterface: {
        // Правая половина — диэлектрик ε_r = 4 (v = c/2, λ вдвое короче).
        for (int i = nx / 2; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                for (int k = 0; k < nz; ++k) sim.setEpsR(i, j, k, 4.0);
        src.kind = EmSource::PlaneX; src.i = margin;
        src.continuous = true; src.cellsPerWavelength = 20;
        break;
    }
    case EmDemo::Mirror: {
        // PEC-зеркало у правой границы (перед поглотителем) → стоячая волна.
        const int mirrorI = nx - margin;
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k) sim.setPec(mirrorI, j, k, true);
        src.kind = EmSource::PlaneX; src.i = margin;
        src.continuous = true; src.cellsPerWavelength = 20;
        break;
    }
    case EmDemo::Waveguide: {
        // Две PEC-пластины (стенки канала) параллельно оси x, на всю длину.
        const int jlo = jc - ny / 6, jhi = jc + ny / 6;
        for (int i = 0; i < nx; ++i)
            for (int k = 0; k < nz; ++k) { sim.setPec(i, jlo, k, true); sim.setPec(i, jhi, k, true); }
        // Точечный источник внутри канала у входа возбуждает направляемые моды.
        src.kind = EmSource::Point; src.i = margin; src.j = jc; src.k = kc;
        src.continuous = true; src.cellsPerWavelength = 12;
        break;
    }
    default: break;
    }

    sim.finalizeMaterials();
    return src;
}

// Значение источника на шаге n.
inline double emWaveform(const FdtdField& sim, const EmSource& s, int n) {
    constexpr double kPi = 3.14159265358979323846;
    const double cellsPerStep = FdtdField::lightSpeed() * sim.dt() / sim.ds();
    const double f0 = cellsPerStep / s.cellsPerWavelength; // циклов на шаг
    double env;
    if (s.continuous) {
        env = 1.0 - std::exp(-static_cast<double>(n) / s.rampSteps);
    } else {
        double x = (n - s.n0) / s.spread;
        env = std::exp(-x * x);
    }
    return s.amp * env * std::sin(2.0 * kPi * f0 * n);
}

// Подать источник в поле на шаге n (вызывать перед sim.step()).
inline void injectEmSource(FdtdField& sim, const EmSource& s, int n) {
    const double val = emWaveform(sim, s, n);
    if (s.kind == EmSource::Point) {
        sim.addSoftSource(s.i, s.j, s.k, s.comp, val);
    } else { // PlaneX — токовый лист в плоскости x = const
        for (int j = 0; j < sim.ny(); ++j)
            for (int k = 0; k < sim.nz(); ++k)
                sim.addSoftSource(s.i, j, k, s.comp, val);
    }
}

} // namespace current_lab::physics
