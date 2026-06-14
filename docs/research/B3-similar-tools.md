# B3 — Похожие инструменты и репозитории (чего не хватает проекту)

Date: 2026-06-14
Агент: B3 (research, src/ не менялся)
Тема: сравнение electricity-lab с open-source/educational симуляторами цепей и
вывод, что из их сильных сторон стоит перенять.

Сравниваемые инструменты:
- **Falstad CircuitJS1** (github.com/sharpie7/circuitjs1, оригинал falstad.com) —
  браузерный, GPLv2, знаменит анимацией тока «бегущими точками».
- **PhET Circuit Construction Kit (DC / AC)** (phet.colorado.edu, github.com/phetsims) —
  эталон учебного симулятора, «electron vs conventional current».
- **Qucs-S + ngspice / Xyce** (ra3xdh.github.io, qucs-s-help.readthedocs.io) —
  полноценный GUI поверх SPICE-движков.
- **ngspice** (ngspice.sourceforge.io) — референс численного солвера (Newton-Raphson,
  gmin/source stepping, AC/DC/tran/noise/FFT).
- **KiCad + ngspice, LTspice** — индустриальные пакеты (save/load, библиотеки моделей).

---

## Находки

- **[DISCREPANCY] Нет AC-анализа / частотного свипа / Боде.** Severity: HIGH.
  - Проект: солвер только DC (MNA) + транзиент (BE/трапеция). README прямо:
    «there is no AC (sinusoidal) source yet». Нет частотной развёртки, нет
    амплитудно-/фазочастотных характеристик.
  - Реальность/источники: ngspice реализует SPICE3f5 AC-анализ
    (ngspice.sourceforge.io/docs); Qucs-S даёт AC/DC/transient/S-param/FFT/distortion
    через единый GUI (qucs-s-help.readthedocs.io/en/legacy/BasSim.html); Falstad
    имеет «Add A/C Sweep» (gain vs frequency) + спектр в scope
    (falstad.com/circuit/doc); PhET CCK:AC даёт AC-источник и RLC-резонанс
    (phet.colorado.edu/.../circuit-construction-kit-ac).
  - Идеал: добавить синусоидальный источник + малосигнальный AC-свип (комплексная
    MNA, развёртка частоты) → строить Bode/резонанс RLC. Это самый крупный
    функциональный пробел учебного электро-симулятора.

- **[PHYSICS-BUG] Нелинейность решается фиксированной точкой по состояниям, нет
  Newton-Raphson.** Severity: MEDIUM.
  - Проект: диод — идеальный PWL (conducting/blocking), `solveIterative`
    итерирует состояния (<=24 прохода); Shockley I=Is(e^{V/nVt}-1) явно НЕ реализован
    (docs/ELEMENT_LIBRARY.md). Нет общего нелинейного движка.
  - Реальность/источники: ngspice использует Newton-Raphson с линеаризацией
    проводимости и критериями сходимости (0.1%/1pA по току, 0.1%/1µV по напряжению),
    плюс gmin-stepping и source-stepping как homotopy для трудной сходимости
    (ngspice manual; intusoft.com/articles/converg.pdf).
  - Идеал: ввести NR-цикл с линеаризацией (companion-модель управляемого тока),
    тогда Shockley-диод, лампа накаливания (нелинейное R(T)) и транзисторы станут
    возможны на одном движке. PWL оставить как быстрый дефолт.

- **[DISCREPANCY] Бедная библиотека активных/нелинейных элементов.** Severity: HIGH.
  - Проект: Wire, R, V-источник, Ground, C, L, идеальный диод, ключ
    (docs/ELEMENT_LIBRARY.md). Нет источника тока, нет AC-источника, нет лампочки,
    нет транзистора (BJT/MOSFET), нет ОУ, нет потенциометра/реостата, нет
    предохранителя.
  - Реальность/источники: PhET CCK даёт battery, resistor, lightbulb (ohmic и
    non-ohmic), fuse, switch, C, L, AC source (phet.colorado.edu); Falstad — десятки
    компонентов вкл. транзисторы/логику/ОУ; ngspice/Qucs-S — полные модельные
    библиотеки.
  - Идеал: приоритетно добавить лампочку (наглядно для обучения), источник тока и
    AC-источник; затем ОУ/транзистор поверх NR-движка.

- **[VISUAL-BUG] Анимация тока «амплифицирована» и помечена heuristic, нет честной
  количественной привязки скорости к |I|.** Severity: MEDIUM.
  - Проект: drift-частицы — `educational`, «visual speed is amplified for
    visibility» (README, docs/VISUALIZATION_MODEL.md). Скорость не пропорциональна
    току строго.
  - Реальность/источники: у Falstad бегущие жёлтые точки движутся со скоростью,
    *пропорциональной величине тока*, с единым слайдером Current Speed
    (falstad.com/circuit/doc) — простая, честная и читаемая модель.
  - Идеал: предложить режим «quantitative current» где скорость/плотность точек ∝ |I|
    с явным глобальным масштабом (как слайдер Falstad), рядом с текущим
    «амплифицированным» учебным режимом. Это снимет эпистемический долг визуализации.

- **[IDEA] Нет переключателя «электронный ток ↔ условный (conventional) ток».**
  Severity: MEDIUM.
  - Проект: соглашение об electron-flow зафиксировано и показано в UI, но единого
    тумблера двух представлений нет.
  - Реальность/источники: PhET CCK имеет явный переключатель Electrons /
    Conventional Current — ключевой дидактический приём против путаницы
    направления (github.com/phetsims/circuit-construction-kit-dc issue #160;
    phet.colorado.edu).
  - Идеал: один тумблер, меняющий и направление, и цвет/форму носителей, с подписью
    знака. Дёшево, высокая педагогическая ценность.

- **[DISCREPANCY] Нет сохранения/загрузки, нет undo/redo, нет шаринга по URL.**
  Severity: HIGH.
  - Проект: README, Known Limitations: «Save/load and undo/redo are still absent».
  - Реальность/источники: Falstad сериализует всю схему в строку и в URL
    (shareable links) + import/export файлов (вклад Rodrigo Hausen, github
    sharpie7/circuitjs1); KiCad/LTspice/Qucs-S — файловый формат проекта.
  - Идеал: текстовый формат схемы (де/сериализация Circuit) → save/load + undo-стек;
    бонусом «поделиться ссылкой» для выдачи заданий в Learning-модуле.

- **[IDEA] Нет встроенного осциллографа/«щупов» поверх транзиента.** Severity: MEDIUM.
  - Проект: есть `SignalRecorder` и черновой план (docs/OSCILLOSCOPE_THERMOMETER.md),
    но это не полноценный многоканальный scope как у конкурентов.
  - Реальность/источники: Falstad — 3 scope-окна одновременно (V и I по компоненту),
    режимы Show Spectrum / RMS / V vs I / X-Y (falstad.com/circuit/doc); PhET —
    графики V(t), I(t) и реалистичные ammeter/voltmeter; Qucs-S — пост-процессинг
    данных.
  - Идеал: довести scope до многоканального с FFT/спектром и X-Y; это естественно
    сочетается с AC-анализом и транзиентом, которые уже есть.

- **[IDEA] Нет подсхем/иерархических блоков и зонда измерений по клику.**
  Severity: LOW.
  - Проект: плоская схема, инспектор по выбранному элементу.
  - Реальность/источники: Qucs/ngspice — subcircuits (.subckt); Falstad — subcircuit
    в исходниках; KiCad — иерархические листы.
  - Идеал: для учебного объёма достаточно «именованных групп»/подсхем для повторного
    использования (RC-фильтр как блок в заданиях).

- **[IDEA] Уникальные сильные стороны проекта, которых нет у конкурентов — усилить, а
  не терять.** Severity: INFO.
  - Проект: механическая и гидравлическая проекции (ChainSim/HydraulicSim), слои
    E-поля/поверхностного заряда/B-поля с явной маркировкой статуса
    (exact-sign/approximation/heuristic), и научно-обоснованный Learning-модуль
    (retrieval practice, predict-then-verify, AI-критик-не-решатель).
  - Реальность/источники: ни Falstad, ни PhET, ни Qucs/ngspice не дают честной
    физ.-эпистемической маркировки слоёв и анти-делегирующего обучения — это
    дифференциатор electricity-lab.
  - Идеал: при заимствовании фич конкурентов сохранять дисциплину статус-меток
    (новые слои AC/scope тоже маркировать), не размывая главное преимущество.

- **[IDEA] Цветовая карта потенциала vs знак-кодирование напряжения.** Severity: LOW.
  - Проект: градиент потенциала вдоль проводника (approximation).
  - Реальность/источники: Falstad кодирует напряжение цветом узла
    (зелёный +, серый земля, красный −) — мгновенно читаемо новичком
    (lushprojects.com/circuitjs, falstad.com/circuit/doc).
  - Идеал: опциональная диск-карта «знак потенциала цветом узла» как быстрый режим
    рядом с непрерывным градиентом.

---

## Сводка по категориям

- DISCREPANCY: 3 (AC-анализ, библиотека элементов, save/load+share)
- PHYSICS-BUG: 1 (нет Newton-Raphson / Shockley)
- VISUAL-BUG: 1 (амплифицированная скорость тока без количественного режима)
- IDEA: 5 (тумблер ток-конвенция, scope, подсхемы, сохранить уник.преимущества, цвет узла)
- Всего: 10

## Источники

- Falstad CircuitJS1 (репо): https://github.com/sharpie7/circuitjs1
- Falstad документация (фичи, current dots, scope, AC sweep): https://www.falstad.com/circuit/doc/
- CircuitJS1 интерактив / цвет напряжения: https://lushprojects.com/circuitjs/
- PhET Circuit Construction Kit: DC: https://phet.colorado.edu/en/simulations/circuit-construction-kit-dc
- PhET Circuit Construction Kit: AC: https://phet.colorado.edu/en/simulations/circuit-construction-kit-ac
- PhET CCK исходники (electron vs conventional, issue #160): https://github.com/phetsims/circuit-construction-kit-dc/issues/160
- Qucs-S: https://ra3xdh.github.io/
- Qucs-S Help — базовая ngspice/Xyce симуляция (AC/DC/tran): https://qucs-s-help.readthedocs.io/en/legacy/BasSim.html
- Qucs-S Help — RF/S-параметры: https://qucs-s-help.readthedocs.io/en/legacy/RF.html
- ngspice User's Manual (NR, gmin/source stepping, модели): https://ngspice.sourceforge.io/docs/ngspice-html-manual/manual.xhtml
- SPICE convergence (Intusoft, gmin/source stepping): http://www.intusoft.com/articles/converg.pdf
