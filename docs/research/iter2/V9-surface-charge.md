# V9 — Поверхностный заряд: перепроверка A5 (итерация 2)

Перепроверка находки A5 из `docs/research/A5-surface-charge-efield.md`.
Дата: 2026-06-14. Принцип: находка ЛОЖНА по умолчанию — опровергаем реальным кодом.

---

## Пункт 1: sigma нормирована на |dV|, а НЕ на |dV|/L → нет физической зависимости от длины/толщины

**ВЕРДИКТ: ПОДТВЕРЖДЕНО (TRUE/PHYSICS-BUG).**

### Доказательство

`src/physics/SurfaceChargeModel.h:40-53`:

```cpp
double dV = vB - vA;
double vAvg = (vA + vB) * 0.5;
double vSwing = std::max(std::abs(dV), 1e-9);   // ← строка 42
// ...
double sigma = (v - vAvg) / vSwing;              // ← строка 53
```

Переменная `len` вычисляется на строке 33 (`double len = ab.length()`), но используется **только** для раннего возврата (строка 34: `if (len <= 1.0...)`) и количества сэмплов (строка 48: `int count = ... len * config.cameraScale / 4.0`). В формуле sigma **длина не участвует**.

`config.wireThickness` используется только для раннего возврата (строка 34: `config.wireThickness * config.cameraScale < 1.0`) и смещения края (строка 44: `edgeOffset = config.wireThickness * 0.5 * 0.92`). В формуле sigma **толщина не участвует**.

sigma всегда ∈ [−0.5, +0.5] для любого провода с ненулевым dV, независимо от его длины и толщины.

### Физическая реальность

По Jackson (AJP 1996, DOI 10.1119/1.18112) и теореме Гаусса: σ_phys ∝ ε₀ · E_normal на поверхности проводника. Для прямого провода E_axial ≈ const = ΔV/L, откуда σ_phys ∝ ε₀ · ΔV/L. Два провода с одинаковым ΔV, но разной длины: короткий имеет бо́льшую физическую σ (круче градиент), длинный — меньшую. Аналогично, толщина провода влияет через теорему Гаусса для цилиндра: σ ∝ E_axial · r.

Эвристика проекта этого не различает.

### Ключевые строки

| Файл | Строка | Содержание |
|------|--------|------------|
| `src/physics/SurfaceChargeModel.h` | 33 | `double len = ab.length()` — вычислена, но не используется в sigma |
| `src/physics/SurfaceChargeModel.h` | 42 | `vSwing = max(|dV|, 1e-9)` — нормировка на dV вместо dV/L |
| `src/physics/SurfaceChargeModel.h` | 53 | `sigma = (v - vAvg) / vSwing` — безразмерная, не зависит от геометрии |
| `src/physics/SurfaceChargeModel.h` | 44 | `edgeOffset = wireThickness * 0.5 * 0.92` — толщина только для позиционирования |
| `src/physics/WirePhysics.h` | 20-23 | `electricFieldMagnitude = |dV|/L` — правильная физика в FieldModel (для сравнения) |

### Источник

Jackson, J. D. (1996). «Surface charges on circuit wires and resistors play three roles.» *AJP*, 64(7), 855–870. DOI: [10.1119/1.18112](https://doi.org/10.1119/1.18112). Wikipedia: [Surface charge](https://en.wikipedia.org/wiki/Surface_charge) — σ = E·ε₀.

---

## Пункт 2: Документация обещает «junction-strength booster», которого в коде НЕТ

**ВЕРДИКТ: ПОДТВЕРЖДЕНО (TRUE/DISCREPANCY).**

### Доказательство

**Документация утверждает наличие booster-а:**

- `src/visualization/VisualizationStatus.h:42-43`:
  ```
  "Edge samples are derived from sigma ~ (V - Vavg) with a junction-strength booster."
  ```
- `docs/PHYSICS_VISUAL_LAYER_STATUS.md:15`:
  ```
  | Surface charge | sigma ~ (V - Vavg) heuristic with junction booster | heuristic | off by default | high |
  ```

**Код явно говорит, что booster УДАЛЁН:**

- `src/physics/SurfaceChargeModel.h:57-58`:
  ```cpp
  // Усиление по 2-й производной убрано: при линейном потенциале
  // лапласиан тождественно 0, блок не давал эффекта.
  ```

**Поиск реализации в src:**
```
grep -r "junction.*boost\|junction-strength\|junctionStrength" src/
→ только VisualizationStatus.h:43 (строка документации), реализация отсутствует.
```

Никакого кода усиления на стыках/изгибах в `SurfaceChargeModel.h` или где-либо ещё в `src/` нет. Стыки и изгибы обрабатываются посегментно, без учёта угла соединения, кривизны или суммы токов в узле.

### Ключевые строки

| Файл | Строка | Содержание |
|------|--------|------------|
| `src/visualization/VisualizationStatus.h` | 43 | «with a junction-strength booster» — обещание |
| `docs/PHYSICS_VISUAL_LAYER_STATUS.md` | 15 | «with junction booster» — обещание |
| `src/physics/SurfaceChargeModel.h` | 57-58 | «Усиление ... убрано» — реальность |

---

## Итог

| Пункт | Суть | Вердикт |
|-------|------|---------|
| 1 | sigma ∝ 1/|dV| вместо 1/|dV/L| — нет зависимости от длины/толщины | **TRUE — PHYSICS-BUG** |
| 2 | junction-strength booster в документации, отсутствует в коде | **TRUE — DISCREPANCY** |

Оба пункта находки A5 подтверждены реальным кодом.
