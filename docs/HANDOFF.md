# Current Lab — Agent Handoff

Date: 2026-06-10 (большой проход по CURRENT_LAB_NEXT_PROMPT.md, все 8 этапов выполнены;
затем UX-фиксы по фидбеку пользователя — см. «UX-проход» ниже)

## Blender-панели (2026-06-10, поздний проход) — 261 тест зелёный

- `src/ui/PaneLayout.h/.cpp`: рекурсивное бинарное дерево панелей (как в Blender):
  split left/right ("| |") и top/bottom ("="), закрытие ("x", кроме последней),
  у каждой панели свой селектор проекции, перетаскиваемые разделители (ratio в узле,
  кламп kPaneMinRatio=0.12). MainWindow: m_paneTree + map paneId→CircuitCanvas
  (создаются/удаляются по дереву, новая панель наследует камеру), пресеты «1/2/3»
  в топ-баре пересоздают дерево. Камеры синхронизируются копированием от изменившейся.
- Источник питания: «+» у плюсового вывода, «−» у минусового, глифы разнесены
  (раньше накладывались в центре). Тест VoltageSourceSymbol.
- Пресеты визуализации снова полностью управляют слоями physics-панели
  (форсирование убрано — оно делало переключение пресетов бессмысленным);
  дефолтный пресет = Current/Drift (kDefaultPreset, анимация из коробки). Тесты Presets.*.
- Ширина панели инструментов считается из самой широкой (переведённой) надписи.
- Тесты: tests/test_pane_layout.cpp (8 шт: split/close/проекции/раскладка без
  перекрытий/кламп ratio/stacked split).

## Вода + формулы + единицы (2026-06-10, ещё позже) — 276 тестов зелёные

- **Гидравлическая проекция** `ProjectionKind::Hydraulic` («Water» в комбо панели):
  `src/projection/HydraulicMapping.h` (I→расход Q, V→давление, ΔP·Q ≡ V·I точно),
  эмиттеры в ProjectionBuilder: трубы с градиентом давления + частицы воды,
  сужение-дроссель (резистор) + нагрев, насос с крыльчаткой (источник),
  бак с уровнем ∝ Vc (конденсатор), турбина (катушка), обратный клапан (диод),
  задвижка (ключ), резервуар (земля). Тесты tests/test_hydraulic.cpp (8 шт).
- **Формулы**: `src/ui/MathText.h` + MathTextParse.cpp (чистый парсер: ^ _ \frac{}{},
  греческие \tau\pi\Omega\mu, символы \cdot ≈ → − и т.д.; тесты test_mathtext.cpp)
  + MathText.cpp (ImGui-рендер: дроби со штрихом, верхние/нижние индексы).
  Формулы уроков переведены в разметку (V_C(t) = V(1−e^{−t/τ}) с настоящей дробью).
- **Единицы как в учебнике**: Ω/kΩ/µF/mF/H/V/mA/mW в подписях проекций и
  инспекторе, через tr() локализуются (Ом/кОм/мкФ/В/мА/мВт...). Шрифт собран
  с диапазонами Cyrillic+Greek+матсимволы (ImFontGlyphRangesBuilder в App.cpp).
- **Тултипы для комбо**: src/ui/UiHelpers.h tooltipIfTruncated() — пресеты,
  режим симуляции, проекция панели; комбо урока растянуто на всю ширину.
- Допереводы: секция ассистента, метрики, статусы, имена семейств задач,
  диод/ключ в редакторе, нижняя полоса.

Осталось из заявок: ничего критичного. Промпты ГЕНЕРИРУЕМЫХ задач — только EN
(формат-строки с числами; нужен отдельный проход по TaskGenerator).

## UX-проход (2026-06-10, после основного прохода) — 253 теста зелёные

- **Element Editor был МОДАЛЬНЫМ** и открывался при каждом выборе элемента, блокируя весь UI
  (корень жалоб «Reset Demo не работает», «переключение режимов не работает»). Теперь это
  обычное окно с крестиком (MainWindow::renderElementEditor).
- **Анимация вне Debug**: дефолтный пресет прятал слои current/drift. Теперь
  `visualization::physicsPaneLayers(preset, multiPane)` (чистая функция, тесты) форсирует
  динамические слои в multi-pane layout.
- **R/unit обновляется живо**: поля провода в редакторе элемента правят
  m_wireResistancePerUnit/m_distributedSegments напрямую с onCircuitChanged (без Apply).
- **RU/EN переключатель**: src/ui/I18n.h/.cpp (`tr()`, словарь EN→RU, включая контент 6 уроков);
  комбо EN/RU в правом конце топ-бара; в App.cpp подгружается DejaVuSans с кириллицей
  (fallback на встроенный шрифт). Промпты генерируемых задач пока только EN (известное ограничение).
- **Тянущиеся сплиттеры**: computeDualViewPaneSplit/computeTripleViewPaneSplit принимают
  ratio (кламп kMinPaneRatio=0.12); InvisibleButton-сплиттеры в renderDualCanvasArea.
- **Уроки-пресеты**: learning::lessonPresetCircuit(family) — каноническая схема урока,
  кнопка «Открыть схему урока» в Learning-панели.
- Тесты на фиксы: tests/test_fixes.cpp (слои панели, сплиттеры, i18n, пресеты уроков).

## Project overview

Интерактивное обучающее приложение для электрических цепей: единая `CircuitModel`, MNA-солвер (DC + transient), три честные проекции одной модели (Circuit / Physics / Spintronics), обучающий модуль на science of learning с AI-resilience предохранителями в коде.

## Quick commands

```bash
cd /home/dima/Desktop/electricity/current-lab
cmake --build build -j$(nproc)     # build app + tests
./build/current-lab-tests          # 240 tests, 40 suites, all green
DISPLAY=:0 ./build/current-lab     # run the app
```

CMake: app sources подхватываются GLOB'ом; тестовые исходники в `TEST_SOURCES` перечислены ЯВНО (новые тесты/чистые .cpp добавлять туда).

## Architecture (после рефакторинга)

```text
CircuitModel (src/circuit/Circuit.h, single source of truth, stable ComponentId)
  -> CircuitSolver (src/solver/) : DC | stepTransient (companion BE/Trapezoidal) | solveTransientSnapshot
       diode states: solveIterative (ideal PWL fixed point)
  -> ProjectionBuilder (src/projection/) : ЕДИНСТВЕННЫЙ источник render-данных
       ProjectionKind::{Schematic, Physics, Spintronics} -> ProjectionResult{RenderPrimitives, ElementState[]}
       ElementGeometry.h (символы C/L), SpintronicsMapping.h (аналогия, P=V*I сохраняется точно)
  -> PrimitiveRenderer (src/render/) : тупой ImGui-бэкенд; RenderPrimitives/ColorMaps — pure data
  -> CanvasInteraction (src/ui/) : edit/hit-test, потребляет InteractionInput (без ImGui, тестируемо)
  -> CircuitCanvas (src/ui/) : тонкая оболочка (камера + клок анимации + сборка ViewParams)
  -> MainWindow : layout Single/Dual/Triple, transient-контролы, DualViewState (3 камеры, sync)
Learning: src/learning/ (TaskGenerator: ground truth из солвера; LearningSession: предохранители 1-6 КОДОМ;
          Lessons.h: арка вывода; AnkiExport: AnkiConnect addNotes, FSRS на стороне Anki)
Assistant: src/assistant/LlmClient.* (OpenAI-compatible, llama.cpp/vLLM/API; критик-не-решатель)
Net: src/net/HttpClient.* (POSIX, plain HTTP, localhost-сервисы)
```

Инвариант: один элемент модели = один ComponentId = N проекций из одной модели+solution. Никаких коллекций элементов на проекцию. Editing/selection/camera — через общую модель и DualViewState.

## Что сделано в этом проходе (этапы промпта)

1. Арх-долг: DualViewProjection вживлён как ProjectionBuilder (реальный путь рендера), CircuitCanvas распилен (1151 -> ~130 строк), слой RenderPrimitives.
2. Transient: companion-модели BE/Trapezoidal (старт трапеции = BE-шаг), режимы DC/Transient, UI Run/Pause/Step/Reset/dt/метод/скорость, снапшот t=0+. Тесты: RC/RL τ, энергия, Tellegen-баланс каждый шаг, устойчивость BE при dt=5τ, сходимость.
3. C+L: полный комплект (модель, солвер, символы, физпроекция: заряды пластин/E в зазоре/энерго-glow, инспектор, редактор, плейсмент).
4. Spintronics: цепь/тормоз/привод/пружина/маховик/якорь/шкивы/храповик/сцепка; маппинг в SpintronicsMapping.h; layout Triple + sync 3 камер.
5. Learning: 6 семейств задач (ground truth из солвера/transient), LearningSession (attempt-first, predict-then-verify, tool-free, reliance-метрики), уроки, Anki-экспорт, панель.
6. Assistant: LlmClient + сократический критик; gating в коде (LearningSession), не в промпте.
7. Diode (ideal PWL + итерация) + Switch (open/closed).
8. Доки: REALTIME_TRANSIENT_MODEL.md, SPINTRONICS_PROJECTION.md, ELEMENT_LIBRARY.md, LEARNING_MODULE.md (эпистемическая таблица), PHYSICS_VISUAL_LAYER_STATUS.md обновлён.

## Known limitations / Recommended next pass

- Нет AC-источника (синус) — без него диод не показывает выпрямление периодического сигнала; добавить `ComponentType::AcVoltageSource` (амплитуда+частота, MNA RHS = f(t) в transient).
- Diode: только идеальная модель; Shockley (Ньютон) — отдельным флагом.
- LLM-вызов блокирующий (UI замирает на время запроса) — вынести в поток.
- extractAssistantReply — таргетный парсер, \uXXXX -> '?'; при желании заменить мини-JSON-парсером.
- HttpClient — plain HTTP (для https нужен локальный прокси); сознательное решение для портативности.
- Учебная панель не навязывает FSRS-расписание tool-free сессий внутри приложения — расписание у Anki.
- Поле/поверхностные заряды остаются эвристикой (см. PHYSICS_VISUAL_LAYER_STATUS.md).
- Приложение визуально не прогонялось в этом проходе (headless-сессия): сборка и 240 тестов зелёные, но стоит запустить `DISPLAY=:0 ./build/current-lab` и глазами проверить triple-layout, transient-анимацию C/L, spintronics-вид, Learning-панель.

## Тесты

240 тестов / 40 сьютов, все зелёные. Чистая логика, без ImGui-рендера (CircuitCanvas.cpp и PrimitiveRenderer.cpp в тестовый таргет не входят). Файлы: test_linear_system, test_circuit, test_solver, test_consistency, test_canvas, test_dual_view, test_projection, test_transient, test_elements, test_spintronics, test_learning, test_diode_switch.
