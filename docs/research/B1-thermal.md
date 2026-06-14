# B1 — Тепловая / электротепловая модель + осциллограф/термометр

Research-агент B1 для electricity-lab. Тема: ТЕПЛОВАЯ МОДЕЛЬ, ЭЛЕКТРОТЕПЛОВАЯ
ОБРАТНАЯ СВЯЗЬ, ОСЦИЛЛОГРАФ И ТЕРМОМЕТР.
Дата: 2026-06-14.

Формат записи: `[PHYSICS-BUG | DISCREPANCY | VISUAL-BUG | IDEA]`, Severity,
«Проект:» (file:line), «Реальность/источники:» (URL/пометка), «Идеал:».

---

## Как сейчас устроено в проекте (ШАГ 1)

### Джоулев нагрев — `src/physics/PowerModel.h`

- `branchPower(current, voltageDrop) = I * dV` (`PowerModel.h:8`).
- `dissipatedPowerOnly(type, power)` возвращает `max(0.0, power)` только для
  `Resistor` и `Wire`; для остальных типов — 0.0 (`PowerModel.h:16-19`).
- `heatFraction(type, power, maxP)` — нормализованная доля для визуального
  heat-свечения (`PowerModel.h:22-26`).
- **Физически корректно:** P = I·dV = I²R = V²/R (закон Джоуля–Ленца, Joule's
  first law). Знак джоулева нагрева всегда положительный (диссипация); источник
  имеет P < 0 (supply) и исключён из `dissipatedPowerOnly`.
- **Тесты:** неявно покрыты в `test_thermal.cpp` через steady-state и монотонность
  разогрева.

### Тепловая модель — `src/physics/ThermalModel.h`

- Сосредоточенная (lumped) RC-модель, **display-only, без обратной связи R(T)**.
- Уравнение: `C_th · dT/dt = P_diss − (T − T_amb) / R_th` — backward Euler
  (`ThermalModel.h:12-16,46-48`).
- `T_new = (a·T_old + P_diss + T_amb/R_th) / (a + 1/R_th)`, где `a = C_th/dt`.
- Состояние: `ThermalState { time; temperature[componentId] }` — зеркало
  `TransientState` (`ThermalModel.h:17-21`).
- `temperatureFor(state, componentId)` → K; если компонента нет —
  `kAmbientTemperature` (`ThermalModel.h:24-27`).
- `celsius(kelvin)` → °C (`ThermalModel.h:29`).
- Интегрируется по **распределённому** решению (`m_distributedCircuit`), поэтому
  каждый сегмент провода — свой тепловой узел; градиент по проводнику возникает
  автоматически.

### Константы — `src/physics/PhysicalUnits.h`

```cpp
kAmbientTemperature = 293.15;  // K (20 °C)
kThermalCapacitance = 1.0;     // J/K  (C_th)
kThermalResistance  = 50.0;    // K/W  (R_th)  → tau = R·C = 50 s
```

- Тепловая постоянная времени τ = 50 с — подобрана педагогически, не
  калибрована («подобраны педагогически», `OSCILLOSCOPE_THERMOMETER.md:149`).

### Осциллограф — `src/simulation/SignalRecorder.h`

- Кольцевой буфер фиксированной длины `kSignalRingSize = 512` на канал
  (`SignalRecorder.h:11`).
- Виды каналов: `NodeV` (напряжение узла), `BranchI` (ток ветви), `ElemT`
  (температура, °C), `ElemP` (мощность, W) (`SignalRecorder.h:18`).
- Значение по `Kind`: для `ElemT` — `celsius(temperatureFor(thermal, ref))`,
  для остальных — поиск по `solution.nodePotentials`/`solution.branches`
  (`SignalRecorder.h:51-90`).
- UI: `ImGui::PlotLines` с авто-масштабом `[lo, hi]`, кольцо разворачивается
  хронологически через `offset` (`MainWindow.cpp:1036-1049`).

### Термометр — `src/ui/MainWindow.cpp`

- Числовой readout: `Тип id: XX.X °C` для выбранного элемента, иначе подсказка
  (`MainWindow.cpp:1013-1019`).
- `elementTemperatureK(originalComponentId)` берёт **максимальную** температуру
  среди распределённых сегментов, принадлежащих исходному компоненту
  (`MainWindow.cpp:493-501`). Это честный максимум для readout.
- Сброс теплового состояния (`m_thermal.reset()`) при всех точках разряда:
  новая цепь, Discharge, загрузка демо, Reset Demo (`MainWindow.cpp:112,825,880,1174,1181`).

### Интеграция в `LiveSim`

- `advanceLiveSim()`: `LiveSim.advance()` → `stepThermal(m_thermal, m_distributedCircuit, m_distributedSolution, dt)` → агрегация температур по исходным id → `m_recorder.sample(m_solution, aggregated, time)` (`MainWindow.cpp:377-389`).
- Аналогично в `stepLiveSimOnce()` (`MainWindow.cpp:392-401`).
- **0 правок в `CircuitSolver` и `LiveSim`** — приборы читают результат, не лезут
  в MNA.

### Тесты

- `tests/test_thermal.cpp` (6 тестов):
  - `TemperatureForFallsBackToAmbientAndResetClearsState`
  - `PoweredResistorHeatsMonotonicallyWithoutOvershoot`
  - `PoweredResistorConvergesToSteadyStateAndSteadyStateIsFixedPoint`
  - `NonDissipatingComponentWithNonzeroBranchPowerStaysAmbient`
  - `TimeAccumulatesAcrossSteps`
  - `CelsiusConvertsKelvinOffset`
- `tests/test_signal_recorder.cpp` (5 тестов): инициализация/очистка каналов,
  хронологическая намотка, wrap-around, корректность значений по Kind,
  отсутствующий ref → 0.

### Визуализация heat-свечения

- `ProjectionBuilder.cpp:191-199`: для `Resistor`/`Wire` в электрической проекции.
- `ProjectionBuilder.cpp:1070-1072`: механическая проекция (brake heat).
- `ProjectionBuilder.cpp:1572-1574`: гидравлическая проекция (friction heat).
- Свечение основано на **мгновенной мощности** (`heatFraction`), а не на
  температуре — см. находку `[VISUAL-BUG]` ниже.

---

## Находки (ШАГ 2 + ШАГ 3)

### [IDEA] Обратная связь R(T): когда R(T) = R₀(1 + αΔT) становится значимой?
**Severity: Low**

- **Проект:** `docs/PHYSICS_AUDIT.md:50` («never feed back into resistance»),
  `src/physics/ThermalModel.h:12` («DISPLAY-ONLY (no R(T) feedback)»).
- **Реальность/источники:**
  - Для меди: α ≈ +0.00393 /K (20°C), для алюминия: +0.00429 /K, для вольфрама
    (лампы): +0.0045 /K. Для углеродных/металлоплёночных резисторов TCR ~ ±50–500
    ppm/K (0.00005–0.0005 /K) — на порядок меньше, чем у чистой меди.
    https://en.wikipedia.org/wiki/Temperature_coefficient
  - При ΔT = 50°C над T_amb (20°C → 70°C): для меди ΔR/R ≈ 50 × 0.00393 ≈ 19.7%
    — **очень заметно**. Для металлоплёночного резистора 100 ppm/K:
    ΔR/R ≈ 50 × 0.0001 = 0.5% — пренебрежимо для учебных целей.
  - В учебных симуляторах (Falstad, PhET CCK, EveryCircuit) R(T) **нигде** не
    моделируется — температура либо не считается вовсе, либо только display-only.
  - **Вывод:** R(T) обратная связь имеет смысл только если в проекте появятся
    медные провода с высоким TCR и большими токами, либо если нужен демо-эффект
    «лампочка накаливания» (вольфрам, α ≈ 0.0045, ΔT до 2500°C — R меняется
    в 12 раз). Для резисторов с низким TCR — **неважно**.
- **Идеал:** Оставить display-only до появления AC/мощных схем или ламп
  накаливания как типа компонента. Тогда добавить флаг `enableThermalFeedback` и
  MNA-перештамповку сопротивления на каждом шаге.

### [PHYSICS-BUG] Тепловые параметры C_th и R_th едины для всех компонентов
**Severity: Medium**

- **Проект:** `src/physics/PhysicalUnits.h:12-13` (одни константы на все
  компоненты), `src/physics/ThermalModel.h:45-49` (одинаковая формула для всех).
- **Реальность/источники:**
  - Теплоёмкость C_th = m·c_p (масса × удельная теплоёмкость). Для SMD-резистора
    0603 масса ~2 мг, c_p алюмооксида ~ 0.8 J/(g·K), C_th ~ 0.0016 J/K. Для
    мощного проволочного резистора массой 10 г — C_th ~ 4 J/K. Разница в 2500×.
  - Тепловое сопротивление R_th зависит от площади поверхности и конвекции.
    Чип-резистор 0603 имеет R_th(ja) ~ 300–500 K/W (junction-to-ambient).
    Мощный резистор в корпусе TO-220 с радиатором — R_th ~ 5–20 K/W.
    Сейчас R_th = 50 K/W — среднее между «голым чипом» и «резистором с небольшим
    радиатором».
    https://en.wikipedia.org/wiki/Thermal_resistance (электротепловая аналогия)
  - **Вывод:** для педагогических целей единые константы допустимы (упрощают
    понимание), но нереалистичны для сравнения «маленький резистор vs большой».
- **Идеал:** Добавить на компонент поля `thermalCapacitanceOverride` и
  `thermalResistanceOverride` (опциональные, 0 = «использовать default»).
  Для провода оценивать C_th пропорционально длине. Дефолтные значения оставить
  педагогическими. Не ломает совместимость.

### [DISCREPANCY] Осциллограф показывает ElemT по исходному componentId, а термометр — max по сегментам
**Severity: Low**

- **Проект:** `src/simulation/SignalRecorder.h:81-82` (канал ElemT: `temperatureFor(thermal, ch.ref)` с ref = originalComponentId),
  `src/ui/MainWindow.cpp:493-501` (термометр: `elementTemperatureK` — max по distributedSource).
- **Реальность/источники:** `docs/OSCILLOSCOPE_THERMOMETER.md:145-148`
  («Если нужно посегментно на осциллографе — завести каналы по distributed-id»).
  Для резистора (не распределяется на сегменты) разницы нет. Для длинного
  провода термометр покажет температуру самого горячего сегмента, а осциллограф
  — температуру «виртуального» исходного компонента (которая равна температуре
  первого сегмента, потому что `temperatureFor` находит запись по id исходного
  компонента, и она пишется в `m_thermal` при `stepThermal` для distributed
  компонентов).
- **Идеал:** При добавлении канала ElemT для провода автоматически создавать
  канал для самого горячего сегмента (или агрегировать max на лету). Либо
  унифицировать: термометр и осциллограф оба берут max по сегментам.

### [VISUAL-BUG] Heat-свечение основано на мгновенной мощности, а не на температуре
**Severity: Low**

- **Проект:** `src/physics/PowerModel.h:22-26` (`heatFraction` = P_diss / maxP),
  `src/projection/ProjectionBuilder.cpp:191-199` (толщина свечения ~ `heatFraction`).
- **Реальность/источники:** Тепловое излучение тела пропорционально T⁴ (закон
  Стефана–Больцмана) и обладает тепловой инерцией (C_th). При включении тока
  свечение должно нарастать с задержкой ~τ, при выключении — спадать плавно.
  Сейчас свечение исчезает мгновенно при P=0, даже если компонент ещё горячий.
- **Идеал:** Заменить `heatFraction` на `temperatureFraction = (T - T_amb) / (T_max - T_amb)`
  в визуализации. Либо смешивать мгновенную мощность и температуру:
  `glow = lerp(heatFraction, tempFraction, 0.5)`. Это сделает визуализацию
  физически честнее и нагляднее.

### [IDEA] Отсутствует «перегорание»/thermal failure компонентов
**Severity: Low**

- **Проект:** Нет ни в `Circuit`, ни в `LiveSim`, ни в `ThermalModel`.
- **Реальность/источники:**
  - EveryCircuit: при превышении мощности компонент «сгорает» (анимация дыма/огня),
    симуляция останавливается.
  - PhET CCK: батарейка «перегревается» и ток падает.
  - Реальные резисторы имеют максимальную рассеиваемую мощность (0.125W, 0.25W,
    0.5W, 1W, ...). При превышении — перегрев, дрейф номинала, разрушение.
  - Типичные максимальные температуры: 70°C для SMD, 155°C для мощных,
    200-250°C для проволочных в стеклоэмали.
- **Идеал:** Добавить `maxPower` в `Component` (опционально, 0 = безлимитный).
  При T > T_max показывать визуальный эффект «перегорания» и переводить компонент
  в разомкнутое состояние (g = kOpenConductance). Студент видит последствия
  теплового пробоя.

### [IDEA] Осциллограф без триггера и таймбазы — v1
**Severity: Low**

- **Проект:** `src/simulation/SignalRecorder.h` (нет полей trigger/ timebase),
  `docs/OSCILLOSCOPE_THERMOMETER.md:143` («Осциллограф без триггера и таймбазы
  (v1) — отдельный TODO»).
- **Реальность/источники:** Настоящий осциллограф имеет: выбор источника
  триггера, уровень триггера (rising/falling edge), горизонтальную развёртку
  (time/div), вертикальную чувствительность (V/div, I/div, °C/div). В учебных
  тулзах: EveryCircuit показывает X-Y и time-domain без триггера, Falstad —
  только анимированную схему без осциллографа.
- **Идеал (v2):** Добавить `triggerChannel`, `triggerLevel`, `triggerEdge`,
  `timePerDiv` в `SignalRecorder`. При включённом триггере запись в кольцо
  происходит только при пересечении порога. В UI — слайдер уровня и выбор
  канала-источника триггера.

### [IDEA] Термометр показывает только выбранный элемент — нет тепловой карты всей схемы
**Severity: Low**

- **Проект:** `src/ui/MainWindow.cpp:1013-1019` (readout одного элемента).
- **Реальность/источники:** Falstad использует цветовое кодирование напряжения
  (зелёный/красный) на всей схеме. Для температуры аналогично можно было бы
  окрашивать компоненты в градиент от синего (T_amb) к красному (max T на схеме).
- **Идеал:** Добавить слой «Thermal map»: при включении все резисторы и провода
  заливаются цветом от температуры (синий→жёлтый→красный). В легенде —
  min/max/current температуры по схеме. Это даст интуитивное понимание «где
  греется» без переключения между элементами.

### [IDEA] Нет термопары / датчика температуры как измерительного прибора на холсте
**Severity: Low**

- **Проект:** Термометр — только текстовый readout в инспекторе.
- **Реальность/источники:** PhET CCK позволяет перетаскивать амперметр/вольтметр
  на схему. Для температуры можно было бы добавить инструмент «термопара»,
  который пользователь размещает на любом компоненте, и он показывает температуру
  прямо на холсте рядом с компонентом.
- **Идеал:** Добавить `EditorMode::PlaceThermometer` — по клику на компонент
  ставится иконка термометра, которая плавает рядом и показывает текущую T. При
  выключении слоя heat — скрывается. Не требует новых ComponentType.

### [IDEA] Параметры R_th и C_th не калиброваны под реальные компоненты
**Severity: Low**

- **Проект:** `src/physics/PhysicalUnits.h:12-13`,
  `docs/OSCILLOSCOPE_THERMOMETER.md:149` («Параметры C_th/R_th подобраны
  педагогически (tau=50 c), не калиброваны»).
- **Реальность/источники:**
  - Для наглядности в учебном процессе τ ~ 1-5 с предпочтительнее, чем 50 с —
    студент не будет ждать минуту, чтобы увидеть установление температуры.
  - EveryCircuit использует ускоренное время (1 с реального ~ 1 мс симуляции).
  - В Falstad время симуляции настраивается слайдером «Simulation Speed».
  - Для демонстрации тепловой инерции достаточно τ ~ 2-10 с.
- **Идеал:** Уменьшить R_th до 5-10 K/W (или C_th до 0.1 J/K) так, чтобы
  τ ≈ 2-5 с. Либо добавить глобальный «thermal speed multiplier» для ускорения
  тепловой динамики относительно электрической (в реальности τ_thermal ≫
  τ_electric, но в симуляции можно сжать).

---

## Источники

1. **Joule heating:** https://en.wikipedia.org/wiki/Joule_heating
   — P = I·dV = I²R = V²/R; Joule's first law; дифференциальная форма J·E.
2. **Temperature coefficient of resistance:** https://en.wikipedia.org/wiki/Temperature_coefficient
   — R(T)=R₀(1+αΔT); Cu α≈0.00393/K; TCR резисторов 50-500 ppm/K.
3. **Thermal resistance (electrothermal analogy):** https://en.wikipedia.org/wiki/Thermal_resistance
   — ΔT = Q̇·R_th; электротепловая RC-аналогия; junction-to-ambient.
4. **Stefan–Boltzmann law (тепловое излучение):** P = εσA(T⁴ − T_amb⁴)
   — учебник (Halliday/Resnick), стандартный курс общей физики.
5. **Falstad Circuit Simulator:** https://www.falstad.com/circuit/
   — анимированная схема, цветовое кодирование напряжения, без тепловой модели.
6. **PhET Circuit Construction Kit:** https://phet.colorado.edu/en/simulations/circuit-construction-kit-dc
   — перетаскиваемые амперметр/вольтметр, без тепловой модели.
7. **EveryCircuit:** https://everycircuit.com/
   — осциллограф с time-domain traces, «перегорание» компонентов при перегрузке.
8. **Исходный код проекта:**
   - `src/physics/ThermalModel.h` — lumped RC thermal model.
   - `src/physics/PowerModel.h` — sign-correct power model.
   - `src/physics/PhysicalUnits.h` — thermal constants.
   - `src/simulation/SignalRecorder.h` — ring buffer signal recorder.
   - `src/ui/MainWindow.cpp` — thermometer/oscilloscope UI.
   - `docs/PHYSICS_AUDIT.md` — physics audit, explicit non-goals.
   - `docs/OSCILLOSCOPE_THERMOMETER.md` — architecture documentation.
   - `tests/test_thermal.cpp` — thermal model tests.
   - `tests/test_signal_recorder.cpp` — signal recorder tests.
