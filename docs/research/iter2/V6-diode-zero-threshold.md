# V6 — Диод: нулевой порог (Находка A3 ПОДТВЕРЖДЕНА)

**Вердикт:** НАХОДКА ПОДТВЕРЖДЕНА — диод действительно идеальный с нулевым порогом, НЕ PWL.

## Доказательства

### 1. Companion ieq всегда ноль
`src/solver/CircuitSolver.cpp:154`:
```cpp
companions[id] = {on ? kWireConductance : kOpenConductance, 0.0};
```
Companion-источник `ieq` жёстко равен `0.0` в любом состоянии. Настоящая PWL-модель имела бы `ieq = g * V_f` в проводящем состоянии (смещение на порог).

### 2. Нет V_f нигде в коде
Grep по `src/` на `V_f|Vf|ForwardDrop|diode.*threshold` — 0 совпадений. Нет параметра порогового напряжения ни в `Component`, ни в `CircuitSolver`, ни в тестах.

### 3. Проводящий диод = чистый short (~0 В падения)
`src/solver/CircuitSolver.cpp:154`: проводящее состояние — `kWireConductance = 1e9 S` (≈1 нОм), без ЭДС-смещения. Это «идеальный ключ по знаку», а не PWL.

### 4. Тест требует нулевого падения
`tests/test_diode_switch.cpp:44-45`:
```cpp
EXPECT_NEAR(res->current, 5.0 / 1000.0, 1e-6);     // полный ток по закону Ома
EXPECT_NEAR(diode->voltageDrop, 0.0, 1e-6);         // падение ровно 0
```
Ток без вычитания V_f, падение напряжения ровно 0 — поведение идеального диода.

### 5. Критерии переключения — только знак, не порог
`src/solver/CircuitSolver.cpp:162-167`:
- Проводящий диод с `current < -1e-12` → блокирует (только знак)
- Блокирующий с `voltageDrop > 1e-9` → проводит (только знак)
Нет сравнения с V_f.

### 6. Комментарий в Circuit.h вводит в заблуждение
`src/circuit/Circuit.h:16`: «`ideal piecewise-linear`» — на самом деле реализована модель **«ideal diode»** (нулевой порог), а не PWL.

### 7. Заголовок решателя описывает модель точно
`src/solver/CircuitSolver.h:69-71`: «`ideal-diode state iteration: each diode is either conducting (short) or blocking (open)`» — это корректное описание идеального диода.

## Источники

- Уравнение Шокли: `I = I_s * (exp(V/(n*V_T)) - 1)`, где `V_T ≈ 25.85 мВ` при 300 K. Идеальный диод — предел `n → 0`, дающий нулевое прямое падение.
  https://en.wikipedia.org/wiki/Shockley_diode_equation
- PWL-модель диода: «идеальный диод + источник напряжения V_γ + R_s». Порог для Si ≈ 0.6–0.7 В.
  https://en.wikipedia.org/wiki/Diode_modelling
- SPICE diode parameters: IS (ток насыщения), N (коэффициент идеальности), RS (последовательное сопротивление).
  https://www.acsu.buffalo.edu/~wie/applet/spice_pndiode/spice_diode_table.html
