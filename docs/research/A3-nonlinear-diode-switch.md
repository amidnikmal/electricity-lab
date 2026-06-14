# A3 — Нелинейные элементы: диод и ключ

Research-агент A3 для electricity-lab. Тема: НЕЛИНЕЙНЫЕ ЭЛЕМЕНТЫ (диод, ключ).
Дата: 2026-06-14.

Формат записи: `[PHYSICS-BUG | DISCREPANCY | VISUAL-BUG | IDEA]`, Severity,
«Проект:» (file:line), «Реальность/источники:» (URL), «Идеал:».

---

## Как сейчас устроено в проекте (ШАГ 1)

### Диод

- Тип: `ComponentType::Diode` — комментарий «ideal piecewise-linear: conducts A->B
  when forward biased» (`src/circuit/Circuit.h:16`).
- `Component.value` для диода **не используется** (заявлено «ideal»;
  `docs/ELEMENT_LIBRARY.md:7`, `src/ui/CanvasInteraction.cpp:67` возвращает 0.0).
- Модель решателя — это **НЕ** кусочно-линейный диод с порогом и наклоном, а
  **идеальный ключ-по-знаку**: два состояния, каждое — чистая проводимость без
  ЭДС-смещения:
  - проводит → `g = kWireConductance = 1e9 S` (`CircuitSolver.cpp:154`, `:6`);
  - блокирует → `g = kOpenConductance = 1e-12 S` (`CircuitSolver.cpp:154`, `:7`).
  - companion-источник `ieq = 0` всегда (`CircuitSolver.cpp:154`).
- Алгоритм сходимости — fixed-point по дискретным состояниям диодов
  (`CircuitSolver::solveIterative`, `CircuitSolver.cpp:138-173`):
  старт «все блокируют»; проводящий диод с током `< -1e-12` → блокирует;
  блокирующий диод с `voltageDrop > 1e-9` → проводит; до 24 проходов.
- Работает одинаково в DC и внутри каждого transient-шага (companions диодов
  добавляются в `stepTransient`/`solveTransientSnapshot` через `solveIterative`).
- Тесты (`tests/test_diode_switch.cpp`): forward → `voltageDrop == 0.0` (порог 1e-6),
  ток = V/R; reverse → ток = 0, диод держит всё напряжение; пик-детектор
  (диод+конденсатор) держит заряд после спада источника.
- В UI/проекциях: треугольник+черта (`ProjectionBuilder.cpp:429,1414,1802`),
  механика — храповик/ratchet (`MechanicsMapping.h:15`), гидравлика — обратный
  клапан/check valve (`HydraulicMapping.h:18`).

### Ключ

- Тип: `ComponentType::Switch`, `value >= 0.5` замкнут, иначе разомкнут
  (`Circuit.h:17`).
- Решатель: прямой stamp проводимости без companion и без итераций —
  `comp.value >= 0.5 ? kWireConductance(1e9) : kOpenConductance(1e-12)`
  (`CircuitSolver.cpp:67-68`, ток на `:122-123`).
- Топология цепи **фиксирована**; замыкание/размыкание — это только подмена
  проводимости, поэтому состояние реактивных элементов (Vc, Il) переживает
  щелчок (`docs/ELEMENT_LIBRARY.md:27-28`; тест
  `OpeningMidTransientFreezesCapacitorCharge`).
- По умолчанию ключ замкнут (`CanvasInteraction.cpp:68`), но демо-схемы
  стартуют разомкнутыми, чтобы студент сам «запускал» процесс
  (`DemoCircuits.h:116,127`).

### Источник напряжения — важный контекст для нелинейных демо

- **AC-источника НЕТ.** `VoltageSource` — это только постоянное `value` (DC).
  Поиск по `sin|sinusoid|frequency|omega|AC|rectif` в `src/` не дал ни одного
  синусоидального источника напряжения; единственный «динамический» путь —
  ручная прокрутка кривошипа `driveSource` (`MainWindow.cpp:176`,
  `CircuitCanvas.cpp:162`), которая задаёт мгновенное ЭДС от скорости вращения,
  а не периодический сигнал.
- Следствие: классическое выпрямление AC (half-wave rectifier) сейчас
  **продемонстрировать нечем** — диодные демо ограничены DC-пиком/блокировкой.

---

## Находки (ШАГ 2 + ШАГ 3)

### [DISCREPANCY] Диод назван «piecewise-linear», но физически это идеальный диод (нулевой порог)
**Severity: High**

- **Проект:** `src/circuit/Circuit.h:16` («ideal piecewise-linear»),
  `docs/ELEMENT_LIBRARY.md:17,22`, `src/solver/CircuitSolver.cpp:154`
  (`{on ? 1e9 : 1e-12, 0.0}` — companion `ieq` всегда 0, ЭДС-смещения нет),
  `tests/test_diode_switch.cpp:45` (`EXPECT_NEAR(diode->voltageDrop, 0.0, 1e-6)`).
- **Реальность/источники:** настоящая кусочно-линейная (PWL) модель — это
  «идеальный диод + источник напряжения V_γ + последовательный резистор R_s»,
  то есть НЕНУЛЕВОЙ порог. Для кремния порог обычно берут 0.6–0.7 В («Values of
  0.6 or 0.7 volts are commonly used for silicon diodes»), для Шоттки ~0.2–0.3 В,
  для Ge ~0.3 В, для красного LED ~1.8 В.
  https://en.wikipedia.org/wiki/Diode_modelling
  https://www.allaboutcircuits.com/technical-articles/forward-conducting-diodes-exponential-and-piecewise-linear-analysis/
- **Идеал:** либо переименовать модель в «ideal diode (zero-drop)» (что физически
  корректно и есть отдельная признанная модель — Wikipedia «ideal diode model»),
  либо реально сделать PWL: ввести `value` = пороговое V_f (по умолчанию 0.7 В Si),
  стампить companion как идеальный диод последовательно со смещением
  (`ieq = g * V_f` в проводящем состоянии) + опционально малый R_s. Тогда
  forward-тест станет `voltageDrop ≈ V_f`, а пик-детектор зарядит C до `V_peak − V_f`,
  как в жизни. Сейчас термин «PWL» в коде/доках вводит в заблуждение.

### [PHYSICS-BUG] Идеальный диод даёт нулевое падение → нет потерь и завышенный ток/заряд
**Severity: Medium**

- **Проект:** `tests/test_diode_switch.cpp:44-45` (ток = 5.0/1000, drop = 0),
  `:80` (пик-детектор заряжает C `> 4.5 В` от источника 5 В — фактически до ~5 В),
  `CircuitSolver.cpp:154`.
- **Реальность/источники:** на реальном Si-диоде в прямом включении падает
  ~0.6–0.7 В, поэтому ток в RC-цепочке меньше `(V−V_f)/R`, а пик-детектор держит
  `V_peak − V_f` (≈4.3 В при 5 В пике), и на диоде рассеивается мощность `I·V_f`.
  https://en.wikipedia.org/wiki/Diode_modelling
  https://www.allaboutcircuits.com/textbook/semiconductors/chpt-3/peak-detector/
- **Идеал:** с порогом V_f (см. находку выше) автоматически появятся реалистичные
  ток, удержанное напряжение пика и ненулевая `power = I·V_f` на диоде (сейчас
  P=0, что физически неверно для проводящего диода). Это важный обучающий момент:
  «диод — не идеальный проводник, он ест ~0.7 В».

### [IDEA] Экспоненциальная модель Шокли (Newton-Raphson) как «реальный» слой
**Severity: Medium (идея/будущее)**

- **Проект:** `docs/ELEMENT_LIBRARY.md:24` прямо помечает «The exponential Shockley
  model (I = Is(e^{V/nVt}-1)) is **not** implemented; it needs Newton iterations
  with conductance linearization and is left as a flagged future option».
  Текущий решатель — линейный (Гаусс в `math/LinearSystem.h`), нелинейность
  только через дискретный fixed-point по состояниям.
- **Реальность/источники:** уравнение Шокли `I = I_s·(e^{V/(n·V_T)} − 1)`,
  где `I_s ≈ 1e-12 A` (Si), `n` = 1..2 (ideality factor), `V_T = kT/q ≈ 25.85 мВ`
  при 300 K. SPICE параметрами DC являются IS, N, RS.
  https://en.wikipedia.org/wiki/Shockley_diode_equation
  https://en.wikipedia.org/wiki/Diode_modelling
  https://www.acsu.buffalo.edu/~wie/applet/spice_pndiode/spice_diode_table.html
- **Идеал:** добавить нелинейный слой решателя на Newton-Raphson: на каждой
  итерации линеаризовать диод вокруг текущего V_d — companion `g = dI/dV = I_s/(nV_T)·e^{V/(nV_T)}`,
  `ieq = I(V) − g·V` — и стампить как обычный companion (структура `Companion`
  уже ровно `i = g·V − ieq`, инфраструктура готова). Это превратит проект в
  настоящий мини-SPICE и покажет плавную ВАХ вместо двух состояний.

### [PHYSICS-BUG] Newton/PWL для идеального диода требует gmin / source stepping / damping — иначе chattering и плохая сходимость
**Severity: Medium**

- **Проект:** `CircuitSolver.cpp:152` — жёсткий лимит «24 прохода» fixed-point
  без damping и без gmin-ramp; `kOpenConductance = 1e-12` (`:7`) — это уже,
  по сути, ручной gmin, но фиксированный и не подстраиваемый. При нескольких
  диодах/резком переключении дискретный fixed-point может зациклиться
  (A блокирует → B проводит → A проводит → …), и цикл просто оборвётся на 24-м
  проходе с возможно НЕсогласованным состоянием (молча, без диагностики).
- **Реальность/источники:** для нелинейных/идеальных диодов промышленные
  симуляторы применяют Newton-Raphson + набор «костылей сходимости»: **gmin
  stepping** (большой gmin шунтирует узлы на землю, затем уменьшается шагами),
  **source stepping** (источники с 0 поднимаются до номинала за N шагов) и
  **damping/limiting** шага по напряжению диода. Резкая ВАХ (очень малое R)
  и далёкая начальная точка → осцилляции итераций даже у неосциллирующей цепи.
  https://www.electronicdesign.com/technologies/industrial/boards/article/21774425/taking-a-peek-under-the-hood-of-your-spice-circuit-simulation-engine
  https://ltwiki.org/index.php?title=Convergence_problems%3F
  https://qucs.sourceforge.net/tech/node16.html
- **Идеал:** (а) для текущего дискретного решателя — детектировать незавершённую
  сходимость на 24-м проходе и сообщать/брать последнее «наименее
  противоречивое» состояние, плюс гистерезис на порогах (`-1e-12` / `1e-9`),
  чтобы гасить chattering; (б) при переходе на Newton (находка выше) —
  реализовать gmin stepping, source stepping и Vd-limiting (`pnjlim`-подобный
  clamp шага), как в SPICE.

### [DISCREPANCY] Идеальный ключ как 1e9/1e-12 S — численная аппроксимация, не «истинное переключение топологии»
**Severity: Low**

- **Проект:** `CircuitSolver.cpp:67-68` — ключ есть всегда стоящий в графе элемент
  с проводимостью `1e9` (замкнут) или `1e-12` (разомкнут); топология не меняется.
- **Реальность/источники:** это стандартный и корректный приём — идеальный ключ
  моделируют как очень малое Ron и очень большое Roff (например, Ron ~ 1 мОм,
  Roff ~ 1 МОм), стремясь к Ron→0/Roff→∞; чисто нулевое R на ненулевом ΔV даёт
  бесконечный ток и срывает решатель, поэтому «истинный» short/open берут
  пределами. Рекомендуют держать отношение Roff/Ron велико (> 1e12).
  https://www.typhoon-hil.com/documentation/typhoon-hil-software-manual/concepts/switch_models.html
  https://www.mathworks.com/help/sps/powersys/ref/idealswitch.html
- **Идеал:** подход проекта корректен; стоит лишь зафиксировать это как
  осознанное приближение в `docs/model_assumptions.md` (там сейчас нет раздела
  про диод/ключ вообще) и убедиться, что отношение 1e9/1e-12 = 1e21 не порождает
  плохую обусловленность матрицы при многих узлах (Гаусс в `LinearSystem.h`
  без масштабирования/предобуславливания — потенциальный риск точности).

### [IDEA] Нет AC-источника → невозможно показать выпрямление (главное применение диода)
**Severity: High**

- **Проект:** `VoltageSource` всегда DC (`CircuitSolver.cpp:69-73`, `value` —
  константа); синуса/частоты в коде нет (см. «Источник напряжения» выше).
  Диодные демо — только `DiodeResistor` и `PeakDetector` на DC
  (`DemoCircuits.h:102-108, 173-181`).
- **Реальность/источники:** ключевое применение диода — выпрямление AC: half-wave
  rectifier пропускает положительные полупериоды синуса и режет отрицательные →
  пульсирующий DC; добавление конденсатора-фильтра сглаживает до почти-DC с
  ripple. Без AC-входа этот раздел физики не демонстрируется.
  https://www.electronics-tutorials.ws/diode/diode_5.html
  https://www.allaboutcircuits.com/textbook/semiconductors/chpt-3/peak-detector/
- **Идеал:** добавить AC-режим источнику: поля «amplitude, frequency, phase» и в
  `stepTransient` обновлять мгновенное `value = A·sin(2π f·t + φ)` по `state.time`
  (инфраструктура времени уже есть — `TransientState::time`). Тогда:
  (1) half-wave rectifier (V_ac → диод → R) покажет срезанный синус;
  (2) peak detector (V_ac → диод → C∥R) покажет заряд до пика и медленный спад
  с ripple — то, ради чего пик-детектор и существует (сейчас он тестируется
  скачком DC→0, что не раскрывает физику). Это самый ценный апгрейд для темы.

### [VISUAL-BUG] Проекции диода/ключа не отражают порог и потери; «открыто/закрыто» — статичный лейбл
**Severity: Low**

- **Проект:** диод рисуется треугольником+чертой, физический слой — «generic
  conductor layers (current only when forward)» (`ELEMENT_LIBRARY.md:17`); ключ —
  лейбл «open»/закрытые контакты (`tests/test_diode_switch.cpp:159-161`,
  `ELEMENT_LIBRARY.md:18`). Падение 0.7 В и рассеиваемая мощность на диоде нигде
  визуально не показаны (т.к. в модели их нет — P=0).
- **Реальность/источники:** диод в прямом включении греется (`P = I·V_f`), и это
  важный обучающий факт; в гидравлической аналогии у обратного клапана есть
  «давление открытия» (cracking pressure) — аналог порога V_f.
  https://en.wikipedia.org/wiki/Diode_modelling
- **Идеал:** после введения V_f (находки выше) показать на диоде падение ~0.7 В в
  inspector/тепловом слое (как у резистора), а в гидравлической проекции —
  ненулевое давление открытия клапана. Так визуальный слой станет согласован с
  физикой.

---

## Сводка по severity

- High: 3 (PWL-vs-ideal терминология; отсутствие AC → нет выпрямления; обе влияют
  на главную физику темы).
- Medium: 3 (нулевое падение/потери; Шокли+Newton как слой; сходимость
  gmin/source stepping/damping).
- Low: 2 (ключ как Ron/Roff — приближение корректно, нужна лишь документация;
  визуальный слой без порога/потерь).

Главный вывод: текущая модель диода — это **идеальный диод с нулевым порогом**
(а не PWL и не Шокли), и для темы «нелинейные элементы» не хватает двух вещей —
**ненулевого порога V_f** и **AC-источника** для демонстрации выпрямления. Модель
ключа физически адекватна (Ron/Roff-приближение), но не задокументирована в
`model_assumptions.md`.

---

## Источники

- Shockley diode equation — Wikipedia:
  https://en.wikipedia.org/wiki/Shockley_diode_equation
- Diode modelling (Shockley / PWL / ideal, пороги 0.6–0.7 В) — Wikipedia:
  https://en.wikipedia.org/wiki/Diode_modelling
- Exponential and Piecewise-Linear Analysis in Forward-Conducting Diode Circuits —
  All About Circuits:
  https://www.allaboutcircuits.com/technical-articles/forward-conducting-diodes-exponential-and-piecewise-linear-analysis/
- SPICE diode model parameters (IS, N, RS) — Univ. at Buffalo:
  https://www.acsu.buffalo.edu/~wie/applet/spice_pndiode/spice_diode_table.html
- SPICE Models (Diodes and Rectifiers) — All About Circuits textbook:
  https://www.allaboutcircuits.com/textbook/semiconductors/chpt-3/spice-models/
- Under the hood of a SPICE engine (Newton-Raphson, gmin/source stepping) —
  Electronic Design:
  https://www.electronicdesign.com/technologies/industrial/boards/article/21774425/taking-a-peek-under-the-hood-of-your-spice-circuit-simulation-engine
- Convergence problems (gmin/source stepping, diodes) — LTwiki (LTspice):
  https://ltwiki.org/index.php?title=Convergence_problems%3F
- Non-linear DC Analysis (Newton-Raphson, homotopy) — Qucs tech docs:
  https://qucs.sourceforge.net/tech/node16.html
- Peak detector (диод + конденсатор, удержание пика) — All About Circuits:
  https://www.allaboutcircuits.com/textbook/semiconductors/chpt-3/peak-detector/
- Power Diodes used as Half-wave Rectifiers (выпрямление AC) — Electronics Tutorials:
  https://www.electronics-tutorials.ws/diode/diode_5.html
- Switch models (ideal switch как Ron/Roff) — Typhoon HIL:
  https://www.typhoon-hil.com/documentation/typhoon-hil-software-manual/concepts/switch_models.html
- Ideal Switch (Ron=0/Roff=∞) — MathWorks Simulink:
  https://www.mathworks.com/help/sps/powersys/ref/idealswitch.html
