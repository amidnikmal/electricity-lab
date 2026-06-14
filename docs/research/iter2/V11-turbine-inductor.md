# V11: Турбина как аналог индуктора — перепроверка (итерация 2)

**Дата:** 2026-06-14
**Worktree:** wt-v11
**Проверяющий:** orchestrator (ручная перепроверка кода)

---

## ВЕРДИКТ: НАХОДКА ПОДТВЕРЖДЕНА (TRUE)

Находка A7 истинна по обоим пунктам: (1) турбина крутится пропорционально току всегда, включая DC steady-state; (2) семантика «турбина» (отбор энергии) расходится с семантикой «индуктор» (запасание энергии).

---

## Доказательство

### 1. Турбина крутится пропорционально току ВСЕГДА

`src/projection/ProjectionBuilder.cpp:1725-1732`:
```cpp
double flow = hydraulic::flowFromCurrent(current);           // line 1725
double angle0 = spinPhase(ctx, comp.id, flow, kVisualSpinRate); // line 1732
```

`src/projection/ProjectionBuilder.cpp:847-851` (`spinPhase`):
```cpp
double spinPhase(const BuildContext& ctx, int componentId, double current, double rate) {
    if (ctx.p.flowIntegrals)
        return componentIntegral(ctx.p.flowIntegrals, componentId) * rate;
    return ctx.p.time * current * rate;  // <-- rotation ∝ current, NOT dI/dt
}
```

Вращение (`angle0`) зависит **только от тока**, не от производной dI/dt. При ненулевом токе турбина вращается, даже если dI/dt = 0.

### 2. Индуктор в DC steady-state = короткое замыкание (V=0)

`src/solver/CircuitSolver.cpp:6,180-181`:
```cpp
static constexpr double kWireConductance = 1e9;             // line 6
// ...
else if (comp.type == ComponentType::Inductor)
    companions[comp.id] = {kWireConductance, 0.0}; // short circuit in DC  // line 180-181
```

В DC steady-state проводимость индуктора = `1e9` См ≈ короткое замыкание. Напряжение V=0, но ток через индуктор отличен от нуля — турбина всё равно крутится.

### 3. Семантическое расхождение: turbineEnergy = ½LI² (запасание), а не отбор

`src/projection/HydraulicMapping.h:57-59`:
```cpp
inline double turbineEnergy(double inductance, double flow) {
    return 0.5 * inductance * flow * flow; // == 1/2 L I^2
}
```

Формула `½ L I²` — это энергия **запасаемая** в магнитном поле индуктора. Но:
- Реальная **турбина** — устройство, **извлекающее** энергию из потока (нагрузка), не запасающее её.
- Стандартный гидравлический аналог индуктивности по Wikipedia — **инертанс** (fluid inertance): `L_hyd = ρ·L/A`, энергия — кинетическая энергия потока `½ L_hyd·Q²`.
- Wikipedia предлагает аналог: «a rotary vane pump with a **heavy rotor**, or a turbine placed in the current. The **mass** of the rotor and the surface area of the vanes restricts the water's ability to rapidly change its rate of flow due to the effects of **inertia**.»
- Ключевое понятие — **инерция** массивного ротора, а не лопастная турбина, отбирающая мощность.

### 4. Итоговая таблица

| Утверждение | Статус | Источник (file:line) |
|---|---|---|
| Турбина крутится ∝ току всегда | Подтверждено | `ProjectionBuilder.cpp:850` (`time * current * rate`) |
| Индуктор = КЗ в DC (V=0) | Подтверждено | `CircuitSolver.cpp:181` (`kWireConductance = 1e9`) |
| turbineEnergy = ½LI² (запасание) | Подтверждено | `HydraulicMapping.h:57-58` |
| Турбина ≠ индуктор семантически | Подтверждено | Wikipedia: hydraulic analogy — «heavy rotor», не turbine |

---

## Источник

Wikipedia [Hydraulic analogy](https://en.wikipedia.org/wiki/Hydraulic_analogy): «Inductor: a rotary vane pump with a **heavy rotor**, or a turbine placed in the current. The **mass** of the rotor and the surface area of the vanes restricts the water's ability to rapidly change its rate of flow due to the effects of **inertia**.»
