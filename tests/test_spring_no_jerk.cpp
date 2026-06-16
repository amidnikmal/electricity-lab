#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "circuit/Circuit.h"
#include "circuit/DemoCircuits.h"
#include "projection/MechanicsCapacitor.h"
#include "simulation/LiveSim.h"
#include "solver/CircuitSolver.h"

// Инвариант: пружина конденсатора в механической проекции НЕ дёргается.
//
// При плавной зарядке RC-цепи напряжение Vc монотонно растёт по экспоненте, а
// длина/прогиб пружины (SpringCapacitorModel) — гладкая монотонная функция Vc
// (theta = clamp(Vc/vRange)*thetaMax, длина ~ cos(theta) у обоих кривошипов).
// Значит покадровое изменение длины обязано быть:
//   * ограниченным (нет скачков/рывков между соседними кадрами);
//   * монотонным на зарядке (длина только убывает — пружина только сжимается);
//   * без знакопеременного дребезга (соседние Δ не меняют знак туда-сюда).
//
// Регрессия, которую это ловит: если бы theta мапилось через «сырой» chainTravel
// или угол кривошипа уходил за 90°, длина ~ cos(theta) то падала бы, то росла —
// пружина «гуляет» (ср. SpringCompressesMonotonicallyWithCharge в
// test_mechanics_capacitor.cpp, но там статические снимки; здесь — живая
// микродинамика LiveSim во времени).
namespace {

using current_lab::mechanics::SpringCapacitorModel;
using current_lab::mechanics::capacitorThetaFromVoltage;
using namespace current_lab::simulation;

// RC-демка: источник 5 В — R 1 кОм — C 1e-3 Ф (tau = 1 c).
Circuit makeRcCapacitor(int& capId) {
    Circuit c = current_lab::demos::buildDemo(current_lab::demos::DemoCircuit::RcCapacitor);
    capId = -1;
    for (const auto& comp : c.components)
        if (comp.type == ComponentType::Capacitor) capId = comp.id;
    return c;
}

// Напряжение на конденсаторе Vc = V(nodeA) - V(nodeB) из решения.
double capVoltage(const CircuitSolution& sol, int capId) {
    for (const auto& br : sol.branches)
        if (br.componentId == capId) return br.voltageDrop;
    return 0.0;
}

// Длина пружины при данном Vc. vRange = размах напряжения сцены (источник 5 В);
// именно так маппинг работает в проекции (capacitorThetaFromVoltage).
double springLengthAtVoltage(double vc, double vRange) {
    SpringCapacitorModel m;
    m.theta = capacitorThetaFromVoltage(vc, vRange, m.p.thetaMax);
    return m.springLength();
}

} // namespace

// Плавность + монотонность длины пружины на всей зарядке RC через LiveSim.
TEST(SpringNoJerk, SpringLengthChangesSmoothlyDuringRcCharge) {
    int capId = -1;
    Circuit c = makeRcCapacitor(capId);
    ASSERT_GE(capId, 0);

    CircuitSolver solver;
    LiveSim sim;
    CircuitSolution sol;

    const double vRange = 5.0; // ЭДС источника = размах сцены

    sim.onCircuitEvent(c, solver);
    // Ручная скорость: фиксированный, предсказуемый sim-dt вместо авто-замедления.
    // 0.1 sim-с/реал-с при solveHz=60 => dt = 1/600 sim-с ~ tau/600: мелкий шаг,
    // но 60 c симуляции набираются за разумное число кадров.
    sim.setManualSpeed(0.1);
    const double simDt = sim.dt();
    ASSERT_GT(simDt, 0.0);

    // Покадровый порог плавности. За один шаг dt напряжение меняется не больше
    // чем на dV = (Vmax/tau)*dt (максимум — в начале зарядки). Длина-функция от
    // theta липшицева: |dLen/dVc| ограничена. Возьмём щедрую верхнюю оценку
    // эмпирически из самого крутого первого шага и потребуем, чтобы НИ ОДИН шаг
    // её заметно не превысил. Жёсткий числовой порог берём с большим запасом от
    // максимально возможного скачка: armLen=44, thetaMax=60deg, dTheta_max за
    // шаг = (thetaMax/vRange)*dV. dV_max ~= (5/1)* (1/600) ~= 8.3e-3 В.
    // dLen ~ |d/dtheta springLength| * dTheta <= 2*armLen*sin(theta)*dTheta
    // <= 2*44*1*(1.047/5*8.3e-3) ~= 0.15 ед. Порог 1.0 — десятикратный запас,
    // но достаточно жёсткий, чтобы поймать скачок на пол-оборота кривошипа.
    const double kMaxStepDelta = 1.0;

    double prevLen = springLengthAtVoltage(capVoltage(sol, capId), vRange);
    double prevDelta = 0.0;
    int frames = 0, signFlips = 0, increases = 0, samples = 0;
    double firstLen = prevLen, lastLen = prevLen;
    double maxStepObserved = 0.0;

    // Гоняем кадрами по 1/60 реальной секунды ВЕСЬ переходный процесс — пока
    // LiveSim не уснёт ровно на DC-асимптоте (Vc->5 В). При tau=1 c и manual
    // speed 0.1 это ~6 сим-секунд = ~3600 кадров чистой RC-арифметики (Box2D не
    // участвует) — доли секунды реального времени. Хвост после засыпания
    // тривиально константен (delta=0), проверять там нечего.
    const int kMaxFrames = 200000;
    while (!sim.settled() && frames < kMaxFrames) {
        bool stepped = sim.advance(c, solver, 1.0 / 60.0, sol);
        ++frames;
        if (!stepped) continue; // решений не было (накопитель < dt) — кадр пуст

        double len = springLengthAtVoltage(capVoltage(sol, capId), vRange);
        double delta = len - prevLen;

        maxStepObserved = std::max(maxStepObserved, std::abs(delta));
        // (1) Плавность: покадровый скачок ограничен.
        EXPECT_LE(std::abs(delta), kMaxStepDelta)
            << "рывок длины пружины на кадре " << frames << " (Δ=" << delta
            << ", Vc=" << capVoltage(sol, capId) << ")";

        // (2) Монотонность на зарядке: длина только убывает (сжатие). Допускаем
        // микрошум солвера ниже эпсилон.
        const double kMonoEps = 1e-6;
        if (delta > kMonoEps) ++increases;

        // (3) Нет знакопеременного дребезга: считаем смены знака значимых Δ.
        if (std::abs(delta) > kMonoEps && std::abs(prevDelta) > kMonoEps &&
            delta * prevDelta < 0.0)
            ++signFlips;

        prevDelta = delta;
        prevLen = len;
        lastLen = len;
        ++samples;
    }

    ASSERT_GT(samples, 100) << "симуляция не набрала кадров";
    EXPECT_TRUE(sim.settled())
        << "RC так и не уснул на DC-асимптоте за " << frames << " кадров";

    // Длина не должна расти ни на одном шаге зарядки (пружина сжимается монотонно).
    EXPECT_EQ(increases, 0)
        << "длина пружины росла на " << increases
        << " кадрах при монотонной зарядке — пружина «гуляет»";

    // Никакого дребезга знака приращения.
    EXPECT_EQ(signFlips, 0)
        << "приращение длины меняло знак " << signFlips << " раз — дребезг";

    // Заряд реально прошёл: к концу пружина заметно короче (сжалась).
    EXPECT_LT(lastLen, firstLen - 1e-3)
        << "пружина не сжалась за зарядку (Vc не вырос?)";

    // Наблюдённый максимальный шаг должен лежать сильно ниже порога — иначе
    // порог подобран впритык, а не с запасом.
    EXPECT_LT(maxStepObserved, kMaxStepDelta * 0.5)
        << "максимальный реальный шаг " << maxStepObserved
        << " подозрительно близок к порогу " << kMaxStepDelta;
}

// Прямая проверка гладкости маппинга Vc -> длина (без солвера): монотонно
// растущему Vc от 0 до vRange отвечает монотонно убывающая длина, без рывков.
// Это «эталон» — фиксирует, что сама проекция гладкая; тест выше проверяет, что
// LiveSim подаёт в неё гладкий Vc.
TEST(SpringNoJerk, VoltageToSpringLengthIsMonotoneAndSmooth) {
    const double vRange = 5.0;
    const int N = 2000;
    double prevLen = springLengthAtVoltage(0.0, vRange);
    double maxStep = 0.0;
    int increases = 0;
    for (int i = 1; i <= N; ++i) {
        double vc = vRange * (double(i) / N);
        double len = springLengthAtVoltage(vc, vRange);
        double delta = len - prevLen;
        if (delta > 1e-9) ++increases;
        maxStep = std::max(maxStep, std::abs(delta));
        prevLen = len;
    }
    EXPECT_EQ(increases, 0) << "длина не монотонна по Vc — пружина «гуляет»";
    // Шаг по Vc = vRange/N = 2.5e-3 В; крупнее, чем в RC-тесте, но всё равно мал.
    EXPECT_LT(maxStep, 0.5) << "рывок в самом маппинге Vc->длина";
}
