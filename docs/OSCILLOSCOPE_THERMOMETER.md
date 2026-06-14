# Oscilloscope & Thermometer

Дата: 2026-06-14. Ветка: `feat/scope-thermo`. Статус: 491 тест зелёный, приложение
и тесты собираются. Многоагентная разработка (оркестратор + Kilo/DeepSeek + Codex).

## Что это

Два инструмента-«потребителя» уже посчитанного решения цепи, без новой электрики:

- **Термометр** — числовой readout температуры выбранного элемента (°C),
  посчитанной односторонней тепловой RC-моделью от рассеиваемой мощности.
- **Осциллограф** — панель с графиками (`ImGui::PlotLines`) по закреплённым
  каналам: напряжение узла (V), ток ветви (I), температура (T) или мощность (P)
  элемента. Кольцевой буфер фиксированной длины, авто-масштаб по окну.

Вольтметр и амперметр **намеренно не добавлены как сущности** — V и I уже видны
в секции «Probe Readout» правого инспектора. Это и есть показания прибора.

## Архитектура (что куда тапает)

```
LiveSim.advance()  ──►  m_distributedSolution (посегментно)
        │                      │
        │              mapDistributedSolution()  ──►  m_solution (по исходным id)
        │                      │
        ▼                      ▼
  stepThermal(m_thermal, m_distributedCircuit, m_distributedSolution, dt)   ← тепло
        │
        ▼
  aggregated[origId] = elementTemperatureK(origId)   (max по сегментам)
        │
        ▼
  m_recorder.sample(m_solution, aggregated, time)    ← запись сигналов
```

Ключевой принцип: **0 правок в `CircuitSolver` и ядре `LiveSim`**. Приборы читают
результат `advance()`, не лезут внутрь матрицы MNA.

### Тепловая модель — `src/physics/ThermalModel.h` (header-only)

Сосредоточенная (lumped) RC-модель, **display-only, без обратной связи R(T)**.
Зеркалит `TransientState`: состояние по `componentId`, шаг тем же `dt`, что и
электрический переходный процесс.

```
C_th · dT/dt = P_diss − (T − T_amb) / R_th        (backward Euler)
```

- `P_diss = dissipatedPowerOnly(type, branch.power)` — переиспользует существующую
  модель мощности (ненулевое только для `Resistor`/`Wire`).
- Backward Euler (A-устойчив, монотонен):
  `T_new = (a·T_old + P + T_amb/R_th) / (a + 1/R_th)`, где `a = C_th/dt`.
- **Устойчивая точка:** `T = T_amb + P_diss · R_th`.
- Интегрируется по **распределённому** решению — каждый сегмент провода свой
  тепловой узел, градиент по проводнику выходит сам. Readout элемента берёт самый
  горячий сегмент (`elementTemperatureK` = max по `distributedSource`).

API:
```cpp
struct ThermalState { double time; std::unordered_map<int,double> temperature; void reset(); };
double temperatureFor(const ThermalState&, int componentId);   // K, T_amb если нет
double celsius(double kelvin);                                  // K → °C
void   stepThermal(ThermalState&, const Circuit&, const CircuitSolution&, double dt);
```

Константы в `src/physics/PhysicalUnits.h`:
```cpp
kAmbientTemperature = 293.15;  // K (20 °C)
kThermalCapacitance = 1.0;     // J/K  (C_th)
kThermalResistance  = 50.0;    // K/W  (R_th)  →  tau = R·C = 50 s
```

### Запись сигналов — `src/simulation/SignalRecorder.h` (header-only)

Чистая логика, без ImGui. Кольцевой буфер на канал.

```cpp
constexpr int kSignalRingSize = 512;
struct SignalChannel {
    enum class Kind { NodeV, BranchI, ElemT, ElemP };
    std::string label; Kind kind; int ref;
    float ring[kSignalRingSize]; int head; int count;
};
class SignalRecorder {
    int  addChannel(label, kind, ref);   // → index
    void removeChannel(int); void clear();
    int  channelCount() const; const SignalChannel& channel(int) const;
    std::vector<SignalChannel>& channels();
    void sample(const CircuitSolution&, const ThermalState&, double time);
};
```

Семантика кольца: `head` — индекс следующей записи; `count` насыщается до
`kSignalRingSize`. Значение канала по `Kind`:

| Kind     | источник                                   | значение          |
|----------|--------------------------------------------|-------------------|
| `NodeV`  | `solution.nodePotentials` (nodeId==ref)    | потенциал, V      |
| `BranchI`| `solution.branches` (componentId==ref)     | ток, A            |
| `ElemP`  | `solution.branches` (componentId==ref)     | мощность, W       |
| `ElemT`  | `celsius(temperatureFor(thermal, ref))`    | температура, °C    |

Для `PlotLines` передавать `(ring, count, offset)`, где
`offset = count < kSignalRingSize ? 0 : head` — так кольцо разворачивается
хронологически.

### UI — `src/ui/MainWindow.{h,cpp}`

Две новые секции в **правом инспекторе**, сразу после «Probe Readout» (никаких
новых окон):

- **Термометр:** `Тип id: XX.X °C` для выбранного элемента; иначе подсказка.
- **Осциллограф:** кнопки «Пин I/T/P» (для элемента) и «Пин V» (для узла)
  закрепляют канал; «Очистить» сбрасывает все; каждый канал рисуется
  авто-масштабированным `PlotLines` с подписью текущего значения и кнопкой
  «Убрать».

Все строки — через `tr()`; RU-переводы добавлены в `src/ui/I18n.cpp` (а значит
попадают в атлас шрифта через `allUiText()`).

Сброс теплового состояния (`m_thermal.reset()`) добавлен во все точки разряда
(`m_liveSim.discharge()`): новая цепь / Discharge / загрузка демо / Reset Demo.

## Тесты

- `tests/test_thermal.cpp` (6): устойчивая точка, монотонный разогрев без
  перелёта, фикс-точка steady-state, недиссипативный элемент остаётся при
  T_amb, накопление `time`, `celsius()`.
- `tests/test_signal_recorder.cpp` (5): инициализация/очистка каналов,
  хронологическая намотка ниже ёмкости, wrap-around с сохранением последних N,
  корректность значения по каждому `Kind`, отсутствующий ref → 0.

Запуск: `cmake --build build --target current-lab-tests && ./build/current-lab-tests`.

## Жёсткие ограничения (соблюдены)

- 0 правок в `solver/CircuitSolver` и `simulation/LiveSim` (ядро).
- Нет новых `ComponentType`, нет приборов на холсте.
- Нет обратной связи R(T) — температура строго односторонняя.
- Heat-свечение не дублируется — термометр это только число.

## Известные ограничения / TODO v2

- Осциллограф без триггера и таймбазы (v1) — отдельный TODO.
- `ElemT`-канал через generic `temperatureFor`: для **провода** буфер берёт
  значение по исходному id (термометр-readout при этом честно агрегирует
  сегменты через `elementTemperatureK`). Если нужно посегментно на осциллографе —
  завести каналы по distributed-id.
- Параметры `C_th/R_th` подобраны педагогически (tau=50 c), не калиброваны.
