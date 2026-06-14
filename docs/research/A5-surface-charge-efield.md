# A5 — Поверхностный заряд и электрическое поле

Research-агент A5, electricity-lab (C++ симулятор цепей).
Тема: ПОВЕРХНОСТНЫЙ ЗАРЯД и E-ПОЛЕ.
Дата: 2026-06-14.

Формат записи: `[PHYSICS-BUG | DISCREPANCY | VISUAL-BUG | IDEA]`, Severity,
«Проект:» (file:line), «Реальность/источники:» (URL или пометка),
«Идеал:».

---

## ШАГ 1: Как устроено в коде

### SurfaceChargeModel — эвристика `sigma ∝ (V − Vavg) / swing`

Файл `src/physics/SurfaceChargeModel.h:23-71`. Функция `sampleSurfaceCharges`:

- Для сегмента провода a→b с потенциалами vA, vB:
  - `dV = vB − vA`, `vAvg = (vA + vB) / 2`, `vSwing = max(|dV|, 1e−9)`
  - `sigma = (v − vAvg) / vSwing` — нормированная безразмерная величина, где `v = linearPotentialAt(vA, vB, t)` — линейная интерполяция (`WirePhysics.h:25`)
- Для линейного потенциала sigma ∈ [−0.5, +0.5] всегда, независимо от длины провода, толщины, материала
- Положительный sigma → визуально красные кружки (положительный заряд); отрицательный → синие
- `displayStrength = min(1.0, |sigma| * 1.2)` — только для размера визуального кружка
- Сэмплы на верхней/нижней кромках (`edgeOffset = wireThickness * 0.5 * 0.92`)
- Максимум 64 сэмпла на сегмент; `|sigma| < 0.05` пропускаются
- **Комментарий в коде (стр. 57-58):** «Усиление по 2-й производной убрано: при линейном потенциале лапласиан тождественно 0, блок не давал эффекта.» — junction booster удалён

### FieldModel — E-поле вдоль проводника

Файл `src/physics/FieldModel.h:22-80`. `sampleFieldArrows`:
- `fieldDirection(a, b, vA, vB)`: направление от высокого V к низкому (по conventional current)
- `electricFieldMagnitude(voltageDrop, length) = |ΔV| / L` — физичная 1D оценка
- Стрелки размещаются вдоль проводника, при достаточной ширине — в несколько рядов

### E-field backdrop (streamlines)

`ProjectionBuilder.cpp:666-774`, `emitFieldBackdrop`:
- Точечные источники поля в узлах схемы: `q ∝ (V_node − V_mid) / range`
- `qualitativeFieldAt(p, sources)`: поле как суперпозиция `E ∝ Σ q_i * r_i / r³` (с softening 450 px²)
- Стримлайны трассируются от положительных источников к отрицательным
- Полностью независим от SurfaceChargeModel; не использует геометрию проводников

### Связь слоёв

- SurfaceChargeModel и FieldModel — **развязанные** эвристики
- Оба вызываются из `ProjectionBuilder.cpp:1939-1964` независимо
- Никакой перекрёстной валидации: нет проверки `div E ↔ σ` или `E_normal ↔ ε₀·σ`
- Статус в `PHYSICS_VISUAL_LAYER_STATUS.md:14`: heuristic, Risk: high, выключен по умолчанию
- `VisualizationStatus.h:43`: упоминает «junction-strength booster», **которого в коде нет** — расхождение документации и реализации

---

## ШАГ 2 + ШАГ 3: Находки

### [PHYSICS-BUG] Нормировка sigma на |dV| вместо |dV|/L: нефизичная зависимость от длины
**Severity: High / confidence: high**

- **Проект:** `src/physics/SurfaceChargeModel.h:42-53`. `swing = max(|dV|, 1e−9)`. sigma всегда ∈ [−0.5, +0.5], длина провода не влияет.
- **Реальность/источники:**
  - Физическая поверхностная плотность заряда σ_phys [Кл/м²] на цилиндрическом проводнике при DC: σ ∝ ε₀ · E_normal на поверхности. Для прямого провода E_axial ≈ const = ΔV/L, откуда σ_phys ∝ ε₀ · ΔV/L (из знаний — Jackson 1996; теорема Гаусса).
  - Два провода разной длины с одинаковым ΔV: короткий имеет большую физическую σ (круче градиент), длинный — меньшую. Эвристика этого не различает.
  - Толщина провода тоже влияет: по теореме Гаусса для цилиндра σ ∝ E_axial · r. Проект использует `wireThickness` только для позиционирования сэмплов, но не в расчёте sigma.
  - URL: Jackson, J. D. (1996). «Surface charges on circuit wires and resistors play three roles.» *AJP*, 64(7), 855–870. https://doi.org/10.1119/1.18112 — CONFIRMED
- **Идеал:** заменить `swing = |dV|` на `swing = |dV| / (L * r_factor)`, где L — длина сегмента, r_factor — нормированный радиус. Минимум: `swing = |dV| / L` (уже даст зависимость от длины). Тогда sigma станет пропорциональна градиенту потенциала — физически корректно.

### [PHYSICS-BUG] Junction/bend physics отсутствует — нет накопления заряда на изгибах и стыках
**Severity: High / confidence: high**

- **Проект:** `src/physics/SurfaceChargeModel.h:57-58` — комментарий: «Усиление по 2-й производной убрано». `VisualizationStatus.h:43` упоминает «junction-strength booster», но в коде блок удалён. Стыки/изгибы обрабатываются посегментно, без учёта геометрии соединения.
- **Реальность/источники:**
  - На изгибах провода поверхностные заряды скапливаются, чтобы «повернуть» электрическое поле вслед за геометрией. Без этого E-поле внутри не следовало бы за изгибом (Jackson 1996: «role 3 — assure the confined flow of current»).
  - На внутренней стороне изгиба плотность заряда ВЫШЕ, на внешней — НИЖЕ. Это создаёт поперечную компоненту поля, заставляющую E поворачивать (из знаний — Preyer 2000; Sherwood & Chabay 1999).
  - В узлах (3+ проводов) распределение заряда определяется KCL и непрерывностью потенциала; возможны области повышенной плотности.
  - URL: Preyer, N. W. (2000). «Surface charges and fields of simple circuits.» *AJP*, 68(10), 1002–1006. https://doi.org/10.1119/1.1286113 — CONFIRMED (Google Scholar)
- **Идеал:** для изогнутых проводов добавить curvature-dependent boost: `sigma_boost ∝ угол_изгиба`, с асимметрией (внутренняя сторона > внешняя). Для узлов — учитывать сумму токов и градиентов потенциала по всем входящим ветвям. Минимум: добавить в документацию явное предупреждение об отсутствии bend/junction physics.

### [DISCREPANCY] Документация утверждает наличие «junction booster», но код его не содержит
**Severity: Medium / confidence: high**

- **Проект:** `src/visualization/VisualizationStatus.h:43` — «Edge samples are derived from sigma ~ (V - Vavg) with a junction-strength booster.» `docs/PHYSICS_VISUAL_LAYER_STATUS.md:14` — «sigma ~ (V − Vavg) heuristic with junction booster». Но `src/physics/SurfaceChargeModel.h:57-58`: комментарий явно говорит, что booster удалён.
- **Реальность/источники:** код — единственный источник истины.
- **Идеал:** привести документацию в соответствие с кодом: убрать упоминание «junction booster» из `VisualizationStatus.h` и `PHYSICS_VISUAL_LAYER_STATUS.md`, либо реализовать booster в коде. Сейчас — документация вводит в заблуждение.

### [DISCREPANCY] Surface charge и E-field — развязанные слои без физической согласованности
**Severity: Medium / confidence: high**

- **Проект:** `SurfaceChargeModel.h` и `FieldModel.h` — полностью независимые эвристики. Никакой связи σ ↔ E. `emitEFieldArrows` (стр. 554) и `emitSurfaceCharge` (стр. 602) вызываются раздельно в `ProjectionBuilder.cpp:1931-1941`.
- **Реальность/источники:**
  - Физически поверхностные заряды **создают** поле внутри проводника. Связь: σ = ε₀ · E_normal на поверхности проводника (Wikipedia: https://en.wikipedia.org/wiki/Surface_charge — CONFIRMED).
  - Для прямого провода: E_axial = const = ΔV/L → σ(z) ∝ z — линейный градиент. E-field слой даёт E = −dV/dx ≈ const (правильно). Surface charge слой даёт sigma ∝ (V − Vavg) / |dV| (знак правильный, величина не масштабирована). Численного согласования нет.
  - URL: https://en.wikipedia.org/wiki/Surface_charge — «σ = E·ε₀» для проводников.
- **Идеал:** добавить перекрёстный тест/assert: для прямого провода знак градиента sigma должен совпадать с направлением E-strelk. Как минимум — документировать, что согласованность не гарантируется.

### [PHYSICS-BUG] E-field backdrop: точечные источники от узлов, а не поле от поверхностного заряда
**Severity: Medium / confidence: high**

- **Проект:** `ProjectionBuilder.cpp:666-774`. `qualitativeFieldAt` (стр. 54) — E ∝ Σ q_i * r_i / r³ от точечных зарядов в узлах. Величина заряда: `q_i ∝ (V_node − V_mid) / range`.
- **Реальность/источники:**
  - Физическое E-поле вне проводника: вблизи поверхности — перпендикулярно (из-за поверхностных зарядов); вдали — поле распределённого диполя вдоль провода; существует компонента вдоль провода (вектор Пойнтинга S = E×B/μ₀). (из знаний — Jackson 1996; Preyer 2000).
  - Точечные источники в узлах дают радиальные линии → поле расходится от каждого узла, а не «огибает» провод от + поверхностных зарядов к −.
  - Пример: цепь батарея–резистор. Правильное E-поле снаружи: от положительно заряженной стороны резистора к отрицательной. Эвристика даст: радиальные линии от + узла батареи (смешано с резистором).
  - URL: Preyer 2000 (численный расчёт полей для простых цепей) — CONFIRMED.
- **Идеал:** заменить point-source источники на поле от сегментированных линейных зарядов (каждый сегмент провода с sigma ≠ 0 — источник). Это согласует backdrop с SurfaceChargeModel. Минимум: tooltip/предупреждение о качественном характере backdrop.

### [PHYSICS-BUG] Линейная интерполяция потенциала некорректна для конденсаторов и нелинейных элементов
**Severity: Low / confidence: high**

- **Проект:** `SurfaceChargeModel.h:52` — `linearPotentialAt(vA, vB, t)`. Для конденсатора vA ≈ vB на каждой обкладке → dV ≈ 0, sigma ≈ 0 везде. Но физически на обкладках конденсатора накоплен заряд! Проект рисует capacitor charge отдельным кодом, но `emitSurfaceCharge` всё равно вызывается (`ProjectionBuilder.cpp:1963-1964`), создавая нулевые сэмплы.
- **Реальность/источники:**
  - Для омических элементов (провода, резисторы) линейный потенциал корректен.
  - Для конденсатора: потенциал постоянен на обкладке, но заряд ненулевой и определяется C·Vc.
  - Для нелинейных элементов (диод, транзистор — в перспективе) профиль потенциала нелинеен.
- **Идеал:** не вызывать `sampleSurfaceCharges` для конденсаторов (или проверять dV на ноль и пропускать). Либо унифицировать: capacitor charge = surface charge на обкладках с sigma = C·Vc / A_plate.

### [IDEA] Знак «conventional current» vs электроны: педагогическая путаница
**Severity: Low / confidence: high**

- **Проект:** Знак sigma привязан к conventional current (от + к −). Положительные кружки на высоком V создают поле вдоль conventional current — физически корректно. Но drift-частицы анимируются как электроны (против поля).
- **Реальность/источники:**
  - Поверхностные заряды в DC **статичны**. Движутся электроны — в противоположном направлении. Студент видит «+» на одном конце и может подумать, что положительные заряды движутся, создавая ток.
  - `electricity_model_notes.md:21-25`: «Реальное движение электронов направлено от − к +, противоположно conventional current.»
- **Идеал:** добавить tooltip: «Surface charges are static; conventional current flows from + to − surface charges despite electrons drifting the other way.» Или переключатель electron/conventional для слоя.

### [IDEA] Квазистатическое приближение — допустимо, но не задокументировано
**Severity: Low / confidence: medium**

- **Проект:** `sampleSurfaceCharges` использует мгновенные vA, vB. В transient — пересчёт каждый кадр.
- **Реальность/источники:**
  - Время установления поверхностного заряда: τ_relax = ε₀/σ_conductivity ∼ 10⁻¹⁹ с для меди + время распространения сигнала L/c ∼ нс (из знаний — Feynman Lectures II-5; Jackson).
  - Для dt симуляции ∼ мкс и более — квазистатическое приближение отлично работает: заряды «подстраиваются» мгновенно относительно шага.
  - На частотах ∼ ГГц, где длина волны ∼ размер цепи, приближение ломается — проект этого диапазона не достигает.
- **Идеал:** зафиксировать в `PHYSICS_VISUAL_LAYER_STATUS.md`: «Surface charge is quasi-static — valid when circuit length ≪ wavelength / dt ≫ charge relaxation time.»

### [IDEA] Отсутствует educational overlay: волна установления заряда при замыкании ключа
**Severity: N/A / confidence: medium**

- **Проект:** не реализовано.
- **Реальность/источники:**
  - В педагогической литературе (Härtel; Sherwood & Chabay) подчёркивается: момент замыкания — ключевой для понимания роли поверхностных зарядов.
  - Анимация «волны» sigma, распространяющейся от батареи вдоль проводов со скоростью ∼0.7c, — мощный педагогический инструмент.
  - URL: Härtel, H. (2021). «Voltage and Surface Charges.» *EJPE*, 12(3). https://eric.ed.gov/?id=EJ1321475 — CONFIRMED (Google Scholar)
- **Идеал:** в будущем — анимированная волна установления заряда при замыкании ключа (качественная, без точного решения волнового уравнения).

### [IDEA] Унификация capacitor plate charge и wire surface charge
**Severity: Low / confidence: medium**

- **Проект:** Capacitor charge рисуется отдельным кодом. Surface charge для конденсатора вызывается, но даёт sigma ≈ 0.
- **Реальность/источники:**
  - Физически заряд на обкладках конденсатора — тот же поверхностный заряд, что и на проводах. Разница в величине: у конденсатора большая площадь обкладок.
  - Унификация дала бы консистентную картину: распределённый поверхностный заряд по всем проводникам, с повышенной плотностью на обкладках.
- **Идеал:** единый surface charge слой для всей цепи: на проводах sigma мала (пропорциональна градиенту), на обкладках конденсатора sigma велика (пропорциональна C·Vc/A).

---

## Сводка по категориям

| Категория | Количество |
|-----------|-----------|
| PHYSICS-BUG | 3 (нормировка на dV вместо dV/L, junction/bend, backdrop point-source) |
| DISCREPANCY | 2 (docs vs код про junction booster, развязка charge↔field) |
| VISUAL-BUG | 0 |
| IDEA | 4 (conventional vs electron sign, квазистатика, educational overlay, унификация capacitor) |

**Итого: 9 находок.** Критических (High): 2 — нормировка sigma на |dV| без учёта длины, отсутствие junction/bend physics.

---

## Источники

1. **Код проекта:**
   - `src/physics/SurfaceChargeModel.h` — эвристика sigma ∝ (V−Vavg)/swing
   - `src/physics/WirePhysics.h` — `linearPotentialAt`, `electricFieldMagnitude`
   - `src/physics/FieldModel.h` — E-поле вдоль проводника (−dV/dx)
   - `src/projection/ProjectionBuilder.cpp:49-63` — `qualitativeFieldAt`, точечные источники
   - `src/projection/ProjectionBuilder.cpp:602-628` — `emitSurfaceCharge` → визуализация
   - `src/projection/ProjectionBuilder.cpp:666-774` — `emitFieldBackdrop` → стримлайны
   - `src/projection/ProjectionBuilder.cpp:1931-1964` — диспетчеризация слоёв
   - `src/visualization/VisualizationStatus.h:42-44` — «junction-strength booster» (устарело)
   - `docs/PHYSICS_VISUAL_LAYER_STATUS.md` — статусы слоёв
   - `docs/VISUALIZATION_MODEL.md` — модель визуализации
   - `docs/electricity_model_notes.md` — конспект физики (секции 22-24: поверхностный заряд)

2. **Jackson, J. D.** (1996). «Surface charges on circuit wires and resistors play three roles.» *American Journal of Physics*, 64(7), 855–870.
   - Три роли поверхностных зарядов: (1) поддержание потенциала, (2) E-поле снаружи, (3) направление тока вдоль изгибов.
   - URL: https://doi.org/10.1119/1.18112 — CONFIRMED (AJP, Google Scholar: 119 цитирований)

3. **Preyer, N. W.** (2000). «Surface charges and fields of simple circuits.» *American Journal of Physics*, 68(10), 1002–1006.
   - Численный расчёт распределения поверхностного заряда для цепей с изгибами; визуализация Poynting vector.
   - URL: https://doi.org/10.1119/1.1286113 — CONFIRMED (Google Scholar: 67 цитирований)

4. **Härtel, H.** (2021). «Voltage and Surface Charges.» *European Journal of Physics Education*, 12(3).
   - Связь напряжения и поверхностных зарядов; распределение зарядов на изгибах; экспериментальная демонстрация.
   - URL: https://eric.ed.gov/?id=EJ1321475 — CONFIRMED (Google Scholar)

5. **Härtel, H.** (2021). «The Electric Circuit — A System Approach.» *European Journal of Physics Education*, 12(1).
   - Системный подход: поверхностные заряды как основа понимания DC-цепей.
   - URL: https://eric.ed.gov/?id=EJ1309646 — источник недоступен (транспортная ошибка), но подтверждён через Google Scholar

6. **Sherwood, B. A. & Chabay, R. W.** (1999, 5-е изд. 2025). *Matter & Interactions* (учебник).
   - Унифицированный подход: поверхностные заряды создают поле, движущее ток; линейный σ(z) для прямого провода; накопление заряда на изгибах.
   - URL: https://matterandinteractions.org — CONFIRMED (сайт доступен, учебник описан)

7. **Wikipedia.** «Surface charge.»
   - σ = E·ε₀ на поверхности проводника (теорема Гаусса); проводник в равновесии — заряд на поверхности.
   - URL: https://en.wikipedia.org/wiki/Surface_charge — CONFIRMED

8. **Feynman, R. P., Leighton, R. B., & Sands, M.** (1964). *The Feynman Lectures on Physics*, Vol. II, Ch. 5.
   - Электростатика проводников; время релаксации заряда; поле вблизи проводников.
   - URL: https://www.feynmanlectures.caltech.edu/II_05.html — HTTP 403 (доступ ограничен), но классический источник (из знаний)

9. **Hirvonen, P. E.** (2007). «Surface-charge-based micro-models — a solid foundation for learning about direct current circuits.» *European Journal of Physics*, 28(3), 513.
   - Педагогический обзор моделей поверхностного заряда.
   - DOI: 10.1088/0143-0807/28/3/013 (из знаний — не верифицирован веб-запросом)

10. **Примечание о Müller 2012:** в предыдущем исследовании указан «Müller, R. (2012). A clearer picture of surface charges.» с DOI 10.1088/0143-0807/33/1/012. Этот DOI ведёт на статью Rojas-Trigos et al. о теплопередаче — НЕ о поверхностных зарядах. Поиск в Google Scholar по фразе «clearer picture of surface charges» не дал результатов. Ссылка на Müller (2012) **НЕ ПОДТВЕРЖДЕНА** и, вероятно, ошибочна.
