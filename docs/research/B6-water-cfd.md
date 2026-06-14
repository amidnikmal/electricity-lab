# B6: realtime water / CFD research

Дата: 2026-06-14. Агент: Research B6. Область: `electricity-lab`,
гидравлическая проекция и возможная замена визуального слоя воды.

Ограничение задачи: исследование, без правок `src/`.

## Контекст проекта

Проект уже разделяет "честную физику" и "картинку":

- `src/projection/HydraulicMapping.h` задает точную аналогию
  `I -> Q`, `V -> pressure`, `dP * Q == V * I`.
- `src/physics/ParticleSim.*` в `ChannelSpec::connected` строит плотную
  Box2D-сеть воды: трубы, общие узловые камеры, насос-импеллер,
  вентури-горловина резистора, физический проход через junctions.
- `tests/test_water_network.cpp` уже фиксирует инварианты: знак потока,
  сравнимый расход в серии, отсутствие накопления в резисторе, поток от
  насоса без глобального ассиста, удержание шариков внутри горловины.
- `src/physics/HydraulicSim.*` помечен как карантинный PLAN B: один
  упрощенный контур, скорость напрямую от тока, декоративные лопасти.
- `docs/OPENFOAM_WATER_PLAN.md` уже формулирует развилку:
  I - офлайн-CFD картинка плюс честный 1D-солвер; II - live GPU-CFD.

Вывод: текущая вода не CFD, но она уже больше, чем декоративные частицы.
Любая CFD-интеграция должна сохранить MNA/1D-сеть источником истинных `Q`
и `dP`, иначе визуальная вода начнет спорить с решателем цепей.

## Findings

### 1. [DISCREPANCY] OpenFOAM не является realtime-движком для render-loop

Severity: Critical

Проект: `docs/OPENFOAM_WATER_PLAN.md` корректно предупреждает, что OpenFOAM
надо держать вне интерактивной петли. Текущая Water-панель ожидает ответ за
миллисекунды и меняет геометрию/номиналы интерактивно.

Реальность/источники:

- https://openfoam.org/licence/ - OpenFOAM = GPLv3, с наследованием GPLv3 для
  кода, включающего GPLv3 source.
- https://openfoam.org/release/13/ - OpenFOAM 13 описан как open-source CFD
  toolbox с mesh/case/solver workflow, платформы - Ubuntu, WSL, macOS через
  Multipass.
- https://openfoam.org/download/windows/ - официальный путь Windows через WSL2.
- Пометка: по архитектуре OpenFOAM это пакетный FVM toolkit, не API "step one
  frame in 16 ms"; это совпадает с локальным планом.

Идеал: развилка I. Использовать OpenFOAM только офлайн: сгенерировать кейс,
посчитать поля, экспортировать VTK/compact field, воспроизводить в приложении.
Не линковать OpenFOAM в бинарь; граница - отдельный процесс или данные.

### 2. [IDEA] Главная физика воды для схемы - 1D Poiseuille/MNA, не 3D CFD

Severity: Critical

Проект: `HydraulicMapping.h` уже задает `Q = I` и `pressure = V`; тесты
ожидают не SI-скорость частиц, а знак, монотонность, непрерывность и power cue.

Реальность/источники:

- https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu
  - даже graphics fluid solvers явно отделяют упрощенную интерактивную
  визуализацию от инженерной точности.
- https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf -
  Stam прямо ставит real-time graphics выше строгой инженерной точности.
- Пометка: для ламинарной трубы закон Гагена-Пуазёйля дает линейное
  `dP = R_hyd * Q`, что и есть гидравлический аналог `V = R * I`.

Идеал: в развилке I оставить solver/MNA источником `Q` и `dP`; CFD-поле
нормировать так, чтобы интегральный расход через сечение совпадал с `Q`
решателя. В развилке II live-CFD должен быть визуальным слоем, подчиненным
boundary conditions от MNA, а не новым источником токов.

### 3. [VISUAL-BUG] Box2D-вода выглядит физической, но не является CFD

Severity: High

Проект: `ParticleSim` использует плотные диски, контакты, нулевое трение,
thermal agitation, chamber assist, rescue для escapees. Это хорошо закреплено
тестами, но не решает Navier-Stokes, давление, профиль Пуазёйля или вихри.

Реальность/источники:

- https://www.cs.ubc.ca/~rbridson/docs/zhu-siggraph05-sandfluid.pdf - FLIP/PIC
  показывает типичный graphics-путь: частицы для поверхности и advection,
  сетка для incompressibility/boundaries. У проекта сеточного pressure solve нет.
- https://mmacklin.com/pbf_sig_preprint.pdf - PBF решает плотностные
  constraints ради real-time stability, но это все равно graphics fluid,
  с tradeoff incompressibility/performance.

Идеал: развилка I - заменить шарики в Water-виде на линии тока/поле скорости/
давление из забейканных CFD-полей; Box2D оставить fallback/debug. Развилка II -
начинать отдельный GPU-fluid прототип, а не пытаться "допилить Box2D до CFD".

### 4. [PHYSICS-BUG] Live CFD может сломать электрическую честность, если дать ему рулить расходом

Severity: Critical

Проект: сейчас circuit solver владеет током, напряжением и мощностью. Вода
только визуально переносит это. Если live CFD начнет сама определять расход
через насос/резистор, получится второй solver с другой физикой и другими
граничными условиями.

Реальность/источники:

- https://openfoam.org/licence/ и https://openfoam.org/release/13/ - OpenFOAM
  инженерный solver для continuum cases, а не педагогическая аналогия
  электрических цепей.
- https://github.com/ProjectPhysX/FluidX3D - FluidX3D может считать LBM-поля,
  но это не знает ничего о MNA-цепи и `V*I == dP*Q`.

Идеал: для обеих развилок ввести контракт: `mean_flux(field, section) == Q_MNA`,
`pressure_drop(field) == dP_MNA` в пределах выбранной нормировки. Любой CFD
рантайм должен работать как renderer/field generator с MNA boundary conditions.

### 5. [IDEA] Развилка I: OpenFOAM offline field atlas - самый реалистичный путь

Severity: High

Проект: текущие элементы уже имеют каноническую геометрию: pipe, venturi
resistor, pump casing, tank membrane, turbine, junction chamber. Это хорошо
ложится на каталог локальных CFD-патчей.

Реальность/источники:

- https://openfoam.org/release/13/ - OpenFOAM умеет single-phase/multiphase,
  VOF/MULES improvements, ParaView/VTK-oriented visualization workflow.
- https://openfoam.org/download/source/ - source/build workflow через
  репозитории и ThirdParty, то есть это dev/offline dependency.
- `docs/OPENFOAM_WATER_PLAN.md` - уже предлагает A/D: bake fields and export
  cases.

Идеал: начать с прямого участка: OpenFOAM Poiseuille validation -> экспорт поля
-> компактный asset -> renderer streamlines/tracers. Потом вентури-резистор и
узловая камера. Рекомендация: I как основной путь.

### 6. [DISCREPANCY] OpenFOAM GPL требует жесткой интеграционной границы

Severity: High

Проект: у проекта нет явного `LICENSE` в корне на момент чтения файлов, а
OpenFOAM = GPLv3. Линковка к OpenFOAM API создаст сильный лицензионный вопрос.

Реальность/источники:

- https://openfoam.org/licence/ - OpenFOAM Foundation описывает GPLv3 и
  наследование GPLv3 для software including GPLv3 source.
- https://openfoam.org/download/windows/ - официальный runtime на Windows -
  WSL2, что само по себе неудобно для bundled desktop app.

Идеал: не линковать. Для I хранить только результаты/поля и собственный
импортер. Для D запускать `blockMesh`, solver и `foamToVTK` внешним процессом
для power-user/offline workflow.

### 7. [DISCREPANCY] FluidX3D подходит для II технически, но не лицензионно как обычная OSS-зависимость

Severity: Critical

Проект: II подразумевает GPU-CFD realtime. FluidX3D - лучший найденный
кандидат по скорости/картинке, но его нельзя воспринимать как MIT/BSD/GPL
зависимость.

Реальность/источники:

- https://github.com/ProjectPhysX/FluidX3D - LBM CFD на OpenCL для GPU/CPU,
  "Free for non-commercial use".
- https://raw.githubusercontent.com/ProjectPhysX/FluidX3D/master/LICENSE.md -
  license запрещает commercial use, military use и AI training на коде.
- README прямо уточняет, что это source-available no-cost non-commercial, не
  open-source в OSI-смысле.

Идеал: развилка II возможна через FluidX3D только для non-commercial research
или как внешний optional prototype. Для продукта/широкого релиза сначала
решить license/commercial permission. Если license неприемлема, смотреть MIT
SPH/PBF или собственный OpenGL/OpenCL solver.

### 8. [IDEA] FluidX3D - эталон "красивая live GPU-CFD вода"

Severity: High

Проект: если цель пользователя - именно "живая текущая вода", OpenFOAM не
подходит, а FluidX3D ближе всех к нужной картинке: LBM, free surface,
interactive visualization, raytracing.

Реальность/источники:

- https://github.com/ProjectPhysX/FluidX3D - OpenCL, cross-vendor GPU/CPU,
  multi-GPU, topics `cfd`, `fluid-simulation`, `lattice-boltzmann`.
- README FluidX3D: rendering raw simulation data directly in VRAM avoids huge
  volumetric exports and supports interactive rasterization/raytracing.
- README FluidX3D: free-surface extension uses VOF/PLIC and resolves droplets
  down to a few grid cells, but ignores the gas phase.

Идеал: II prototype вне основного `src/`: отдельный sample app or external
process, feeding field texture/snapshots into Water view. Do not start by
rewriting current `ParticleSim` in place.

### 9. [IDEA] DualSPHysics годится как engineering/offline SPH reference, но не как быстрый UI-движок

Severity: Medium

Проект: SPH естественно дает свободную поверхность и частицы, но текущая задача
в схемах - поток в трубах с точным `Q`/`dP`, а не dam-break/wave impact.

Реальность/источники:

- https://github.com/DualSPHysics/DualSPHysics - C++/CUDA/OpenMP SPH solver.
- https://raw.githubusercontent.com/DualSPHysics/DualSPHysics/master/LICENSE -
  LGPL-2.1.
- README DualSPHysics: free-surface phenomena, waves, dam-break impact,
  engineering problems; GPU version требует CUDA stack.

Идеал: использовать DualSPHysics для offline validation/beautiful demos,
не как обязательную runtime-зависимость. Для I - источник сравнительных
SPH-роликов/данных; для II - только если CUDA и LGPL acceptable.

### 10. [IDEA] PySPH удобен для экспериментов, но не для C++/OpenGL runtime

Severity: Medium

Проект: C++ desktop app. Python dependency в render-loop и packaging усложнят
проект больше, чем помогут.

Реальность/источники:

- https://github.com/pypr/pysph - Python framework for SPH.
- https://raw.githubusercontent.com/pypr/pysph/main/LICENSE.txt - BSD-style
  permissive license.

Идеал: держать PySPH как notebook/offline research tool для проверки
параметров, генерации reference cases и картинок. Не встраивать в основной
desktop app.

### 11. [IDEA] SPlisHSPlasH - лучший permissive C++ SPH/PBF кандидат для II

Severity: High

Проект: если II надо делать без source-available/non-commercial риска
FluidX3D, нужен C++ library candidate.

Реальность/источники:

- https://github.com/InteractiveComputerGraphics/SPlisHSPlasH - C++ fluid
  simulation library, SPH, pressure solvers WCSPH/PCISPH/PBF/IISPH/DFSPH/PF,
  CPU/GPU neighbor search, VTK/partio export.
- https://raw.githubusercontent.com/InteractiveComputerGraphics/SPlisHSPlasH/master/LICENSE
  - MIT license.

Идеал: для II рассмотреть SPlisHSPlasH как permissive research base. Минус:
это still SPH graphics/physics library, а не готовая интеграция с MNA и
OpenGL UI; потребуется отдельная архитектура snapshots, GPU/CPU budget,
surface/pipe rendering and coupling.

### 12. [IDEA] PBF/FleX дает game-real-time воду, но это визуальная физика

Severity: Medium

Проект: PBF похож на то, что пользователь ожидает от "реалтайм-воды":
устойчивая интерактивная масса частиц, surface splatting, вихри. Но он не
заменяет электрогидравлический solver.

Реальность/источники:

- https://mmacklin.com/pbf_sig_preprint.pdf - PBF формулирует density
  constraints в PBD и показывает 128k particles около 10 ms/frame в примере
  paper.
- https://github.com/NVIDIAGameWorks/FleX - particle-based simulation library
  for real-time applications; platforms CUDA/DX11/DX12, Windows/Linux.
- Пометка: FleX license надо проверять отдельно перед использованием; repo
  показывает proprietary GameWorks-style package, не обычную MIT/BSD библиотеку.

Идеал: для II использовать PBF-подход как дизайн, не как автоматическую
замену solver truth. Если нужна библиотека, предпочтительнее permissive
SPlisHSPlasH; FleX - только после license review и с учетом CUDA/DX coupling.

### 13. [IDEA] Stable Fluids / GPU Gems / WebGL fluid - дешево и красиво, но это 2D dye/smoke field

Severity: Medium

Проект: C++/OpenGL desktop может быстро получить красивое поле скорости через
FBO/texture ping-pong или compute shaders. Это хорошо для визуального слоя
внутри труб, но не для free-surface CFD и не для инженерного pressure drop.

Реальность/источники:

- https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf -
  Stable Fluids: устойчивый интерактивный Navier-Stokes-style solver with
  numerical dissipation, designed for graphics.
- https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu
  - GPU implementation of Stable Fluids in textures; scope is 2D rectangular
  domain and no free surface.
- https://github.com/PavelDoGreat/WebGL-Fluid-Simulation - browser/mobile
  WebGL fluid, MIT, references GPU Gems.

Идеал: для I это хороший renderer primitive: use MNA `Q` to advect dye/tracers
inside precomputed or procedural fields. Для II это только "toy CFD" baseline,
not final water engine.

### 14. [VISUAL-BUG] Шейдерная вода/ocean примеры не решают поток внутри схемы

Severity: Low

Проект: Water view требует видеть расход по трубам, сужение резистора, насос,
бак-конденсатор, junction chambers. Океанский shader не знает топологии цепи.

Реальность/источники:

- https://raw.githubusercontent.com/mrdoob/three.js/dev/examples/jsm/objects/Water.js
  - reflective flat water effect with normal map, mirror render target and
  distortion.
- https://raw.githubusercontent.com/mrdoob/three.js/dev/LICENSE - three.js MIT.

Идеал: брать идеи для specular/refraction/normal-map polish поверх I/II, но
не считать это CFD. Для проекта полезнее pressure/velocity colormap and
streamlines than ocean waves.

### 15. [DISCREPANCY] Карантинный `HydraulicSim` не должен стать CFD-наследником

Severity: Medium

Проект: `HydraulicSim.h` прямо говорит: "ВАРИАНТ Б", один замкнутый контур,
speed задается напрямую током, насос декоративный, файл надо удалить, если не
понадобится. Он подключен только к tests, чтобы не сгнил.

Реальность/источники:

- Локально: `src/physics/HydraulicSim.h`, `src/physics/HydraulicSim.cpp`.
- Пометка: это project-specific source, не веб.

Идеал: не строить I/II поверх `HydraulicSim`. Для I нужен renderer of fields;
для II - отдельный GPU/fluid subsystem. `HydraulicSim` полезен только как
fallback idea "one loop with fixed particle pool", но не как CFD foundation.

### 16. [PHYSICS-BUG] Junction blending из локальных CFD-патчей может нарушить неразрывность

Severity: High

Проект: канонические OpenFOAM-поля по прямым трубам/вентури легко забейкать,
но схема собирает их в произвольные узлы. На junctions сумма входящих/исходящих
`Q` должна совпадать с MNA, иначе на экране появятся визуальные источники/стоки.

Реальность/источники:

- https://www.cs.ubc.ca/~rbridson/docs/zhu-siggraph05-sandfluid.pdf - hybrid
  particle/grid methods use grids for boundary conditions and incompressibility;
  blending local patches without pressure solve is not automatically
  divergence-free.
- Локально: `tests/test_water_network.cpp` уже ловит накопление и comparable
  flow; аналогичные tests нужны для field renderer.

Идеал: для I начать не с arbitrary junction CFD, а с:
1. per-component atlas,
2. MNA-normalized flux,
3. explicit junction primitive with guaranteed continuity,
4. fallback to current Box2D/procedural flow for unsupported topology.

### 17. [PHYSICS-BUG] Free-surface вода конфликтует с некоторыми элементами аналогии

Severity: Medium

Проект: конденсатор в Water-виде - мембранный бак, DC через него не течет;
индуктор - турбина/инертность; diode - check valve. Свободная поверхность
или "реальная вода" может сделать красивый, но неверный образ для этих
элементов.

Реальность/источники:

- Локально: `ProjectionBuilder.cpp` comments around `emitTank`, `emitTurbine`,
  `emitPump`, and `emitConstriction`.
- https://github.com/ProjectPhysX/FluidX3D - free-surface FluidX3D models
  water-air interface and surface tension, not electrical components.

Идеал: I - CFD fields only where they teach the same invariant: pipe,
resistor/throttle, pump, junction. Keep symbolic components (capacitor
membrane, diode flap, turbine) if they are more honest pedagogically. II -
only after accepting that the app becomes a fluid playground, not just a
circuit analogy.

## Recommendation

Primary recommendation: choose fork I.

Implement "real CFD on screen" as offline OpenFOAM field playback while keeping
the existing 1D/MNA hydraulic truth. This is the only path that is consistent
with current architecture, GPL boundaries, desktop packaging, tests, and the
educational analogy.

Fork II is valid only if the product goal changes to live fluid simulation as
a first-class feature. Then OpenFOAM should be dropped from runtime planning,
and the candidate stack is GPU-CFD/LBM/SPH/PBF:

- FluidX3D for best non-commercial live CFD prototype.
- SPlisHSPlasH for permissive C++ SPH/PBF base.
- Stable-Fluids/OpenGL compute for cheap 2D field visuals.
- DualSPHysics/PySPH for offline research/reference, not UI runtime.

## Suggested next milestones

1. I/F0: one straight pipe Poiseuille OpenFOAM case; compare integrated flux
   and pressure drop to `HydraulicMapping`/MNA.
2. I/F1: import one VTK/field asset and render streamlines/tracers in Water
   without changing solver truth.
3. I/F2: venturi resistor atlas with 3-5 normalized regimes, preserving
   `mean_flux == Q_MNA`.
4. I/F3: junction continuity primitive and tests analogous to
   `WaterNetwork.SeriesVolumeFlowIsConservedThroughResistor`.
5. II/spike only if requested: isolated FluidX3D or SPlisHSPlasH prototype,
   not wired into `src/physics/ParticleSim` yet.

## Источники

- OpenFOAM license: https://openfoam.org/licence/
- OpenFOAM 13 release/platforms/features: https://openfoam.org/release/13/
- OpenFOAM source build workflow: https://openfoam.org/download/source/
- OpenFOAM on Windows/WSL2: https://openfoam.org/download/windows/
- FluidX3D repository/README: https://github.com/ProjectPhysX/FluidX3D
- FluidX3D license: https://raw.githubusercontent.com/ProjectPhysX/FluidX3D/master/LICENSE.md
- DualSPHysics repository: https://github.com/DualSPHysics/DualSPHysics
- DualSPHysics license: https://raw.githubusercontent.com/DualSPHysics/DualSPHysics/master/LICENSE
- PySPH repository: https://github.com/pypr/pysph
- PySPH license: https://raw.githubusercontent.com/pypr/pysph/main/LICENSE.txt
- SPlisHSPlasH repository: https://github.com/InteractiveComputerGraphics/SPlisHSPlasH
- SPlisHSPlasH license: https://raw.githubusercontent.com/InteractiveComputerGraphics/SPlisHSPlasH/master/LICENSE
- Jos Stam, Stable Fluids: https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf
- GPU Gems, Fast Fluid Dynamics Simulation on the GPU: https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu
- Macklin/Muller, Position Based Fluids: https://mmacklin.com/pbf_sig_preprint.pdf
- Zhu/Bridson, Animating Sand as a Fluid / FLIP-style hybrid fluids:
  https://www.cs.ubc.ca/~rbridson/docs/zhu-siggraph05-sandfluid.pdf
- WebGL Fluid Simulation: https://github.com/PavelDoGreat/WebGL-Fluid-Simulation
- Three.js Water shader: https://raw.githubusercontent.com/mrdoob/three.js/dev/examples/jsm/objects/Water.js
- Three.js license: https://raw.githubusercontent.com/mrdoob/three.js/dev/LICENSE
- NVIDIA FleX repository: https://github.com/NVIDIAGameWorks/FleX
