# A1 — Солвер цепей и численные методы

Research-агент A1 для electricity-lab. Тема: СОЛВЕР ЦЕПЕЙ И ЧИСЛЕННЫЕ МЕТОДЫ.
Дата: 2026-06-14.

Формат записи: `[PHYSICS-BUG | DISCREPANCY | VISUAL-BUG | IDEA]`, Severity,
«Проект:» (file:line), «Реальность/источники:» (URL или пометка),
«Идеал:».

---

## Как сейчас устроено в проекте (ШАГ 1)

- `CircuitSolver::solveWithCompanions` собирает плотную MNA-систему:
  неизвестные = все неграундовые потенциалы + токи через независимые источники
  напряжения (`src/solver/CircuitSolver.cpp:27-39`).
- Резисторы и провода штампуются как проводимости: диагональ `+g`, внедиагональ
  `-g` (`src/solver/CircuitSolver.cpp:53-56`, `:65-68`).
- Источник напряжения добавляет отдельную строку/столбец и неизвестный ток:
  `+1/-1` в KCL-строках и уравнение `Va - Vb = value`
  (`src/solver/CircuitSolver.cpp:69-74`). Ток источника читается из добавленной
  MNA-переменной (`src/solver/CircuitSolver.cpp:116-119`).
- Провода, замкнутые ключи, DC-индукторы и stiff-снапшот конденсатора — не
  идеальные топологические слияния, а конечная проводимость `1e9 S`
  (`src/solver/CircuitSolver.cpp:6`, `:10-12`, `:67-68`, `:181`, `:247`).
- Разомкнутые элементы моделируются фиксированной утечкой `1e-12 S`
  (`src/solver/CircuitSolver.cpp:7`, `:67-68`, `:179`, `:251`).
- DC: конденсатор = `gmin`-утечка, индуктор = `1e9 S` short
  (`src/solver/CircuitSolver.cpp:175-183`; то же задокументировано в
  `src/solver/CircuitSolver.h:46`).
- Transient: C/L идут через companion-модели `i_ab = g*(Va-Vb) - ieq`;
  Backward Euler и Trapezoidal реализованы прямо в `stepTransient`
  (`src/solver/CircuitSolver.h:49-57`, `src/solver/CircuitSolver.cpp:186-237`).
- Диоды решаются outer fixed-point итерацией по состояниям open/short,
  максимум 24 прохода, без статуса сходимости
  (`src/solver/CircuitSolver.cpp:138-173`).
- Линейная система решается самописным плотным Гауссом-Жорданом с частичным
  выбором pivot по столбцу; если `|pivot| < 1e-15`, столбец просто пропускается
  (`src/math/LinearSystem.h:17-44`).
- `Circuit::toDistributed` уже превращает провод в цепочку резисторов
  (`src/circuit/Circuit.h:143-190`), хотя часть базового документа всё еще
  называет распределенный провод «будущим слоем».

---

## Находки (ШАГ 2 + ШАГ 3)

- ### [IDEA] Текущая сборка источников напряжения — правильная MNA-форма, это надо закрепить как контракт
  **Severity: Low / confidence: high**

  - **Проект:** `docs/model_assumptions.md:15-17` заявляет MNA с потенциалами
    узлов и токами через источники напряжения; код это реально делает:
    `unknowns = N - 1 + numVsource` (`src/solver/CircuitSolver.cpp:37`),
    источник штампуется в B/C-блоки (`src/solver/CircuitSolver.cpp:69-74`),
    ток источника возвращается из добавленной переменной
    (`src/solver/CircuitSolver.cpp:116-119`).
  - **Реальность/источники:** Qucs описывает MNA как систему `[A]x=z`, где для
    `N` узлов и `M` независимых источников напряжения матрица имеет размер
    `(N+M)x(N+M)`, а нижняя часть вектора неизвестных — токи через эти источники:
    https://qucs.sourceforge.net/tech/node14.html
    Ngspice/Spice analog simulation решает уравнения Кирхгофа через матричный
    solve на каждом operating point/time point:
    https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
  - **Идеал:** сохранить этот подход, но явно зафиксировать в документации
    sign convention: `BranchResult.current` для `VoltageSource` — это MNA-ток
    из `nodeA` в `nodeB`; положительная мощность `I*(Va-Vb)` означает
    поглощение, отрицательная — отдачу источником. Это снизит риск будущих
    визуальных/энергетических инверсий.

- ### [PHYSICS-BUG] Сингулярная или недоопределенная матрица решается молча
  **Severity: High / confidence: high**

  - **Проект:** `LinearSystem::solve` при `abs(pivot) < 1e-15` делает `continue`
    без ошибки, статуса, ранга или residual-check (`src/math/LinearSystem.h:27`).
    `CircuitSolver` всегда использует возвращенный `x` как валидное решение
    (`src/solver/CircuitSolver.cpp:92-104`, `:116-133`). Тесты фактически
    закрепляют произвольное «нулевое» решение для плавающего резистора
    (`tests/test_solver.cpp:219-231`) и проверяют только `isfinite` для
    параллельных одинаковых источников (`tests/test_solver.cpp:401-414`).
  - **Реальность/источники:** ngspice прямо предупреждает, что DC operating point
    труден для схем с floating nodes и применяет отдельные convergence aids:
    https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
    LAPACK `dgesv` при нулевом pivot возвращает `INFO > 0`: факторизация дошла до
    точно сингулярного `U(i,i)`, и решение не может быть вычислено:
    https://www.netlib.org/lapack/explore-html/d8/da6/group__gesv_ga831ce6a40e7fd16295752d18aed2d541.html
  - **Идеал:** заменить `std::vector<double> solve()` на `LinearSolveResult`
    (`x`, `status`, `rank/singularColumn`, `residualNorm`, возможно `rcond`).
    В `CircuitSolver` различать: нет reference/DC path, противоречивые идеальные
    источники, плохо обусловленная, но решаемая система. В UI/обучении показывать
    «floating subcircuit / inconsistent ideal sources», а не рисовать произвольные
    0 В как физический факт.

- ### [PHYSICS-BUG] Конфликтующие идеальные источники напряжения не валидируются
  **Severity: High / confidence: high**

  - **Проект:** каждый `VoltageSource` добавляет жесткое уравнение
    `Va - Vb = comp.value` (`src/solver/CircuitSolver.cpp:69-73`), но перед
    solve нет проверки топологии/совместности. Существующий тест покрывает только
    две одинаковые параллельные ЭДС и ожидает конечные числа
    (`tests/test_solver.cpp:401-414`); случай `5 V || 3 V` или замкнутый контур
    идеальных источников с несовместимой суммой не диагностируется.
  - **Реальность/источники:** идеальный источник напряжения поддерживает заданное
    напряжение независимо от тока; два идеальных независимых источника напрямую
    параллельно допустимы только при одинаковом напряжении, иначе система
    уравнений противоречива. Реальные источники имеют ненулевое внутреннее
    сопротивление:
    https://en.wikipedia.org/wiki/Voltage_source
    MNA как раз вводит дополнительные уравнения для voltage-defined branches,
    поэтому несовместимые constraints должны проявляться как сингулярность или
    inconsistency, а не как «конечный ток»:
    https://qucs.sourceforge.net/tech/node14.html
  - **Идеал:** добавить `CircuitValidator` до solve: искать параллельные
    несовместимые источники, циклы из идеальных voltage-defined ветвей с
    несовместимой KVL-суммой, источник в идеальном short с ненулевым напряжением.
    Для учебного режима можно предлагать «добавь series resistance/internal R»,
    а для солвера — возвращать `InconsistentIdealSources`.

- ### [PHYSICS-BUG] Фиксированный `gmin = 1e-12 S` смешивает численную регуляризацию с физикой
  **Severity: Medium / confidence: high**

  - **Проект:** `kOpenConductance = 1e-12` используется как «open» для DC
    конденсаторов, разомкнутых ключей, блокирующих диодов и snapshot-индуктора
    (`src/solver/CircuitSolver.cpp:7`, `:67-68`, `:154`, `:179`, `:251`).
    `docs/REALTIME_TRANSIENT_MODEL.md:44` формулирует это как gmin leak «to keep
    the matrix non-singular». При этом глобального `rshunt`, gmin stepping или
    source stepping нет; LiveSim уже содержит защитные комментарии против
    «фиктивного делителя из gmin-утечек» (`src/simulation/LiveSim.cpp:167-168`,
    `:215-219`).
  - **Реальность/источники:** ngspice для `.op` после обычной попытки использует
    последовательность aids: gmin stepping, source stepping и optional transient
    operating point; gmin стартует большим значением и меняется, source stepping
    поднимает источники от 0 до 100%:
    https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
    Qucs для нелинейной DC-задачи описывает Newton-Raphson как пересборку
    Jacobian/MNA на итерациях:
    https://qucs.sourceforge.net/tech/node16.html
  - **Идеал:** разделить два режима: (1) физическая модель leakage/off-conductance,
    явно показываемая пользователю, и (2) численная регуляризация/continuation,
    не выдаваемая за элемент схемы. Для текущего учебного солвера минимум:
    параметризовать `gmin`, документировать фантомные токи, детектировать floating
    islands отдельно. Для будущей нелинейности: gmin stepping + source stepping +
    damping/limiting вместо одного фиксированного `1e-12`.

- ### [PHYSICS-BUG] Диапазон проводимостей `1e-12..1e9 S` делает MNA плохо масштабированной
  **Severity: Medium / confidence: high**

  - **Проект:** open/closed аппроксимации дают отношение `1e21`
    (`src/solver/CircuitSolver.cpp:6-7`, `:67-68`, `:154`). Нулевой или почти
    нулевой резистор также превращается в `1e9 S`
    (`src/solver/CircuitSolver.cpp:9-12`). Решатель не делает equilibration,
    scaling, condition estimate или iterative refinement
    (`src/math/LinearSystem.h:17-44`).
  - **Реальность/источники:** condition number линейной системы задает
    чувствительность решения к малым ошибкам данных; при большом condition number
    даже малая ошибка в `b` или округление может сильно менять `x`:
    https://en.wikipedia.org/wiki/Condition_number
    LAPACK отдельно предоставляет оценку reciprocal condition number и
    equilibration/scaling routines для линейных систем:
    https://www.netlib.org/lapack/lug/node38.html
    Eigen `SparseLU` тоже отмечает, что для badly scaled matrices может быть
    полезно scaling/equilibration:
    https://eigen.tuxfamily.org/dox/classEigen_1_1SparseLU.html
  - **Идеал:** либо сжимать идеальные проводники топологически (union-find по
    `Wire`/closed switch/ideal short) до сборки MNA, либо выбирать Ron/Roff
    относительно масштаба схемы и оценивать `rcond/residual`. Для больших
    распределенных цепей перейти на sparse storage и solver с ordering/scaling.

- ### [IDEA] Гаусс-Жордан стоит заменить на solver API с LU/status; Eigen подходит, но нужен правильный класс
  **Severity: Medium / confidence: high**

  - **Проект:** `LinearSystem` хранит плотную `vector<vector<double>>`, решает
    Гауссом-Жорданом до reduced-row формы, нормализуя pivot row и зануляя все
    строки на каждом столбце (`src/math/LinearSystem.h:17-44`). Частичный выбор
    pivot есть (`src/math/LinearSystem.h:21-29`), но статуса сингулярности нет.
  - **Реальность/источники:** стандартный dense solve в LAPACK (`dgesv`) делает
    LU with partial pivoting and row interchanges, затем triangular solve, и
    возвращает `INFO` при точной сингулярности:
    https://www.netlib.org/lapack/explore-html/d8/da6/group__gesv_ga831ce6a40e7fd16295752d18aed2d541.html
    Eigen `PartialPivLU` рассчитан на square invertible matrices и не является
    rank-revealing; для не-full-rank нужен `FullPivLU`. Eigen `SparseLU` — sparse
    supernodal LU для general matrices с ordering (COLAMD/AMD/METIS):
    https://eigen.tuxfamily.org/dox/classEigen_1_1PartialPivLU.html
    https://eigen.tuxfamily.org/dox/classEigen_1_1SparseLU.html
  - **Идеал:** краткосрочно: оставить маленький dense solver, но вернуть
    `status + residual`. Среднесрочно: Eigen dense `PartialPivLU` для нормальных
    учебных схем, `FullPivLU`/rank-revealing путь для диагностики, `SparseLU` для
    распределенных проводов/больших сетей. Не использовать `PartialPivLU` как
    детектор сингулярности: его собственная документация этого не обещает.

- ### [VISUAL-BUG] Плавающие подсхемы получают произвольный абсолютный потенциал, но UI может воспринимать его как физический
  **Severity: Medium / confidence: medium**

  - **Проект:** если `groundNodeId` не найден, solver молча берет первый узел как
    ground (`src/solver/CircuitSolver.cpp:32-35`). В изолированной резистивной
    подсхеме без источников тест ожидает все узлы по 0 В
    (`tests/test_solver.cpp:219-231`). Для визуальных слоев это выглядит как
    физически известный absolute potential, хотя у floating island определены
    только разности потенциалов внутри заданных constraints.
  - **Реальность/источники:** MNA/nodal analysis требует reference node; Qucs
    прямо исключает ground из вектора неизвестных:
    https://qucs.sourceforge.net/tech/node14.html
    Ngspice выделяет floating nodes как класс схем, где DC solution может быть
    трудно найти:
    https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
  - **Идеал:** валидатор должен помечать connected components без DC-path к
    reference. Для визуализации можно выбрать canonical offset (например, mean=0)
    только с явной пометкой `floating`, а не смешивать это с реальным ground.
    В обучении это хороший момент: напряжение относительно, но floating island
    без связи с reference не имеет определенного абсолютного уровня.

- ### [DISCREPANCY] `docs/model_assumptions.md` отстал от кода по C/L, transient, диоду, ключу и распределенным проводам
  **Severity: Medium / confidence: high**

  - **Проект:** документ говорит «DC steady-state», «все элементы линейны»,
    «отсутствие емкости и индуктивности» и «распределенный провод — будущий слой»
    (`docs/model_assumptions.md:25-33`, `:48-57`, `:168-175`). Код уже имеет
    C/L companion transient (`src/solver/CircuitSolver.cpp:186-237`), DC-модели
    C/L (`src/solver/CircuitSolver.cpp:175-183`), diode/switch
    (`src/circuit/Circuit.h:16-17`, `src/solver/CircuitSolver.cpp:138-173`) и
    `Circuit::toDistributed` (`src/circuit/Circuit.h:143-190`). Более свежие docs
    это знают (`docs/REALTIME_TRANSIENT_MODEL.md:7-16`, `:42-69`,
    `docs/ELEMENT_LIBRARY.md:9-18`).
  - **Реальность/источники:** для симулятора корректно иметь lumped DC + transient
    companion models; ngspice аналогично различает DC operating point, AC и
    transient, а в `.op` индуктивности short, емкости open:
    https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
  - **Идеал:** обновить `docs/model_assumptions.md`: текущий уровень 1 =
    lumped MNA DC + transient BE/TR for linear C/L + ideal diode/switch PWL +
    численные Ron/Roff/gmin. Убрать «будущий слой» для distributed wire или
    уточнить: базовая схемная модель ideal-wire, опциональная проекция
    `toDistributed` уже реализована как 1D resistive chain.

- ### [DISCREPANCY] Документы называют провода идеальными, код решает их как 1 нОм-эквивалент
  **Severity: Low / confidence: high**

  - **Проект:** `docs/model_assumptions.md:28-29` и `:170-172` говорят
    «идеальные провода, R_wire = 0, эквипотенциальные». В коде `Wire` получает
    `kWireConductance = 1e9 S`, то есть эквивалент `1e-9 Ohm`
    (`src/solver/CircuitSolver.cpp:6`, `:10`, `:65-66`); near-zero resistor тоже
    принудительно становится таким short (`src/solver/CircuitSolver.cpp:11-12`).
    Тесты допускают малое, но ненулевое отличие (`tests/test_solver.cpp:121-137`,
    `:277-300`).
  - **Реальность/источники:** идеальный провод/идеальный источник — математический
    предел; практический численный симулятор часто использует конечные Ron/Roff
    или топологическое слияние узлов. При прямой численной аппроксимации надо
    управлять масштабом матрицы и точностью:
    https://www.netlib.org/lapack/lug/node38.html
  - **Идеал:** в документах писать «idealized as equipotential; implemented as
    1e9 S numerical short in the lumped solver» либо заменить провода на
    предварительное слияние узлов, тогда код станет ближе к `R_wire = 0` и лучше
    по обусловленности.

- ### [IDEA] Transient companion-модели согласованы с учебной целью, но public API не защищен от плохого `dt`
  **Severity: Low / confidence: medium**

  - **Проект:** `stepTransient` использует `dt` напрямую: для C `g=C/dt`,
    для L `g=dt/L` или `dt/(2L)` (`src/solver/CircuitSolver.cpp:198-213`).
    Значение C/L снизу clamp-ится до `1e-15` (`src/solver/CircuitSolver.cpp:192`,
    `:206`), но `dt <= 0`, `NaN`, `inf` не отсекаются в самом solver API.
    UI-док ограничивает пользовательский `dt` (`docs/REALTIME_TRANSIENT_MODEL.md:51-57`),
    но низкоуровневый метод публичный (`src/solver/CircuitSolver.h:51-52`).
  - **Реальность/источники:** ngspice transient analysis управляет timestep и
    initial operating point как частью алгоритма, а не как произвольным числом
    без проверки:
    https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
    Backward Euler A-stable не означает «любой dt физически точен»: большой dt
    сохраняет boundedness, но увеличивает дискретизационную ошибку
    (из знаний, без живого URL: Hairer & Wanner, *Solving Ordinary Differential
    Equations II*, разделы про A-stability/stiff problems).
  - **Идеал:** в `stepTransient` явно валидировать `dt > 0 && isfinite(dt)`,
    возвращать ошибку или clamp на уровне API. В research/docs разделить
    stability и accuracy: BE устойчив для stiff RC/RL, но не освобождает от
    timestep policy; TR точнее, но может давать ringing на резких/stiff входах.

---

## Сводка

- PHYSICS-BUG: 4
- DISCREPANCY: 2
- VISUAL-BUG: 1
- IDEA: 3

Главный вывод: MNA-скелет источников напряжения реализован правильно, но
численный слой пока учебно-минимальный: нет статуса сингулярности/несовместности,
fixed `gmin` смешан с физикой, а `1e21` диапазон проводимостей легко портит
обусловленность. Самый полезный следующий шаг — не «переписать физику», а
ввести solver status + circuit validation + residual/rcond diagnostics.

---

## Источники

- Qucs Technical Papers — Modified Nodal Analysis:
  https://qucs.sourceforge.net/tech/node14.html
- Qucs Technical Papers — Non-linear DC Analysis / Newton-Raphson:
  https://qucs.sourceforge.net/tech/node16.html
- Ngspice User Manual (версия manual на сайте ngspice, открыт 2026-06-14):
  https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
- LAPACK `dgesv` documentation — LU with partial pivoting and singular `INFO`:
  https://www.netlib.org/lapack/explore-html/d8/da6/group__gesv_ga831ce6a40e7fd16295752d18aed2d541.html
- LAPACK Users' Guide — Linear Equations, condition estimate, refinement,
  equilibration:
  https://www.netlib.org/lapack/lug/node38.html
- Eigen `PartialPivLU` documentation:
  https://eigen.tuxfamily.org/dox/classEigen_1_1PartialPivLU.html
- Eigen `SparseLU` documentation:
  https://eigen.tuxfamily.org/dox/classEigen_1_1SparseLU.html
- Voltage source — ideal source constraints and real source resistance:
  https://en.wikipedia.org/wiki/Voltage_source
- Condition number — sensitivity of linear systems:
  https://en.wikipedia.org/wiki/Condition_number
- Gaussian elimination — numerical instability and partial pivoting:
  https://en.wikipedia.org/wiki/Gaussian_elimination
- SPICE at UC Berkeley — historical SPICE distribution page:
  https://ptolemy.berkeley.edu/projects/embedded/pubs/downloads/spice/index.htm
