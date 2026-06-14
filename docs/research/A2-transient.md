# A2 research: transient analysis (C/L)

Scope: `src/solver/CircuitSolver.*`, `src/simulation/LiveSim.*`,
`docs/REALTIME_TRANSIENT_MODEL.md`, `docs/electricity_model_notes.md`,
`tests/test_transient.cpp`. `src/` не менялся.

## 1. [PHYSICS-BUG] Сохраняется напряжение C, хотя документация обещает сохранение заряда

Severity: High

Проект: `src/solver/CircuitSolver.h:32`, `src/solver/CircuitSolver.h:36`,
`src/solver/CircuitSolver.cpp:193`, `src/solver/CircuitSolver.cpp:199`,
`src/simulation/LiveSim.h:17`, `src/simulation/LiveSim.h:65`,
`docs/electricity_model_notes.md:443`, `docs/electricity_model_notes.md:447`

Реальность/источники: `TransientState` хранит `capVoltage`, а companion строится
из `vOld`. При правке номинала C состояние сохраняется, значит сохраняется V,
но физический заряд `Q = C*V`; при изменении C это мгновенно меняет Q и энергию.
Локальный конспект сам задает C = Q/V и I_C = C*dV/dt. Qucs для емкостей
формулирует ток как производную заряда и отдельно упоминает charge conservation
control: https://qucs.sourceforge.net/tech/node26.html. Базовая формула энергии
и заряда: https://en.wikipedia.org/wiki/Capacitor.

Идеал: При изменении номинала C либо хранить/мигрировать `Q` и пересчитывать
`V = Q/C`, либо явно переименовать обещание в "сохраняется напряжение". Для UI
правок лучше ввести migration hook: старое C, новое C, старое V -> новый V с
сохранением Q, плюс тест "edit capacitance preserves charge".

## 2. [PHYSICS-BUG] Состояние L и разрыв цепи не защищают непрерывность тока/потока

Severity: High

Проект: `src/solver/CircuitSolver.h:33`, `src/solver/CircuitSolver.h:38`,
`src/solver/CircuitSolver.cpp:207`, `src/solver/CircuitSolver.cpp:210`,
`src/solver/CircuitSolver.cpp:229`, `src/simulation/LiveSim.h:65`,
`src/simulation/LiveSim.cpp:224`, `tests/test_diode_switch.cpp:111`

Реальность/источники: Код хранит `indCurrent` и сохраняет состояние при правках.
Для фиксированного идеального L ток не может измениться скачком, потому что
`V_L = L*dI/dt`; при разрыве пути требуется очень большое/бесконечное напряжение
или паразитная емкость/дуга. В MNA без внешнего пути KCL может заставить новый
ток стать нулевым за один шаг, а напряжение companion станет численным
артефактом порядка `L*I/dt`. Qucs: transient-анализ обязан учитывать энергоемкие
L/C, а для L базовое уравнение `V_L = L*dI/dt`:
https://qucs.sourceforge.net/tech/node23.html и
https://qucs.sourceforge.net/tech/node26.html. Базовая справка:
https://en.wikipedia.org/wiki/Inductor.

Идеал: Для открытия ветви с током L нужен явный режим: parasitic C/snubbers,
ограничитель напряжения, предупреждение "идеальная схема сингулярна", либо
специальное сохранение flux linkage. При изменении L нужно определить семантику:
сохраняем I или `lambda = L*I`; сейчас это не задокументировано и не покрыто
тестами.

## 3. [PHYSICS-BUG] Trapezoidal-history переживает события цепи и может стартовать с несовместимой истории

Severity: High

Проект: `src/solver/CircuitSolver.cpp:194`, `src/solver/CircuitSolver.cpp:197`,
`src/solver/CircuitSolver.cpp:202`, `src/solver/CircuitSolver.cpp:208`,
`src/solver/CircuitSolver.cpp:213`, `src/simulation/LiveSim.h:65`,
`src/simulation/LiveSim.h:69`, `src/simulation/LiveSim.cpp:95`,
`src/simulation/LiveSim.cpp:118`, `docs/REALTIME_TRANSIENT_MODEL.md:33`

Реальность/источники: Документ правильно предупреждает, что несовместимый старт
trapezoidal дает persistent ringing artifact, но код считает историю валидной
только по наличию `capCurrent`/`indVoltage`. `onCircuitEvent()` и
`wakeKeepSpeed()` будят солвер, но не инвалидируют trapezoidal-history после
правки топологии, переключателя или скачка источника. В SPICE-практике такие
разрывы требуют breakpoints, order reduction или демпфирования. Ngspice явно
имеет `XMU` для подавления trap ringing и предупреждает о риске лишнего
демпфирования: https://ngspice.sourceforge.io/docs/ngspice-manual.pdf.

Идеал: Хранить epoch/validity для history-current/history-voltage и после
топологического или кусочно-постоянного события принудительно делать один BE
restart для затронутых C/L. Для trapezoidal добавить breakpoint-семантику и тест:
RC/RL после switch/source step не получает alternating artifact.

## 4. [PHYSICS-BUG] Backward Euler по умолчанию сильно демпфирует LC/RLC

Severity: High

Проект: `src/simulation/LiveSim.h:37`, `src/simulation/LiveSim.cpp:69`,
`src/simulation/LiveSim.cpp:91`, `src/simulation/LiveSim.cpp:102`,
`src/simulation/LiveSim.cpp:132`, `src/solver/CircuitSolver.cpp:198`,
`src/solver/CircuitSolver.cpp:209`, `docs/REALTIME_TRANSIENT_MODEL.md:54`,
`docs/REALTIME_TRANSIENT_MODEL.md:55`

Реальность/источники: BE A-stable, но dissipative. Для гармонического LC
амплитуда BE за шаг умножается примерно на `1/sqrt(1+(omega*dt)^2)`. Текущий
auto-speed для LC берет период как story scale, при дефолтах дает около
`dt = T/50`; оценка дает амплитуду около `exp(-pi*omega*dt) ~= 0.67` за один
период, то есть заметное численное затухание. Qucs: BE - first order, TR -
second order, BE/TR/Gear2 A-stable:
https://qucs.sourceforge.net/tech/node24.html. Ngspice по умолчанию предлагает
trapezoidal/Gear, а BE получается через ограничение порядка:
https://ngspice.sourceforge.io/docs/ngspice-manual.pdf.

Идеал: Для RLC/LC не выбирать BE как единственный "визуально честный" default.
Либо переключать oscillatory circuits на trapezoidal/modified trap/Gear2, либо
уменьшать `dt` по допустимой energy error, либо явно показывать пользователю
"численное затухание BE". Нужен тест на LC: энергия не должна исчезать быстрее
заданного численного бюджета.

## 5. [IDEA] Добавить BDF/Gear или modified trapezoidal как третий режим

Severity: Medium

Проект: `src/solver/CircuitSolver.h:24`, `src/solver/CircuitSolver.h:27`,
`src/solver/CircuitSolver.cpp:198`, `src/solver/CircuitSolver.cpp:202`,
`src/solver/CircuitSolver.cpp:209`, `src/solver/CircuitSolver.cpp:212`,
`docs/REALTIME_TRANSIENT_MODEL.md:55`

Реальность/источники: Сейчас есть только BE и pure trapezoidal. Pure TR точнее
на гладких RC/RL, но склонна к trap ringing на жестких/переключаемых схемах.
Ngspice поддерживает `METHOD=Gear|trapezoidal`, `MAXORD`, `XMU` и прямо говорит,
что небольшое снижение `XMU` может подавлять trap ringing:
https://ngspice.sourceforge.io/docs/ngspice-manual.pdf. Qucs описывает Gear/BDF
как важные multistep-формулы для transient:
https://qucs.sourceforge.net/tech/node24.html.

Идеал: Добавить `IntegrationMethod::Gear2` или `ModifiedTrapezoidal(xmu)`.
Gear2 полезен для жестких switched transient с контролируемым численным
демпфированием; modified trap полезен как компромисс против ringing без полного
перехода на BE.

## 6. [DISCREPANCY] Документация по dt/UI устарела относительно LiveSim

Severity: Medium

Проект: `docs/REALTIME_TRANSIENT_MODEL.md:53`, `docs/REALTIME_TRANSIENT_MODEL.md:57`,
`src/simulation/LiveSim.h:23`, `src/simulation/LiveSim.h:36`,
`src/simulation/LiveSim.h:121`, `src/simulation/LiveSim.cpp:128`,
`src/simulation/LiveSim.cpp:133`, `src/simulation/LiveSim.cpp:143`

Реальность/источники: Документ говорит "Default dt = 1 ms, user-settable
1 us .. 1 s" и "steps per frame are capped at 2000". Код `LiveSim` считает
`dt = speed / solveHz`, начальное поле `m_dt = 1/60`, clamp `1e-9..1.0`, а
`maxStepsPerFrame = 8`. Это не физический дефект solver, но неверная
операционная модель в docs.

Идеал: Обновить docs: transient solver принимает `dt` как API, а live UI
управляет не прямым `dt`, а sim-speed + solveHz; frame cap равен 8, simulation
lags rather than catches up.

## 7. [IDEA] Auto-speed по tau не равен контролю точности dt

Severity: Medium

Проект: `src/simulation/LiveSim.cpp:45`, `src/simulation/LiveSim.cpp:57`,
`src/simulation/LiveSim.cpp:62`, `src/simulation/LiveSim.cpp:96`,
`src/simulation/LiveSim.cpp:103`, `src/simulation/LiveSim.cpp:106`,
`src/simulation/LiveSim.cpp:132`, `docs/REALTIME_TRANSIENT_MODEL.md:51`

Реальность/источники: `smallestTimeConstant()` и `oscillationTimescale()` задают
story speed, а не LTE/error control. Быстрые tau ниже `minTau` вообще
игнорируются для скорости; многополюсные RC/RL/RLC сети не сводятся к одному
`R_th*C` или `L/R_th`. Qucs отдельно описывает adaptive step-size control:
слишком большой шаг дает inaccurate или wrong results, слишком маленький
тратит время: https://qucs.sourceforge.net/tech/node25.html. Ngspice `.tran`
разделяет print step и maximum internal timestep:
https://ngspice.sourceforge.io/docs/ngspice-manual.pdf.

Идеал: Оставить auto-speed как UX-эвристику, но добавить независимый accuracy
policy: `dt <= tau/N`, `dt <= T_osc/N`, breakpoints на переключениях, либо LTE
оценку для C/L. В docs явно разделить "скорость истории" и "численная точность".

## 8. [PHYSICS-BUG] `stepTransient()` не валидирует dt

Severity: Medium

Проект: `src/solver/CircuitSolver.cpp:186`, `src/solver/CircuitSolver.cpp:199`,
`src/solver/CircuitSolver.cpp:202`, `src/solver/CircuitSolver.cpp:210`,
`src/solver/CircuitSolver.cpp:212`, `src/solver/CircuitSolver.cpp:236`,
`tests/test_transient.cpp:63`

Реальность/источники: Публичный API делит на `dt` для C, умножает `dt/L` для L
и затем делает `state.time += dt`. `LiveSim` clamp-ит свой `m_dt`, но tests и
любой прямой вызов solver могут передать 0, отрицательное или NaN. В численной
интеграции step size - это `t(n+1)-t(n)` и должен быть положительным:
https://qucs.sourceforge.net/tech/node24.html.

Идеал: В `stepTransient()` проверять `std::isfinite(dt) && dt > 0`. При dt=0
возвращать `solveTransientSnapshot()` или бросать/ASSERT в зависимости от
проектного стиля; отрицательный dt для этой модели запретить.

## 9. [DISCREPANCY] Тесты слабее некоторых утверждений документа

Severity: Low

Проект: `docs/REALTIME_TRANSIENT_MODEL.md:55`,
`docs/REALTIME_TRANSIENT_MODEL.md:67`, `tests/test_transient.cpp:128`,
`tests/test_transient.cpp:148`, `tests/test_transient.cpp:220`,
`tests/test_transient.cpp:232`

Реальность/источники: Документ говорит, что trapezoidal на `dt=tau/20` дает
ошибку примерно на два порядка ниже BE, но тест проверяет только
`trapezoidal error < BE error`. Документ говорит energy matches to ~3%, тесты
покрывают только простые RC/RL, не LC/RLC и не события switch/source.

Идеал: Либо ослабить docs до того, что реально тестируется, либо добавить
числовой ratio-тест для RC и отдельные tests: LC energy drift, switched
trapezoidal restart, charge preservation on C edit, L open-circuit behavior.

## 10. [IDEA] Tellegen-тест нужен, но не доказывает сохранение энергии C/L

Severity: Medium

Проект: `src/solver/CircuitSolver.cpp:131`, `tests/test_transient.cpp:168`,
`tests/test_transient.cpp:176`, `docs/REALTIME_TRANSIENT_MODEL.md:61`,
`docs/REALTIME_TRANSIENT_MODEL.md:62`

Реальность/источники: `sum(branch.power) == 0` проверяет совместимость KCL/KVL
и sign convention MNA. Это ровно дух теоремы Теллегена: если напряжения
удовлетворяют KVL, а токи KCL, сумма `v*i` равна нулю:
https://en.wikipedia.org/wiki/Tellegen%27s_theorem. Но это не гарантирует, что
дискретный интегратор сохраняет физическую энергию `1/2*C*V^2 + 1/2*L*I^2`.
BE может иметь идеальный Tellegen balance на каждом solved point и одновременно
численно демпфировать LC.

Идеал: Оставить Tellegen как matrix/sign regression. Добавить отдельные
инварианты: charge conservation для capacitive islands, current/flux continuity
для inductors, stored-energy drift для LC/RLC, dissipated energy >= 0 для
резистивных сетей.

## 11. [DISCREPANCY] `electricity_model_notes.md` не отражает уже реализованный L/transient слой

Severity: Low

Проект: `docs/electricity_model_notes.md:423`, `docs/electricity_model_notes.md:441`,
`docs/electricity_model_notes.md:447`, `docs/electricity_model_notes.md:466`,
`docs/electricity_model_notes.md:470`, `docs/electricity_model_notes.md:478`,
`docs/REALTIME_TRANSIENT_MODEL.md:71`

Реальность/источники: Общий конспект говорит, что водная аналогия плохо
описывает C/L и переходные процессы, затем дает только базовую емкость. В нем
нет индуктивности, `V_L=L*dI/dt`, `tau=L/R`, LC/RLC, companion-моделей или
ограничений transient-слоя, хотя код и отдельный transient doc уже это имеют.
Qucs transient intro явно ставит C и L как energy-storing components:
https://qucs.sourceforge.net/tech/node23.html.

Идеал: Добавить короткий раздел "Индуктивность и переходные процессы" или
ссылку из общего конспекта на `REALTIME_TRANSIENT_MODEL.md`, чтобы
образовательная физика и кодовая модель не расходились.

## 12. [VISUAL-BUG] Snapshot со stiff conductance не всегда буквально "держит" несовместимые состояния

Severity: Medium

Проект: `src/solver/CircuitSolver.cpp:240`, `src/solver/CircuitSolver.cpp:247`,
`src/solver/CircuitSolver.cpp:251`, `src/simulation/LiveSim.cpp:164`,
`src/simulation/LiveSim.cpp:169`, `docs/REALTIME_TRANSIENT_MODEL.md:47`,
`docs/REALTIME_TRANSIENT_MODEL.md:49`, `tests/test_transient.cpp:251`

Реальность/источники: Документ описывает snapshot как честный t=0+ с C held at
stored Vc и L held at stored Il. Код реализует это finite conductance
`1e9`/`1e-12`, а не идеальными constraints. Для одного RC тест корректен, но
если после правки схемы два заряженных конденсатора с разными V оказываются
идеально соединены, finite stiff sources дадут компромисс/большие токи в
рендере без явного события перераспределения заряда. Это не обязательно
ошибка solve step, но это визуально не "каждый C удержан ровно".

Идеал: В docs уточнить, что snapshot - finite stiff approximation. Для
топологически несовместимых stored states показывать предупреждение, делать
instant charge redistribution с energy loss model, либо требовать один
transient step/breakpoint перед отображением.

## Источники

- Qucs Technical Papers, "Transient Analysis":
  https://qucs.sourceforge.net/tech/node23.html
- Qucs Technical Papers, "Integration methods":
  https://qucs.sourceforge.net/tech/node24.html
- Qucs Technical Papers, "Predictor-corrector methods / Adaptive step-size control":
  https://qucs.sourceforge.net/tech/node25.html
- Qucs Technical Papers, "Energy-storage components":
  https://qucs.sourceforge.net/tech/node26.html
- Ngspice User's Manual, Version 46, 2026-03-31:
  https://ngspice.sourceforge.io/docs/ngspice-manual.pdf
- Tellegen's theorem:
  https://en.wikipedia.org/wiki/Tellegen%27s_theorem
- RC time constant:
  https://en.wikipedia.org/wiki/RC_time_constant
- RL circuit / time-domain response:
  https://en.wikipedia.org/wiki/RL_circuit
- Capacitor:
  https://en.wikipedia.org/wiki/Capacitor
- Inductor:
  https://en.wikipedia.org/wiki/Inductor
- (из знаний) L. W. Nagel, "SPICE2: A Computer Program to Simulate
  Semiconductor Circuits", UCB/ERL M520, 1975.
- (из знаний) C. W. Gear, "Numerical Initial Value Problems in Ordinary
  Differential Equations", Prentice-Hall, 1971.
- (из знаний) K. S. Kundert, "The Designer's Guide to SPICE and Spectre",
  Kluwer, 1995.
