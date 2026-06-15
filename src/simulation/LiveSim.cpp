#include "simulation/LiveSim.h"

#include <algorithm>
#include <cmath>

namespace current_lab::simulation {

namespace {

double branchCurrentFor(const CircuitSolution& solution, int componentId) {
    for (const auto& br : solution.branches)
        if (br.componentId == componentId) return br.current;
    return 0.0;
}

double potentialFor(const CircuitSolution& solution, int nodeId) {
    for (const auto& np : solution.nodePotentials)
        if (np.nodeId == nodeId) return np.potential;
    return 0.0;
}

} // namespace

double theveninResistanceSeenBy(const Circuit& circuit, int componentId,
                                CircuitSolver& solver) {
    int idx = circuit.componentIndex(componentId);
    if (idx < 0) return -1.0;

    // Элемент -> тестовый источник; два решения с 0 В и 1 В. Разность токов
    // гасит вклад остальных источников: R_th = dV / dI. Работает и в чисто
    // пассивной разрядной сети, где V_oc/I_sc делит ноль на ноль.
    Circuit probe = circuit;
    probe.components[idx].type = ComponentType::VoltageSource;

    probe.components[idx].value = 0.0;
    double i0 = branchCurrentFor(solver.solve(probe), componentId);
    probe.components[idx].value = 1.0;
    double i1 = branchCurrentFor(solver.solve(probe), componentId);

    double di = std::abs(i1 - i0);
    if (di < 1e-12 || !std::isfinite(di)) return -1.0; // пути нет (ключ разомкнут)
    return 1.0 / di;
}

double smallestTimeConstant(const Circuit& circuit, CircuitSolver& solver,
                            const LiveSimConfig& cfg) {
    double best = -1.0;
    for (const auto& comp : circuit.components) {
        bool isCap = comp.type == ComponentType::Capacitor;
        bool isInd = comp.type == ComponentType::Inductor;
        if (!isCap && !isInd) continue;
        if (comp.value <= 0.0) continue;

        double rth = theveninResistanceSeenBy(circuit, comp.id, solver);
        if (rth <= 0.0) continue; // изолированный элемент: процесса не будет

        double tau = isCap ? rth * comp.value : comp.value / rth;
        // Паразитно быстрые tau (наносекундные RC) НЕ участвуют: клампить их
        // вверх значило бы прибить скорость к minSpeed и хоронить медленную
        // историю той же цепи. Сверху — клампим (история длиннее maxTau всё
        // равно играется куском).
        if (tau < cfg.minTau) continue;
        tau = std::min(tau, cfg.maxTau);
        if (best <= 0.0 || tau < best) best = tau;
    }
    return best;
}

double oscillationTimescale(const Circuit& circuit, CircuitSolver& solver,
                            const LiveSimConfig& cfg) {
    constexpr double kTwoPi = 6.283185307179586;
    // Период звона = 2*pi*sqrt(L*C) считаем по ЗНАЧЕНИЯМ элементов, а НЕ по
    // R_th-постоянным. Причина: в ПОСЛЕДОВАТЕЛЬНОМ RLC катушка стоит в одной
    // ветви с конденсатором, и тевенин-проба катушки не находит DC-пути (C его
    // разрывает) -> tau_L был бы не определён. Требуем: есть катушка и есть
    // ПОДКЛЮЧЁННЫЙ конденсатор (R_th>0; изолированный за разомкнутым ключом не
    // резонирует). Берём наименьшие L и C -> самый быстрый (требовательный к
    // видимости) звон.
    double cVal = -1.0, lVal = -1.0;
    bool capConnected = false;
    for (const auto& comp : circuit.components) {
        if (comp.value <= 0.0) continue;
        if (comp.type == ComponentType::Capacitor) {
            if (cVal < 0.0 || comp.value < cVal) cVal = comp.value;
            if (theveninResistanceSeenBy(circuit, comp.id, solver) > 0.0) capConnected = true;
        } else if (comp.type == ComponentType::Inductor) {
            if (lVal < 0.0 || comp.value < lVal) lVal = comp.value;
        }
    }
    if (cVal <= 0.0 || lVal <= 0.0 || !capConnected) return -1.0; // не колебательный
    double period = kTwoPi * std::sqrt(lVal * cVal);
    return std::clamp(period, cfg.minTau, cfg.maxTau);
}

void LiveSim::onCircuitEvent(const Circuit& circuit, CircuitSolver& solver) {
    double tau = smallestTimeConstant(circuit, solver, m_cfg);
    // Колебательный контур (есть и L, и C): интересен не самый быстрый R*C, а
    // ПЕРИОД звона ~ 2*pi*sqrt(L*C). Берём его как масштаб истории (он >= самого
    // быстрого tau для недодемпфированного контура), иначе крошечный R*C загоняет
    // скорость в ~1% и звон ползёт незаметно. Для RC/RL (нет пары L+C) osc<=0 и
    // поведение прежнее — по наименьшей tau.
    double osc = oscillationTimescale(circuit, solver, m_cfg);
    double storyTau = std::max(tau, osc);
    // ~3 масштаба истории растягиваются на storySeconds реального времени; без
    // реактивных элементов скорость не важна (уснём после первого же шага).
    m_autoSpeedValue = storyTau > 0.0
        ? std::clamp(3.0 * storyTau / m_cfg.storySeconds, m_cfg.minSpeed, m_cfg.maxSpeed)
        : m_cfg.maxSpeed;
    applySpeed();
    m_state.invalidateHistory(); // trap-история несовместима с новой топологией
    wake();
}

void LiveSim::discharge() {
    m_state.reset();
    wake();
}

void LiveSim::wake() {
    m_settled = false;
    m_quietSteps = 0;
    // НЕ обнулять аккумулятор: события каждый кадр (ручка-динамо, drag) на
    // мониторе >60 Гц иначе сбрасывают неполный вклад кадра и время цепи
    // замерзает совсем (ревью 2026-06-12, подтверждено численно). Кламп
    // ограничивает всплеск после смены скорости (dt уже пересчитан).
    m_accumulator = std::clamp(m_accumulator, 0.0, m_dt);
}

void LiveSim::applySpeed() {
    m_speed = m_manualSpeed > 0.0
        ? std::clamp(m_manualSpeed, 1e-6, 10.0)
        : m_autoSpeedValue;
    // ~solveHz решений на реальную секунду: dt = скорость / частота.
    m_dt = std::clamp(m_speed / std::max(1.0, m_cfg.solveHz), 1e-9, 1.0);
}

bool LiveSim::advance(const Circuit& circuit, CircuitSolver& solver, double realDt,
                      CircuitSolution& solution) {
    if (m_settled || realDt <= 0.0) return false;

    m_accumulator += realDt * m_speed;
    int steps = static_cast<int>(m_accumulator / m_dt);
    if (steps <= 0) return false;
    if (steps > m_cfg.maxStepsPerFrame) {
        steps = m_cfg.maxStepsPerFrame; // подвисший кадр: симуляция отстаёт, не догоняет
        m_accumulator = 0.0;
    } else {
        m_accumulator -= steps * m_dt;
    }

    bool updated = false;
    for (int i = 0; i < steps && !m_settled; ++i) {
        integrateStep(circuit, solver, solution);
        updated = true;
    }
    return updated;
}

void LiveSim::stepOnce(const Circuit& circuit, CircuitSolver& solver,
                       CircuitSolution& solution) {
    if (m_settled) wake();
    integrateStep(circuit, solver, solution);
}

CircuitSolution LiveSim::currentSolution(const Circuit& circuit, CircuitSolver& solver) {
    // Всегда снапшот текущего состояния: во сне состояние уже снапнуто на
    // асимптоту там, где DC-путь существует, а у изолированных элементов
    // (разомкнутый ключ) честно сохраняет заряд. Голый solve() здесь рисовал
    // бы фиктивный делитель из gmin-утечек (ревью 2026-06-12).
    return solver.solveTransientSnapshot(circuit, m_state);
}

void LiveSim::integrateStep(const Circuit& circuit, CircuitSolver& solver,
                            CircuitSolution& solution) {
    TransientState before = m_state;
    solution = solver.stepTransient(circuit, m_state, m_dt, m_cfg.method);

    // Цепь без реактивных элементов стационарна по построению (state пуст).
    // AC-источник никогда не приходит к стационару: цепь под переменным
    // напряжением колеблется вечно. Её НЕЛЬЗЯ усыплять — иначе анимация
    // замирает (демка AcRectifier «не работала» именно из-за этого: выход
    // выпрямителя выходил на стабильную пульсацию и сим засыпал).
    bool hasAcSource = false;
    for (const auto& comp : circuit.components)
        if (comp.type == ComponentType::AcVoltageSource) { hasAcSource = true; break; }
    bool reactive = !m_state.capVoltage.empty() || !m_state.indCurrent.empty();
    if (!hasAcSource && (!reactive || stateChangeRel(before) < 1.0)) {
        if (++m_quietSteps >= m_cfg.settleQuietSteps || !reactive) {
            m_settled = true;
            snapToAsymptote(circuit, solver, solution);
        }
    } else {
        m_quietSteps = 0;
    }
}

// > 1.0 — состояние ещё движется заметнее порогов затухания; < 1.0 — тихо.
// Пороги — СКОРОСТИ в сим-времени (|dV|/dt), а не абсолютные шаги: шаговый
// порог при глубоком ручном замедлении (крошечный dt) ловил «тишину» прямо
// посреди процесса и телепортировал заряд к асимптоте (ревью 2026-06-12).
// Скоростной критерий эквивалентен «остаток < rate*tau» при любой скорости.
// Не относительные: у экспоненциального разряда к нулю dV/V постоянно.
double LiveSim::stateChangeRel(const TransientState& before) const {
    double worst = 0.0;
    auto scan = [&](const std::unordered_map<int, double>& now,
                    const std::unordered_map<int, double>& prev, double rate) {
        double eps = std::max(rate * m_dt, 1e-300);
        for (const auto& [id, value] : now) {
            auto it = prev.find(id);
            double d = std::abs(value - (it != prev.end() ? it->second : 0.0));
            worst = std::max(worst, d / eps);
        }
    };
    scan(m_state.capVoltage, before.capVoltage, m_cfg.settleRateVc);
    scan(m_state.indCurrent, before.indCurrent, m_cfg.settleRateIl);
    return worst;
}

void LiveSim::snapToAsymptote(const Circuit& circuit, CircuitSolver& solver,
                              CircuitSolution& solution) {
    // Точная асимптота от DC-оракула — но ТОЛЬКО для элементов с реальным
    // DC-путём. Для изолированного элемента (разомкнутый ключ, конденсаторы
    // последовательно) solve() выдаёт фиктивный делитель из gmin-утечек
    // 1e-12 См, а честный предел — сохранение заряда: тот элемент оставляем
    // на его транзиентном состоянии (ревью 2026-06-12: демка SwitchedRc
    // показывала V/2 на конденсаторе за никогда не замыкавшимся ключом).
    CircuitSolution dc = solver.solve(circuit);
    for (const auto& comp : circuit.components) {
        bool isCap = comp.type == ComponentType::Capacitor;
        bool isInd = comp.type == ComponentType::Inductor;
        if (!isCap && !isInd) continue;
        if (theveninResistanceSeenBy(circuit, comp.id, solver) <= 0.0) continue;
        if (isCap) {
            double va = potentialFor(dc, comp.nodeA);
            double vb = potentialFor(dc, comp.nodeB);
            double dV = va - vb;
            m_state.capCharge[comp.id] = comp.value * dV;
            m_state.capVoltage[comp.id] = dV;
            m_state.capCurrent[comp.id] = 0.0; // в стационаре d/dt = 0
        } else {
            m_state.indCurrent[comp.id] = branchCurrentFor(dc, comp.id);
            m_state.indVoltage[comp.id] = 0.0;
        }
    }
    // Рисуем снапшот состояния, не голый DC: совпадает с DC там, где путь
    // есть, и физичен там, где его нет.
    solution = solver.solveTransientSnapshot(circuit, m_state);
}

} // namespace current_lab::simulation
