# V8 — Дрейф: проверка A4 (итерация 2)

Дата: 2026-06-14
Проверяемая находка: [A4-drift-drude.md](../A4-drift-drude.md)

## ВЕРДИКТ: ПОДТВЕРЖДЕНО (все три утверждения substantiated)

---

### (1) Ненулевой дрейф при I=0 — **ПОДТВЕРЖДЕНО**

**Файл:** `src/physics/DriftModel.h:91`

```cpp
double driftSpeed = (0.06 + absI * 0.25) * std::max(0.0, config.visualSpeedMultiplier);
```

При `absI = 0`: `driftSpeed = (0.06 + 0) * multiplier = 0.06 * multiplier > 0`.

При `visualSpeedMultiplier` по умолчанию = 18.0 (из `PhysicalUnits.h:7`), при I=0 скорость дрейфа = `0.06 * 18.0 = 1.08` world units/с — ненулевая, частицы движутся.

**Контракт «sign exact»** (`docs/PHYSICS_VISUAL_LAYER_STATUS.md:17`): «Drift particles | solver current direction/sign; amplified speed + thermal jitter | visualization (sign exact)». Ненулевой дрейф при I=0 означает, что частицы имеют направленное смещение там, где направленного тока нет — нарушение контракта.

**Источник:** Drift velocity formula: `v_d = I/(nAe)`. При I = 0 → v_d = 0. https://en.wikipedia.org/wiki/Drift_velocity

---

### (2) Произвольные коэффициенты вместо физической формулы — **ПОДТВЕРЖДЕНО**

**Файл:** `src/physics/DriftModel.h:91`

Формула: `driftSpeed = (0.06 + absI * 0.25) * multiplier`

Коэффициенты 0.06 и 0.25 — **произвольные**, не привязаны к:
- концентрации носителей n (~8.5×10²⁸ м⁻³ для Cu)
- площади сечения A (из `wireThickness`)
- заряду электрона e = 1.602×10⁻¹⁹ Кл

Физическая дрейфовая скорость: `v_d = I / (n · A · e)`.
Для меди, провода ∅1 мм, I = 1 А → v_d ≈ 9.3×10⁻⁵ м/с.
Текущий `absI * 0.25 * 18.0 = 4.5` world units/с — произволен.

**Источник:** Drude model, drift velocity: https://en.wikipedia.org/wiki/Drude_model

---

### (3) Тепловой «шум» — детерминированные периодические функции — **ПОДТВЕРЖДЕНО**

**Файл:** `src/physics/DriftModel.h:110-118`

```cpp
double thx = std::sin(config.time * 117.3 + seed * 7.1) * 2.8
           + std::cos(config.time * 89.7 + seed * 11.3) * 2.2
           + std::sin(config.time * 143.1 + seed * 3.7) * 1.5;
double thy = std::cos(config.time * 103.7 + seed * 13.1) * 2.8
           + std::sin(config.time * 127.9 + seed * 5.3) * 2.2
           + std::cos(config.time * 77.1 + seed * 17.3) * 1.5;
```

Шесть детерминированных синусоидальных/косинусоидальных членов с фиксированными частотами (77.1–143.1) и амплитудами (1.5–2.8). Ни одного стохастического элемента — траектории воспроизводимы для одинаковых seed/time.

Реальное тепловое движение — случайный процесс: `v_th ≈ √(3kT/m_e) ≈ 1.17×10⁵ м/с` при 300 K. Отношение v_d/v_th ~ 10⁻¹¹. Синтетический шум приемлем как визуальный эвристический, но должен документироваться как «synthetic noise for reproducibility», а не «thermal motion».

**Источник:** https://en.wikipedia.org/wiki/Drift_velocity (thermal velocity), https://en.wikipedia.org/wiki/Fermi_energy

---

## Итог

Все три утверждения A4 substantiated кодом:
- `driftSpeed != 0` при I=0 (`DriftModel.h:91`)
- Формула произвольна, не `v_d = I/(nAe)` (`DriftModel.h:91`)
- «Тепловой шум» — периодические sin/cos, не стохастический (`DriftModel.h:110-118`)
