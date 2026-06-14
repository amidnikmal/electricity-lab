# V7 — AC-источник отсутствует

**ВЕРДИКТ: НАХОДКА A3 ПОДТВЕРЖДЕНА** — синусоидального/переменного источника в коде нет.

## Доказательство

### 1. ComponentType — нет AcVoltageSource
`src/circuit/Circuit.h:9-18` — перечисление содержит только:
`Wire, Resistor, VoltageSource, Ground, Capacitor, Inductor, Diode, Switch`.
Никакого `AcVoltageSource`, `SineSource`, `FunctionGenerator`.

### 2. VoltageSource — константный DC
`src/solver/CircuitSolver.cpp:73` — `sys.b[vsr] = comp.value;`
Значение `comp.value` штампуется как константа в правую часть СЛАУ. Никакой
зависимости от времени, `sin(…)`, частоты. Ни в одном вызове `solveWithCompanions`
value не пересчитывается по временной функции.

### 3. Единственная динамика — ручной кривошип (crank)
`src/projection/MechanicsMapping.h:61-63` — `emfFromCrankSpeed(double omegaRadPerSec)`
переводит угловую скорость перетаскивания мыши в EMF:
```cpp
double v = 1.5 * omegaRadPerSec;
return std::clamp(v, -12.0, 12.0);
```
Это **не** синусоидальный AC: omega здесь — мгновенная скорость вращения колеса
пользователем, а не электрическая частота. Результат — меняющееся DC-напряжение,
а не знакопеременный синус.

`src/ui/MainWindow.cpp:176-179` — crank-колбэк присваивает `comp->value = emfFromCrankSpeed(omega)`.

### 4. grep: sin/omega/freq/ac/rectif — только механика и геометрия
- `std::sin` — только в геометрических поворотах (ProjectionBuilder, ParticleSim, ChainGeometry).
- `omega` — угловая скорость кривошипа, визуальный спинрейт, символ `\omega` в MathText.
- `freq`/`frequency` — «tooth frequency» в рендере цепи, не электрическая.
- `AC`/`rectif` — ни одного совпадения.

## Источник
Утверждение из КИМ: «AC-источника (синус) НЕТ вообще — выпрямление диодом показать нечем; VoltageSource — константный DC, единственная динамика — ручной кривошип driveSource.»

## Заключение
Код содержит только идеальный константный DC-источник (`VoltageSource`).
Переменного/синусоидального напряжения нет ни в одной точке симуляции.
Диод в схеме работает только при ручном вращении кривошипа (меняющееся DC)
либо при переходных процессах с L/C, но не с AC-выпрямлением.
