# Current Lab

Интерактивное C++ / OpenGL / Dear ImGui приложение для изучения тока, потенциала,
напряжённости поля, дрейфа электронов и рассеяния энергии в простых цепях.

Проект ориентирован на учебную физическую честность:

- solver даёт схемное DC-решение через MNA, плюс переходные процессы (transient)
  для конденсаторов и катушек (companion-модели, Backward Euler / трапеции);
- distributed wire даёт 1D-приближение конечного сопротивления провода;
- визуальные слои помечены статусами `exact-sign`, `approximation`, `educational`,
  `heuristic`, `qualitative` (см. `src/visualization/VisualizationStatus.h`);
- renderer больше не придумывает E-field / drift / surface charge / B-field
  прямо внутри UI-логики: эти слои вынесены в чистые модели в `src/physics/`.

## Быстрый старт

```bash
cd current-lab
cmake -S . -B build
cmake --build build -j$(nproc)
./build/current-lab-tests
DISPLAY=:0 ./build/current-lab
```

На Windows проект собирается через Ninja + MinGW (UCRT):
`cmake -S . -B build -G Ninja && cmake --build build -j`.

## Возможности

- Редактор цепей: узел, провод, резистор, источник напряжения, земля,
  конденсатор, катушка, диод, ключ
- DC-решатель установившегося режима: модифицированный узловой анализ (MNA)
- Переходные процессы RC/RL: companion-модели C/L, Backward Euler / трапеции,
  конденсатор хранит заряд Q = C·V
- Диод: кусочно-линейная модель с порогом ~0.7 В (не идеальный «нулевой» порог)
- Режим distributed wire: настраиваемые `segments` и `R / unit`
- Градиент потенциала вдоль проводников (перцептивная палитра viridis)
- Стрелки тока со знаково-корректным током ветви
- Слой E-field: `E ≈ -dV/dx` вдоль проводов
- Частицы дрейфа: скорость ∝ |I| (ноль при I=0), с явной пометкой, что
  визуальная скорость усилена
- Слой поверхностного заряда (эвристика): плотность ∝ градиенту |dV|/L
- Слой магнитного поля: Био–Савар для конечного отрезка, видимый спад ~1/r,
  модель соленоида (внутри однородное поле, снаружи диполь); static DC
- Слои тепла и мощности (знак: рассеяние vs выдача)
- Тепловая модель (сосредоточенная RC), осциллограф и термометр как потребители
  посчитанного решения (`src/simulation/SignalRecorder.h`, `src/physics/ThermalModel.h`)
- Двойная/тройная проекция: схема ↔ физика ↔ механика, плюс вид «вода» (гидроаналогия)
- Инспектор: `Va`, `Vb`, `dV`, `I`, `P`, `Length`, distributed `R`, `E`, статус слоёв
- Выбор reference-узла прямо в UI
- Пресеты визуализации:
  - вид «Схема»
  - вид «Потенциал»
  - вид «E-field»
  - вид «Дрейф электронов»
  - вид «Мощность/тепло»
  - вид «Поверхностный заряд»
  - полный учебный оверлей
- Управление анимацией: пауза, слайдер скорости, сброс времени
- Учебный модуль (predict-then-verify, attempt-first, critic-not-solver,
  экспорт в Anki/FSRS)

## Физический статус слоёв

| Слой | Статус | Примечания |
|---|---|---|
| Ток / напряжение / знак мощности из solver | `exact-sign` | Из решения MNA |
| Потенциал | `approximation` | Интерполяция вдоль 1D distributed-модели провода |
| Электрическое поле | `approximation` | `E ≈ -dV/dx` вдоль каждого проводника |
| Дрейф | `educational` | Направление сохранено, скорость ∝ \|I\|, усилена для наглядности |
| Поверхностный заряд | `heuristic` | Плотность ∝ градиенту \|dV\|/L (теорема Гаусса), знаковый узор |
| Магнитное поле | `qualitative static DC` | Био–Савар отрезка, спад ~1/r, модель соленоида |
| Тепло | `approximation` | Сосредоточенная RC; readout — самый горячий сегмент |

## Архитектура

Текущий поток данных:

```text
Circuit
  -> Circuit::toDistributed(...)
  -> CircuitSolver  (DC + transient companion-модели)
  -> physics/* чистые модели визуализации
  -> CircuitCanvas renderer
  -> MainWindow / InspectorPanel
```

Ключевые файлы:

```text
src/
  circuit/Circuit.h
  circuit/CircuitValidator.h
  solver/CircuitSolver.h/.cpp
  simulation/LiveSim.h/.cpp        (единая live-модель: DC как предел transient)
  physics/
    PhysicalUnits.h
    WirePhysics.h
    FieldModel.h
    DriftModel.h
    SurfaceChargeModel.h
    MagneticFieldModel.h
    SolenoidModel.h
    ThermalModel.h
    PowerModel.h
  render/ColorMaps.h               (viridis/magma LUT)
  visualization/VisualizationStatus.h
  ui/
    MainWindow.h/.cpp
    CircuitCanvas.h/.cpp
    InspectorPanel.h/.cpp
```

Важные изменения второго прохода:

- непрерывность `node.id` и `component.id` не предполагается (не `id == индекс`);
- distributed-wire маппинг источников использует оригинальные ID компонентов,
  а не смещения в векторе;
- `CircuitCanvas` потребляет чистые сэмплы моделей для слоёв поля, дрейфа,
  магнитного поля и поверхностного заряда;
- инспектор показывает статус моделей и физические величины по элементам.

## Тесты

Набор тестов покрывает:

- операции над графом цепи и стабильность ID;
- корректность solver, знаковые конвенции и поведение distributed wire;
- проверки согласованности (KCL / KVL / баланс мощности / Tellegen);
- переходные процессы (transient C/L);
- состояние и геометрию канваса;
- поведение чистых моделей визуализации (поле, дрейф, магнитное поле,
  поверхностный заряд, палитры);
- валидацию цепи (`CircuitValidator`).

## Известные ограничения

- Переходные процессы RC/RL реализованы (companion-модели, Backward Euler /
  трапеции); однако **AC-источника (синусоидального) пока нет**, а диод —
  кусочно-линейный (без модели Шокли);
- интегратор по умолчанию — Backward Euler (демпфирует LC-колебания);
- нет полного решения Максвелла / Лапласа / Пуассона для поля;
- поверхностный заряд остаётся эвристикой;
- магнитное поле — локальный учебный оверлей, не полный 3D-расчёт;
- линейный solver не возвращает статус сингулярности/обусловленности
  (`CircuitValidator` ловит грубые случаи: нет земли, плавающие узлы,
  конфликт источников), но **не подключён к solve-пути / UI**;
- эволюция температуры — упрощённая сосредоточенная RC, без R(T);
- save/load и undo/redo пока отсутствуют.

Актуальный статус ресёрч-бэклога (что сделано / частично / нет, с пруфами в коде):
**[docs/RESEARCH_BACKLOG_STATUS_2026-06-15.md](docs/RESEARCH_BACKLOG_STATUS_2026-06-15.md)**.

## Документация

- [docs/HANDOFF.md](docs/HANDOFF.md)
- [docs/RESEARCH_BACKLOG_STATUS_2026-06-15.md](docs/RESEARCH_BACKLOG_STATUS_2026-06-15.md)
- [docs/model_assumptions.md](docs/model_assumptions.md)
- [docs/electricity_model_notes.md](docs/electricity_model_notes.md)
- [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md)
- [docs/PHYSICS_AUDIT.md](docs/PHYSICS_AUDIT.md)
- [docs/VISUALIZATION_MODEL.md](docs/VISUALIZATION_MODEL.md)
- [docs/TEST_PLAN.md](docs/TEST_PLAN.md)
- [docs/MIT_TEAL_RENDERING_PLAN.md](docs/MIT_TEAL_RENDERING_PLAN.md)
- [docs/SECOND_PASS_REPORT.md](docs/SECOND_PASS_REPORT.md)
