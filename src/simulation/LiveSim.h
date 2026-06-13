#pragma once

#include "circuit/Circuit.h"
#include "solver/CircuitSolver.h"

// Единый живой режим симуляции: DC steady и Transient слиты. Цепь всегда живёт
// во времени; «стационар» — не отдельный режим, а предел процесса.
//
//   событие (правка / щелчок выключателя / ручка) -> просыпаемся,
//     авто-замедление времени от наименьшей tau цепи (стори ~ storySeconds);
//   ~solveHz решений в секунду, пока процесс не затухнет;
//   затухло -> солвер СПИТ: решение подменяется ТОЧНОЙ DC-асимптотой
//     (внутренний оракул), реактивное состояние снапится на неё, ноль решений
//     в кадр до следующего события. Визуальные миры (электроны/вода/цепи)
//     при этом продолжают крутиться — спит только MNA.
//
// Заряд конденсаторов ПЕРЕЖИВАЕТ правки и щелчки (это физика, а не сброс);
// обнуление — только явным discharge().
// Чистая логика без ImGui, тестируется в tests/test_live_sim.cpp.
namespace current_lab::simulation {

struct LiveSimConfig {
    double solveHz = 60.0;        // целевая частота решений MNA, 1/с реального времени
    double storySeconds = 2.5;    // за сколько реальных секунд проигрываются ~3*tau
    double minSpeed = 1e-5;       // кламп sim-секунд на реальную секунду
    double maxSpeed = 1.0;        // быстрее реального времени не идём
    double minTau = 1e-6;         // s; tau вне диапазона не участвует в авто-скорости
    double maxTau = 30.0;
    // Пороги затухания — скорости в СИМ-времени (тихо, когда |dV|/dt ниже):
    // эквивалентно «остаток до асимптоты < rate*tau» при любом замедлении.
    // Шаговый или относительный критерий не годятся: первый ложно засыпает
    // при глубоком slow-mo (крошечный dt), второй слеп к разряду в ноль.
    double settleRateVc = 0.05;   // V / сим-с: остаток ~ 1% от 5 В при tau=1 с
    double settleRateIl = 5e-5;   // A / сим-с
    int settleQuietSteps = 5;     // столько тихих шагов подряд = затухло
    int maxStepsPerFrame = 8;     // кап на подвисший кадр (обычно 1 шаг/кадр)
    IntegrationMethod method = IntegrationMethod::BackwardEuler;
};

// R_th, которое элемент «видит» со своих выводов: элемент заменяется тестовым
// источником 0 В и 1 В, два DC-решения, R_th = 1 / |I(1V) - I(0V)|. Разность
// гасит вклад остальных источников, поэтому проба работает и для ПАССИВНОЙ
// разрядной сети (V_oc = 0), где наивный V_oc/I_sc слеп. Возвращает <= 0,
// если пути нет (разомкнутый ключ: |dI| ~ 0).
double theveninResistanceSeenBy(const Circuit& circuit, int componentId,
                                CircuitSolver& solver);

// Наименьшая постоянная времени цепи (tau = R_th*C | L/R_th) в пределах
// [minTau, maxTau]; <= 0, если реактивных элементов с конечной tau нет.
double smallestTimeConstant(const Circuit& circuit, CircuitSolver& solver,
                            const LiveSimConfig& cfg);

class LiveSim {
public:
    explicit LiveSim(LiveSimConfig cfg = {}) : m_cfg(cfg) {}

    // Цепь изменилась (правка, щелчок выключателя, демка): будит солвер,
    // пересчитывает авто-скорость. Состояние C/L сохраняется.
    void onCircuitEvent(const Circuit& circuit, CircuitSolver& solver);

    // Лёгкое событие: меняется только ЗНАЧЕНИЕ источника (ручка-динамо, до
    // 60 раз/с) — tau цепи не менялась, тевенин-пробы не нужны, просто будим.
    void wakeKeepSpeed() { wake(); }

    // Явный разряд: Vc = 0, Il = 0, t = 0. Будит.
    void discharge();

    // Продвинуть на realDt реальных секунд. true => solution обновлено
    // (хотя бы один шаг или снап на асимптоту в момент засыпания).
    bool advance(const Circuit& circuit, CircuitSolver& solver, double realDt,
                 CircuitSolution& solution);

    // Один ручной шаг dt (кнопка Step). Будит, если спал.
    void stepOnce(const Circuit& circuit, CircuitSolver& solver,
                  CircuitSolution& solution);

    // Срез текущего состояния без продвижения времени (для рендера сразу
    // после правки): спим — асимптота, не спим — transient-снапшот.
    CircuitSolution currentSolution(const Circuit& circuit, CircuitSolver& solver);

    bool settled() const { return m_settled; }
    double time() const { return m_state.time; }
    double dt() const { return m_dt; }

    // sim-секунд на реальную секунду; авто от tau или ручной override.
    double simSpeed() const { return m_speed; }
    bool autoSpeed() const { return m_manualSpeed <= 0.0; }
    void setManualSpeed(double simPerReal) { m_manualSpeed = simPerReal; applySpeed(); }
    void setAutoSpeed() { m_manualSpeed = -1.0; applySpeed(); }

    IntegrationMethod method() const { return m_cfg.method; }
    void setMethod(IntegrationMethod m) { m_cfg.method = m; }

    const TransientState& state() const { return m_state; }
    TransientState& state() { return m_state; }

private:
    void wake();
    void applySpeed();
    // Один шаг интегратора + детектор затухания; в момент засыпания снапит
    // solution и состояние на DC-асимптоту.
    void integrateStep(const Circuit& circuit, CircuitSolver& solver,
                       CircuitSolution& solution);
    double stateChangeRel(const TransientState& before) const;
    void snapToAsymptote(const Circuit& circuit, CircuitSolver& solver,
                         CircuitSolution& solution);

    LiveSimConfig m_cfg;
    TransientState m_state;
    double m_autoSpeedValue = 1.0; // последняя авто-оценка (для UI)
    double m_manualSpeed = -1.0;   // > 0 => ручной override
    double m_speed = 1.0;          // действующая скорость
    double m_dt = 1.0 / 60.0;      // sim-шаг = speed / solveHz
    double m_accumulator = 0.0;    // sim-секунды, ещё не отшаганные
    bool m_settled = false;
    int m_quietSteps = 0;
};

} // namespace current_lab::simulation
