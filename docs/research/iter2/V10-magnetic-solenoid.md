# V10 — Magnetic/Solenoid (iter2 counter-check)

**Claim A6 reviewed against actual `src/` code.  Date: 2026-06-14.**

---

## Sub-claim 1 — Соленоид/петля отсутствует: поле катушки = сумма зигзаг-сегментов

**VERDICT: ПОДТВЕРЖДЕНО**

### Доказательство

| Что проверялось | Результат | Источник |
|---|---|---|
| Файлы `*Solenoid*`, `*LoopField*`, `*Biot*`, `*Savart*` в `src/physics/` | НЕТ. Единственный файл магнитного поля — `MagneticFieldModel.h` (83 строки). | grep `src/` — 0 совпадений в заголовках/именах файлов |
| `emitMagneticField()` вызывается для КАЖДОГО сегмента | ДА. `ProjectionBuilder.cpp:1967-1969`: внутри цикла по всем сегментам, включая зигзаги индуктора. | `ProjectionBuilder.cpp:1967` |
| `emitInductorPhysics()` — физическое поле соленоида? | НЕТ. Рисует фиолетовый glow-диск (`glows.push_back`), без какой-либо физической модели поля катушки. | `ProjectionBuilder.cpp:414-427` |
| Документация | Честно признаёт: «no full Biot-Savart geometry is solved». | `VisualizationStatus.h:48` |
| Геометрия индуктора | Зигзаг из 4 полукругов (`bumps=4`), каждый аппроксимирован 10 отрезками (`inductorBumpArc`). Каждый отрезок получает отдельный `emitMagneticField()` → сумма полей бесконечных прямых проводов. | `ElementGeometry.h:51-67, 104-117` |

**Источник (как должно быть):** Соленоид: внутри почти однородное поле `B = μ₀NI/l`, снаружи — дипольное. Одиночная петля: на оси `B = μ₀IR² / (2(x²+R²)^(3/2))`.
— [Wikipedia/Solenoid](https://en.wikipedia.org/wiki/Solenoid), [Hyperphysics](http://hyperphysics.phy-astr.gsu.edu/hbase/magnetic/solenoid.html), [Biot–Savart law](https://en.wikipedia.org/wiki/Biot%E2%80%93Savart_law).

---

## Sub-claim 2 — B = μ₀I/(2πr) (бесконечный провод) применён к конечным сегментам

**VERDICT: ПОДТВЕРЖДЕНО**

### Доказательство

| Что проверялось | Результат | Источник |
|---|---|---|
| Формула величины B | `return kMu0 * std::abs(current) / (2.0 * kPi * radius);` — закон Ампера для БЕСКОНЕЧНОГО прямого провода. | `MagneticFieldModel.h:28-32` |
| Вызов формулы | `sampleMagneticField()` (строка 72) вызывает `magneticFieldMagnitude(current, radius)` для каждой точки СЭМПЛИРОВАНИЯ каждого конечного сегмента a→b, независимо от длины сегмента. | `MagneticFieldModel.h:63-75, 34-44` |
| Био–Савар для конечного отрезка | НЕТ нигде в кодовой базе. Формула для конечного отрезка: `B = (μ₀I/4πr)·(sin θ₁ + sin θ₂)`. На конце полубесконечного провода `sin θ₁=1, sin θ₂=0 → B=μ₀I/(4πr)` — вдвое меньше текущей. | grep `src/` на `Biot`, `Savart` — только в документации |

**Источник:** [Biot–Savart law](https://en.wikipedia.org/wiki/Biot%E2%80%93Savart_law), [Hyperphysics — Magnetic Field of Current](http://hyperphysics.phy-astr.gsu.edu/hbase/magnetic/magcur.html).

---

## Sub-claim 3 — Сэмплирование только на 2 фиксированных радиусах (спад 1/r не виден)

**VERDICT: ПОДТВЕРЖДЕНО**

### Доказательство

| Что проверялось | Результат | Источник |
|---|---|---|
| Количество радиусов | `double radii[2] = { wireThickness*0.75, wireThickness*1.15 };` — ровно ДВА. | `MagneticFieldModel.h:53-56` |
| Итерация по радиусам | `for (double radius : radii)` — никакого варьирования, нет опции добавить радиусы. | `MagneticFieldModel.h:63` |
| Расчёт величины B с переданным радиусом | Формула корректна: `μ₀I/(2πr)`. При `r=1.15×WT` и `r=0.75×WT` значения различаются в ~1.53 раза — но визуально это ДВА кольца глифов, а не непрерывный спад. | `MagneticFieldModel.h:31` |
| Визуализация | Нормировка `frac = B/maxMagnitude` на сегмент (строка 644) сглаживает разницу между радиусами. | `ProjectionBuilder.cpp:638-644` |

**Источник:** Принцип: магнитное поле прямого провода спадает как ~1/r. Образовательные визуализации (PhET «Magnet and Compass», MIT TEAL) показывают поле на многих расстояниях. — [Hyperphysics](http://hyperphysics.phy-astr.gsu.edu/hbase/magnetic/magcur.html), [PhET Magnet and Compass](https://phet.colorado.edu/en/simulations/magnet-and-compass).

---

## Итоговый вердикт

| Подпункт | Статус |
|---|---|
| 1. Нет модели соленоида/петли — зигзаг-сегменты + glow | **ПОДТВЕРЖДЕНО** |
| 2. B=μ₀I/(2πr) — бесконечный провод для конечных сегментов | **ПОДТВЕРЖДЕНО** |
| 3. Только 2 фиксированных радиуса, спад 1/r не виден | **ПОДТВЕРЖДЕНО** |

**Находка A6 ПОДТВЕРЖДЕНА ПОЛНОСТЬЮ (3/3 подпункта).**
