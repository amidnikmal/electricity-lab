// Инвариант стрелок тока: на одном проводящем участке стрелки НЕ показывают
// одновременно в разные стороны, и в последовательной ветви ток один — все
// стрелки сонаправлены.
//
// Как ProjectionBuilder рисует стрелки (см. emitCurrentArrows): для КАЖДОГО
// компонента/провода направление стрелок = знак тока ветви, наложенный на ось
// этого компонента (nodeA->nodeB). Ток берётся из solution.branches[k].current
// (положительный = условно от nodeA к nodeB). То есть экранный вектор потока,
// вносимый ветвью k в узел N, направлен В узел N, когда ток втекает в N.
//
// Отсюда «стрелки не сталкиваются» эквивалентно закону Кирхгофа по току (KCL)
// в каждом узле: сумма (ток, ВЫтекающий из узла) == 0. Если бы на участке две
// стрелки смотрели навстречу (обе втекают в общий узел степени 2, или обе
// вытекают), KCL в этом узле нарушился бы. Для последовательной цепочки KCL
// автоматически даёт один и тот же ток во всех ветвях ветки → стрелки
// сонаправлены. Поэтому проверяем KCL по знаковым токам ветвей — ровно та
// величина, которой ProjectionBuilder ориентирует стрелки.
//
// Решение цепи: используем стационарное DC-решение CircuitSolver::solve — это
// то, что показывают стрелки в покое (LiveSim в пределе сходится к нему). Диоды
// решаются итеративно внутри solve; разомкнутый ключ даёт ток ≈ 0 (тривиально
// согласованно); AC-источник в DC-решателе берётся как мгновенное значение.

#include <gtest/gtest.h>

#include "circuit/Circuit.h"
#include "circuit/DemoCircuits.h"
#include "solver/CircuitSolver.h"

#include <cmath>
#include <unordered_map>
#include <vector>

namespace {

using namespace current_lab;

const BranchResult* branchFor(const CircuitSolution& sol, int componentId) {
    for (const auto& br : sol.branches)
        if (br.componentId == componentId) return &br;
    return nullptr;
}

} // namespace

// KCL по знаковым токам ветвей: в каждом узле сумма вытекающих токов ≈ 0.
// Это прямое выражение «стрелки тока не направлены навстречу на участке».
TEST(DemoCurrentArrows, KirchhoffCurrentLawHoldsAtEveryNode) {
    using demos::DemoCircuit;

    for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
        auto demo = static_cast<DemoCircuit>(d);
        Circuit circuit = demos::buildDemo(demo);
        const char* name = demos::demoName(demo);

        CircuitSolver solver;
        CircuitSolution sol = solver.solve(circuit);

        // Накапливаем знаковый ВЫтекающий ток в каждый узел и опорный масштаб
        // тока для относительного допуска.
        std::unordered_map<int, double> outflow; // nodeId -> сумма вытекающих токов
        double maxAbsI = 0.0;
        for (const auto& comp : circuit.components) {
            if (comp.nodeA == comp.nodeB) continue; // Ground — псевдоэлемент-петля
            const BranchResult* br = branchFor(sol, comp.id);
            if (!br) continue;
            double I = br->current; // условно nodeA -> nodeB
            maxAbsI = std::max(maxAbsI, std::abs(I));
            outflow[comp.nodeA] += I;  // из A ток вытекает (+)
            outflow[comp.nodeB] -= I;  // в B ток втекает (-)
        }

        // Узел земли — точка отвода опорного потенциала: в полном решении KCL
        // там тоже замыкается, но на всякий случай не наказываем именно его,
        // если бы решатель туда сливал невязку. Узлы степени 2 (последовательная
        // ветвь) — главная мишень: там обе стрелки обязаны быть согласованы.
        double tol = std::max(1e-6, maxAbsI * 1e-3);

        for (const auto& [nodeId, sum] : outflow) {
            if (nodeId == circuit.groundNodeId) continue;
            EXPECT_NEAR(sum, 0.0, tol)
                << name << ": нарушен KCL в узле " << nodeId
                << " (суммарный вытекающий ток " << sum
                << " A, масштаб " << maxAbsI << " A) — стрелки тока на сходящихся "
                << "участках направлены несогласованно";
        }
    }
}

// Прямая формулировка для последовательных участков: узел, в котором сходятся
// РОВНО два проводящих элемента (степень 2), обязан иметь один ток втекающим,
// другой вытекающим — стрелки сонаправлены вдоль ветви, не навстречу.
TEST(DemoCurrentArrows, SeriesNodesHaveConsistentArrowDirection) {
    using demos::DemoCircuit;

    for (int d = 0; d < static_cast<int>(DemoCircuit::Count); ++d) {
        auto demo = static_cast<DemoCircuit>(d);
        Circuit circuit = demos::buildDemo(demo);
        const char* name = demos::demoName(demo);

        CircuitSolver solver;
        CircuitSolution sol = solver.solve(circuit);

        // Для каждого узла соберём знаковые токи инцидентных ветвей,
        // ориентированные «в узел» (положительный = стрелка указывает В узел).
        struct Incident { int compId; double towardNode; };
        std::unordered_map<int, std::vector<Incident>> incident;
        double maxAbsI = 0.0;

        for (const auto& comp : circuit.components) {
            if (comp.nodeA == comp.nodeB) continue;
            const BranchResult* br = branchFor(sol, comp.id);
            if (!br) continue;
            double I = br->current; // nodeA -> nodeB
            maxAbsI = std::max(maxAbsI, std::abs(I));
            // В nodeB ток втекает при I>0 -> стрелка В узел = +I.
            incident[comp.nodeB].push_back({comp.id, +I});
            // Из nodeA ток вытекает при I>0 -> стрелка В узел = -I.
            incident[comp.nodeA].push_back({comp.id, -I});
        }

        double tol = std::max(1e-6, maxAbsI * 1e-3);

        for (const auto& [nodeId, list] : incident) {
            if (list.size() != 2) continue;       // только последовательные узлы
            if (nodeId == circuit.groundNodeId) continue;
            double a = list[0].towardNode;
            double b = list[1].towardNode;
            // Оба значимых тока не должны иметь ОДИН знак (обе стрелки В узел или
            // обе ИЗ узла = столкновение/расхождение). Один из них обязан быть
            // встречным: a и b противоположны по знаку (их сумма ≈ 0 по KCL).
            bool aActive = std::abs(a) > tol;
            bool bActive = std::abs(b) > tol;
            if (!aActive && !bActive) continue;   // мёртвая ветвь (разомкнутый ключ) — стрелок нет
            EXPECT_LE(a * b, tol * tol)
                << name << ": в последовательном узле " << nodeId
                << " стрелки тока двух элементов (id " << list[0].compId
                << " и id " << list[1].compId << ") направлены НЕсогласованно "
                << "(токи в узел: " << a << " A и " << b << " A)";
        }
    }
}
