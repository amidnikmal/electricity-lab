# B2 — Микродинамика частиц (Drude/Box2D-визуализация)

Дата: 2026-06-14. Контекст: electricity-lab, worktree wt-res6.

---

## Находки

### [PHYSICS-BUG] LCG-смещение в pickOutgoing: модуль по младшим битам
**Severity:** Low (визуальный выбор канала, не физика)
**Проект:** `src/physics/ParticleSim.cpp:683-684`
**Реальность/источники:** Wikipedia/LCG — для LCG с модулем-степенью-2 младшие k бит имеют период 2^k; операция `% 10000` читает младшие биты, давая короткопериодическую последовательность. Параметры `pickState * 1664525 + 1013904223` — Numerical Recipes ranqd1, 32-битный LCG.
**Идеал:** Использовать старшие биты: `roll = (pickState >> 16) / 65536.0`, либо `std::mt19937` (уже есть в `TaskGenerator.h`).

### [PHYSICS-BUG] Нулевой ток → частицы застревают в камерах узлов
**Severity:** Medium (вода останавливается некорректно)
**Проект:** `src/physics/ParticleSim.cpp:466-467`
**Реальность/источники:** При `targetSpeed < 1e-9` функция `steerThroughChamber` делает `return` — частица, вошедшая в камеру узла с нулевым током, теряет управление. Контакты могут вытолкнуть, но гарантии нет.
**Идеал:** При нулевом токе chamber assist должен мягко возвращать частицу в её канал (центростремительное поле к оси трубы), либо полагаться на давление контактов + дрейф в flowOwnership без активного руления.

### [DISCREPANCY] Тепловая агитация: изотропное равномерное, а не Максвелл-Больцман
**Severity:** Low (визуализация, не физика; честно заявлена как qualitative Drude)
**Проект:** `src/physics/ParticleSim.cpp:527-538`
**Реальность/источники:** Drude model (Wikipedia): после столкновения распределение скорости электрона определяется локальной температурой — распределение Максвелла-Больцмана (гауссово по компонентам). В коде: `angle = (h % 6283) * 0.001` — равномерное по углу, постоянная амплитуда `kick = mass * 18.0 * kToSim`. Все «тепловые» толчки имеют одинаковую энергию.
**Идеал:** Гауссово распределение амплитуды (Box-Muller из двух LCG-выборок или `std::normal_distribution` + `std::mt19937`). Для детерминизма можно использовать тот же LCG-хеш с преобразованием Бокса-Мюллера: `sqrt(-2*ln(u1)) * cos(2π*u2)`.

### [DISCREPANCY] DriftModel.h: «тепловое» движение — чистый детерминированный sin/cos
**Severity:** Low (этот код — запасной путь; основной — Box2D ParticleSim)
**Проект:** `src/physics/DriftModel.h:110-115`
**Реальность/источники:** `thermalX = sin(time*117.3 + seed*7.1)*2.8 + cos(...)` — периодическая функция, не случайный процесс. При этом `hasThermalMotion = true`.
**Идеал:** Заменить на стохастический thermal jitter (даже простой LCG + sin достаточно для визуального шума) или удалить поле `hasThermalMotion` как вводящее в заблуждение.

### [IDEA] Box2D избыточен для учебной Drude-визуализации
**Severity:** — (архитектурное решение, не баг)
**Проект:** `src/physics/ParticleSim.cpp` (весь файл, 930 строк)
**Реальность/источники:** Falstad circuit simulator (falstad.com/circuit) визуализирует ток «движущимися жёлтыми точками» — чистая анимация вдоль пути, без физического движка. PhET Circuit Construction Kit (phet.colorado.edu) использует анимированные сферы вдоль проводов — тоже без физики столкновений. Box2D manual (box2d.org): «A 2D physics engine for games» — tuned for MKS, moving objects 0.1–10m, оптимизирован под «large piles of bodies». Частицы в проекте: ~1.2 world units → 0.06m в симуляции — на границе допустимого (Box2D: «keep moving objects larger than 1cm»). Стоимость: 120 Hz substeps × 10 velocity iterations × 6 position iterations на каждый мир — два мира (электроны + вода).
**Идеал:** Рассмотреть облегчённую альтернативу для электронного мира: path-based animation (как ChainSim, но с thermal jitter). Box2D оставить ТОЛЬКО для воды/механики, где контактное давление и несжимаемость действительно нужны. Для учебных целей Falstad-стиль достаточен и на порядок дешевле.

### [IDEA] Отсутствует пулинг частиц: bodies создаются/удаляются на каждом configure()
**Severity:** — (производительность при частой смене схемы)
**Проект:** `src/physics/ParticleSim.cpp:228-253`
**Реальность/источники:** При вызове `configure()` (смена геометрии, слайдер толщины провода) все b2Body удаляются через `world.reset()` и создаются заново. Box2D v3 manual: «Fast memory management plays a central role» — но аллокация сотен тел через `CreateBody` всё равно дороже переноса существующих. Стандартный паттерн: пул предсозданных тел, `SetTransform` + `SetLinearVelocity` вместо `CreateBody`/`DestroyBody`.
**Идеал:** Выделить N_max тел при первом configure, при повторном — переместить существующие на новые позиции, лишние усыпить (`SetEnabled(false)`), недостающие активировать.

### [VISUAL-BUG] ChainSim.h в physics/, но Box2D удалён — путаница в API
**Severity:** Low (документация/именование)
**Проект:** `src/physics/ChainSim.cpp:7-11`
**Реальность/источники:** Комментарий: «The earlier Box2D body ring... was stepped and then every transform was overwritten by the guided positions anyway — pure CPU waste at 120 Hz; it is gone.» Цепь теперь чисто математическая (oval racetrack + phase). Но файл лежит в `physics/` и называется `Sim`, создавая впечатление симуляции.
**Идеал:** Переименовать в `ChainAnimator` или `ChainPath`, перенести в `animation/`. Или явно документировать в заголовке: «guided animation, not physics simulation».

### [IDEA] «Мёртвый» код: sampleDriftParticles — запасной путь при активном Box2D
**Severity:** — (поддерживаемость)
**Проект:** `src/physics/DriftModel.h:72-135`, `src/projection/ProjectionBuilder.cpp:597-599`
**Реальность/источники:** `sampleDriftParticles` используется как fallback когда Box2D-мир не содержит частиц для данного компонента. Основной путь: `ParticleSim::particles()` → Box2D. Два кодовых пути делают одно и то же (визуализация дрейфа) с разной механикой.
**Идеал:** Унифицировать: либо всегда Box2D (и тогда удалить `sampleDriftParticles`), либо оставить fallback явным с комментарием «deprecated, remove when Box2D covers all cases».

### [IDEA] HydraulicSim (вариант Б) в карантине — судьба не решена
**Severity:** — (технический долг)
**Проект:** `src/physics/HydraulicSim.h:10-31`, `src/physics/HydraulicSim.cpp:1-3`
**Реальность/источники:** Комментарий: «в приложение не подключён. Карантин.» Подключён только к тестам. Критерий удаления: «если к ~сентябрю 2026 так и не понадобился — удалить целиком».
**Идеал:** Запланировать решение на сентябрь 2026 согласно документу.

### [PHYSICS-BUG] Радиус частиц не обновляется при движении слайдера толщины провода
**Severity:** Medium (известный баг, задокументирован)
**Проект:** `src/physics/ParticleSim.cpp:863-868` (setTargets) + `docs/HANDOFF.md:446-449`
**Реальность/источники:** «слайдер толщины провода НЕ обновляет радиус частиц при пересборке мира: MainWindow передаёт particleRadius только в ПЕРВЫЙ configure, а setTargets реконфигурит со старым m_impl->particleRadius — контракт «шар == коллайдер» ломается после слайдера.»
**Идеал:** Либо запретить реконфигурацию в setTargets при изменении радиуса, либо передавать particleRadius в setTargets и использовать при авто-реконфигурации.

---

## ## Источники

| Источник | URL / пометка |
|---|---|
| Drude model | https://en.wikipedia.org/wiki/Drude_model — уравнение движения, время релаксации τ, распределение Максвелла-Больцмана после столкновений |
| LCG (linear congruential generator) | https://en.wikipedia.org/wiki/Linear_congruential_generator — проблема младших бит, спектральный тест; Numerical Recipes ranqd1: a=1664525, c=1013904223 |
| Box2D manual v3.1 | https://box2d.org/documentation/ — MKS units, moving objects 0.1–10m, «keep moving objects larger than 1cm», data-oriented design, optimized for large piles |
| Box2D README (GitHub) | https://github.com/erincatto/box2d — features: continuous collision, island sleep, extensive multithreading/SIMD |
| Falstad Circuit Simulator | https://www.falstad.com/circuit/ — «moving yellow dots indicate current», чистая path-based анимация |
| Falstad Electrodynamics | https://www.falstad.com/emwave2/ — 2D EM simulation, educational |
| PhET Circuit Construction Kit | https://phet.colorado.edu/en/simulations/circuit-construction-kit-dc — анимированные сферы, без физики столкновений |
| PHYSICS_VISUAL_LAYER_STATUS.md | /docs/PHYSICS_VISUAL_LAYER_STATUS.md — статус всех слоёв: drift = visualization (sign exact) |
| HANDOFF.md | /docs/HANDOFF.md — план particle-движка, известные баги, история фиксов |
| VISUALIZATION_MODEL.md | /docs/VISUALIZATION_MODEL.md — pure sample types, layer notes |
| Код ParticleSim.cpp/h | /src/physics/ — Box2D-микродинамика: стены, столбики, лопасти, steering, flowOwnership |
| Код DriftModel.h | /src/physics/ — старый fallback: детерминированные sin/cos частицы |
| Код ChainSim.cpp/h | /src/physics/ — guided animation, Box2D удалён |
| Код HydraulicSim.cpp/h | /src/physics/ — вариант Б, карантин |
