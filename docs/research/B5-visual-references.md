# B5: Референсы визуализации — красивое и корректное отображение

Дата: 2026-06-14. Агент: research-B5 (electricity-lab, C++ симулятор цепей).

## Карта находок (issues / ideas / discrepancies)

| # | Тег | Severity | Проект (file:line) | Реальность / источники | Идеал |
|---|-----|----------|---------------------|------------------------|-------|
| 1 | **DISCREPANCY** | HIGH | `src/render/ColorMaps.h:19-32` | `potentialColor()` — кастомная 4-стопная карта: `(49,78,130)→(72,136,170)→(213,170,82)→(211,92,68)`. Это blue→teal→yellow→red, т.е. rainbow-derivative с тремя hue-переходами. Не perceptual: lightness немонотонен, есть false boundaries на стыках hue. | Перцептивно-равномерная sequential colormap: **viridis** (синий→зелёный→жёлтый, монотонный lightness) или **inferno** (чёрный→красный→жёлтый). Обе colourblind-safe, grayscale-safe, lightness линейно возрастает. Источник: BIDS/Matplotlib (van der Walt & Smith, SciPy 2015) — https://bids.github.io/colormap/ |
| 2 | **DISCREPANCY** | MEDIUM | `src/render/ColorMaps.h:34-38` | `currentColor()` — 2-стопный blend: (102,174,216) → (245,118,66). Синий→оранжевый. Lightness не контролируется, нет перцептивной линейности. | Sequential colormap с монотонным lightness, например **magma** или **plasma** для magnitude. Или diverging: **coolwarm** (синий↔красный) если нужна биполярность (±ток). Источник: matplotlib colormaps docs — https://matplotlib.org/stable/users/explain/colors/colormaps.html |
| 3 | **IDEA** | MEDIUM | `src/render/ColorMaps.h:9-38` | Нет абстракции colormap как объекта — только inline-функции с hardcoded стопами. | Ввести `struct Colormap { virtual uint32_t sample(double t) = 0; }`, фабрику `Colormap::viridis()`, `Colormap::inferno()`, чтобы палитру можно было менять без переписывания кода. Хранить 256-элементный LUT для производительности. |
| 4 | **DISCREPANCY** | HIGH | `src/render/PrimitiveRenderer.cpp:137-148` | Glow = 5 концентрических кругов (1 fill + 4 ring) с линейным alpha-спадом. Это НЕ Gaussian blur / bloom. Нет bleeding света за пределы источника, нет HDR-экстракции, нет частотного контроля радиуса. | Двухпроходный Gaussian blur bloom: 1) рендер ярких регионов в offscreen FBO с порогом (brightness > threshold), 2) ping-pong Gaussian blur (horizontal + vertical, 5-10 итераций), 3) additive blend с исходной сценой. Для 2D-ImGui: программный separable Gaussian по одному каналу. Источник: LearnOpenGL Bloom — https://learnopengl.com/Advanced-Lighting/Bloom |
| 5 | **DISCREPANCY** | HIGH | `src/projection/ProjectionBuilder.cpp:54-63,713-773` | `qualitativeFieldAt()` — E = Σ(r·strength/r³) от потенциалов узлов (Coulomb point-source heuristic). `emitFieldBackdrop()` — Euler-интегрированные streamlines с seed'ами вокруг узлов, отрисованные как polylines с halo+core. Это НЕ Line Integral Convolution (LIC). Статус в `PHYSICS_VISUAL_LAYER_STATUS.md` — «heuristic, high risk». | **Line Integral Convolution (LIC)**: Cabral & Leedom (1993). Конволюция white-noise текстуры вдоль field lines. Даёт pixel-density разрешение, показывает ВСЮ топологию поля без зависимости от seed-точек. GPU-параллелизуемо (Fast LIC, Stalling & Hege 1995). OLIC (Wegenkittl & Gröller 1997) добавляет ориентацию. Цвет кодирует magnitude (scalar field → hue, LIC → lightness). Источник: Wikipedia LIC — https://en.wikipedia.org/wiki/Line_integral_convolution, оригинал: Cabral & Leedom, SIGGRAPH '93. |
| 6 | **IDEA** | MEDIUM | `src/projection/ProjectionBuilder.cpp:502-552` | `emitCurrentArrows()` — анимированные стрелки-глифы вдоль проводников. Традиционный glyph-based подход: низкое пространственное разрешение, зависит от spacing и seed-фазы. | **OLIC-подобная кодировка ориентации**: ramp-like асимметричное ядро вдоль streamline создаёт визуальное направление без стрелок. Или **streaklines** с дроплетами (как Falstad: анимированные точки). Falstad circuit sim — https://www.falstad.com/circuit/ — использует движущиеся жёлтые точки для current, зелёный/красный для voltage. |
| 7 | **IDEA** | LOW | `src/render/PrimitiveRenderer.cpp:56-88` | `drawLegend()` — 40-сегментный bar с `potentialColor()`. Нет аннотации perceptually-critical точек (нуль, среднее), нет галочки «равномерный ли масштаб?». | Легенда с: (a) маркерами key values (Vmin, Vmid, Vmax), (b) индикатором perceptual линейности (lightness profile сбоку), (c) опциональным дискретным режимом как у Falstad (green/gray/red). |
| 8 | **IDEA** | LOW | `src/projection/ProjectionBuilder.cpp:630-664` | `emitMagneticField()` — кружки (⊙ out-of-page) и кресты (⊗ into-page) с right-hand rule. Glyph-based, хорош для 2D wire. | Добавить density-based LOD: при zoom-out крупные кружки, при zoom-in больше мелких. Рассмотреть **iron filing** визуализацию (Ounjai et al.) — случайно распределённые частицы, ориентированные по B, дают интуитивный «магнитный узор». |
| 9 | **DISCREPANCY** | MEDIUM | `docs/MIT_TEAL_RENDERING_PLAN.md:23-29` | Документ перечисляет «Still missing»: dedicated legend panel, probe cursor readout, render primitive caching, reference-node switching, typography hierarchy. Эти пункты актуальны и НЕ реализованы. | MIT TEAL (Technology-Enabled Active Learning, John Belcher) использует: (a) colourmaps для scalar potential (не rainbow), (b) vector field lines для E/B, (c) interactive 3D visualizations. Стиль: «educational instrumentation, not decorative VFX». Добавить: legend panel как отдельный render helper, HUD probe readout, кэширование градиентов. |
| 10 | **IDEA** | LOW | Весь `src/render/` + `src/projection/` | Нет визуального «слоя пост-обработки»: всё рисуется напрямую в ImDrawList. Невозможно сделать глобальный bloom, colour-grading, vignette. | Ввести `PostProcessPass`: offscreen FBO → render primitives → Gaussian blur (bloom) → tone mapping → composite. Даже для ImGui это достижимо: рендер в `ImGui::GetBackgroundDrawList()` на image, потом блур средствами CPU/GPU. |

## Источники

1. **Perceptual colormaps (viridis/magma/inferno/plasma)**:
   - BIDS Colormap Project (van der Walt & Smith, 2015): https://bids.github.io/colormap/
   - Matplotlib colormap docs (perceptual uniformity, classes, colourblind): https://matplotlib.org/stable/users/explain/colors/colormaps.html
   - «The Rainbow is Dead… Long Live the Rainbow!» (MyCarta blog series): https://mycarta.wordpress.com/2012/10/06/the-rainbow-is-deadlong-live-the-rainbow-part-3/
   - «Why Should Engineers and Scientists Be Worried About Color?» (IBM Research, Rogowitz & Treinish): https://doi.org/10.1109/VISUAL.1995.480803

2. **Line Integral Convolution (LIC)**:
   - Cabral & Leedom, «Imaging Vector Fields Using Line Integral Convolution», SIGGRAPH '93: https://doi.org/10.1145/166117.166151
   - Stalling & Hege, «Fast and Resolution Independent Line Integral Convolution», SIGGRAPH '95: https://doi.org/10.1145/218380.218448
   - Wikipedia LIC (обзор, варианты OLIC/UFLIC/FLIC): https://en.wikipedia.org/wiki/Line_integral_convolution
   - Wegenkittl & Gröller, «Fast Oriented Line Integral Convolution (FROLIC)», Visualization '97: https://doi.org/10.1109/VISUAL.1997.663897

3. **Glow / Bloom (2D post-processing)**:
   - LearnOpenGL Bloom tutorial (Gaussian blur, two-pass, HDR, tone mapping): https://learnopengl.com/Advanced-Lighting/Bloom
   - Kalogirou, «How to do good Bloom for HDR rendering»: http://kalogirou.net/2006/05/20/how-to-do-good-bloom-for-hdr-rendering/
   - Epic Games / Unreal Engine Bloom (multiple Gaussian curves): https://web.archive.org/web/20190128205221/https://udk-legacy.unrealengine.com/udk/Three/Bloom.html
   - Efficient Gaussian Blur with linear sampling: http://rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/

4. **MIT TEAL / physics education visualization**:
   - MIT TEAL (Technology-Enabled Active Learning) — John Belcher, Physics Department: визуализации EM-полей для обучения. Ссылка устарела (web.mit.edu/viz/EMvisualizations/ → 404), подтверждено по косвенным источникам (Wikipedia TEAL, OCW).
   - Falstad Circuit Simulator (эталонный educational circuit visualizer): https://www.falstad.com/circuit/ — зелёный=positive V, серый=ground, красный=negative V, жёлтые точки=current.

5. **Красивые физические симуляции (галереи)**:
   - Falstad Math & Physics Applets: https://www.falstad.com/mathphysics.html — EM, волны, квантовая механика, цепи.
   - PhET Interactive Simulations (U of Colorado Boulder): https://phet.colorado.edu/ — circuit construction kit, charges and fields.
   - Oimo.js / Box2D physics demos (механика частиц, цепи, жидкости).
   - SciVis / ParaView LIC examples: https://www.paraview.org/Wiki/ParaView/Line_Integral_Convolution
   - Wind map (hint.fm) — real-time LIC визуализация ветра: http://hint.fm/wind/

6. **Проектные документы (контекст)**:
   - `docs/VISUALIZATION_MODEL.md` — pure sample types, layer notes.
   - `docs/VISUALIZATION_PRESETS.md` — preset definitions (Circuit, Potential, Field, Current, Power, Charges, Debug).
   - `docs/MIT_TEAL_RENDERING_PLAN.md` — target style, applied changes, still missing.
   - `docs/PHYSICS_VISUAL_LAYER_STATUS.md` — exact/approx/heuristic/visualization status per layer.

## Резюме (≤10 буллетов + счётчик)

- **3 DISCREPANCY**: (1) potentialColor rainbow-derivative palette, (2) glow = cheap circles not bloom, (3) E-field backdrop = Euler streamlines not LIC.
- **4 IDEA**: (1) viridis/magma perceptual palette with Colormap abstraction, (2) two-pass Gaussian bloom post-process, (3) OLIC-style orientation in current arrows, (4) improved legend with perceptual markers + Falstad discrete mode.
- **1 уже задокументированный gap**: MIT_TEAL_RENDERING_PLAN.md «Still missing» — 5 пунктов, ни один не реализован.
- **Счётчик**: 3 DISCREPANCY + 4 IDEA + 1 DOC-GAP + 2 с код-уровня (currentColor palette, magnetic glyph density) = **10 находок**.
