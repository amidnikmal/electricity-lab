# V18 (iter2, ЖИВОЙ ВЕБ) — экосистема и лицензии: перепроверка итерации 1

Date: 2026-06-14
Агент: V18 (состязательная перепроверка, src/ НЕ менялся)
Метод: WebSearch + WebFetch по первоисточникам (июнь 2026). Проверены утверждения из
`docs/research/B3-similar-tools.md` и `docs/research/B6-water-cfd.md`.

Легенда: CONFIRMED = подтверждено первоисточником; PARTIAL = верно с оговоркой;
REFUTED = опровергнуто.

---

## Пункт 1 — «Нет AC-анализа/частотного свипа/Боде, а у конкурентов есть»

**ВЕРДИКТ: CONFIRMED** (с уточнением по PhET).

- **ngspice — CONFIRMED.** Команда `.ac dec|oct|lin` делает small-signal AC-свип
  (амплитуда+фаза vs частота), нелинейные элементы линеаризуются вокруг DC-точки;
  нужен источник с `ac`-значением. Источник:
  https://ngspice.sourceforge.io/docs/ngspice-html-manual/manual.xhtml
  и .AC-страница https://nmg.gitlab.io/ngspice-manual/analysesandoutputcontrol_batchmode/analyses/ac_small-signalacanalysis.html
- **Qucs-S — CONFIRMED.** «Small signal AC simulation is fully supported»; шкалы
  linear/decade/octave; magnitude/phase (Bode-style) графики штатной визуализацией.
  Источник: https://qucs-s-help.readthedocs.io/en/legacy/BasSim.html
- **Falstad CircuitJS — CONFIRMED.** Есть выход «A/C Sweep» (gain/phase vs frequency)
  и готовые frequency-response демо (напр. Stub Frequency Response).
  Источники: https://www.falstad.com/circuit/doc/ (страница отдаёт 403 ботам, но
  feature реальна), демо https://www.falstad.com/circuit/e-tlfreq.html ,
  индекс демо https://www.falstad.com/circuit/e-index.html
- **PhET CCK:AC — PARTIAL/точно как и заявлено в B3.** Даёт AC-источник, RLC-экран и
  резонанс, графики V(t)/I(t), но НЕ формальный Bode/gain-vs-frequency sweep. B3 и
  утверждал именно «AC-источник и RLC-резонанс» — то есть формулировка B3 корректна,
  PhET не приписывался Боде. Источник:
  https://phet.colorado.edu/en/simulations/circuit-construction-kit-ac

Итог: утверждение B3 (DISCREPANCY, HIGH) о пробеле AC у проекта при наличии AC у
ngspice/Qucs-S/Falstad — **подтверждено**; нюанс лишь в том, что у PhET это
AC+резонанс, а не Боде (как B3 и писал).

---

## Пункт 2 — «Бедная библиотека элементов» vs наличие у конкурентов

**ВЕРДИКТ: CONFIRMED** (с распределением по инструментам).

- **Falstad — CONFIRMED по всем пяти.** Демо-индекс перечисляет Current Source,
  Current Mirror/Ramp, NPN/PNP/MOSFET-транзисторы, op-amp-схемы, лампу, логику.
  Источник: https://www.falstad.com/circuit/e-index.html ;
  список в README/contrib https://github.com/sharpie7/circuitjs1
- **PhET CCK:AC — CONFIRMED частично, ровно как у B3.** Есть AC-источник, батарея,
  резистор, лампочка (ohmic/non-ohmic), C, L, предохранитель, ключ, ammeter/voltmeter.
  НЕТ транзистора, ОУ, идеального источника тока — но B3 их PhET и НЕ приписывал.
  Источник: https://phet.colorado.edu/en/simulations/circuit-construction-kit-ac
- **Qucs-S / ngspice — CONFIRMED.** Полные модельные библиотеки (BJT/MOSFET/диоды/
  источники/ОУ-модели) через SPICE-движки. Источник:
  https://qucs-s-help.readthedocs.io/en/legacy/BasSim.html

Итог: тезис B3 (DISCREPANCY, HIGH) о бедной библиотеке проекта (нет источника тока,
AC-источника, лампы, транзистора, ОУ) на фоне конкурентов — **подтверждён**.

---

## Пункт 3 — Лицензии воды (КРИТИЧНО для развилки)

**ВЕРДИКТ: всё CONFIRMED по первоисточникам.**

| Инструмент | Заявлено в iter1 | Проверка | URL-первоисточник |
|---|---|---|---|
| OpenFOAM | GPLv3 | **CONFIRMED** — «licensed under the GNU General Public Licence», футер прямо «GPLv3» | https://openfoam.org/licence/ |
| FluidX3D | source-available / non-commercial | **CONFIRMED** — запрещены commercial use, military use, AI-training на коде; не OSI-open-source | https://raw.githubusercontent.com/ProjectPhysX/FluidX3D/master/LICENSE.md |
| SPlisHSPlasH | MIT | **CONFIRMED** — «The MIT License (MIT)», © 2016 Jan Bender | https://raw.githubusercontent.com/InteractiveComputerGraphics/SPlisHSPlasH/master/LICENSE |
| DualSPHysics | LGPL | **CONFIRMED** — «GNU LESSER GENERAL PUBLIC LICENSE Version 2.1» (LGPL-2.1) | https://raw.githubusercontent.com/DualSPHysics/DualSPHysics/master/LICENSE |

Следствие для развилки (без изменений к выводу B6): FluidX3D остаётся непригоден как
обычная OSS-зависимость для коммерческого/широкого релиза (non-commercial + запрет
AI-training); permissive-альтернатива для развилки II — SPlisHSPlasH (MIT);
DualSPHysics (LGPL-2.1) пригоден при приемлемости LGPL/CUDA; OpenFOAM (GPLv3)
требует жёсткой интеграционной границы (только офлайн/внешний процесс/данные).

---

## Пункт 4 — Найденные неточности/устаревания в B3 и B6

- **[НЕТОЧНОСТЬ, B6]** В §1, §4, §6 OpenFOAM описан как «под GPL» без указания
  версии; первоисточник openfoam.org/licence явно фиксирует **GPLv3**. Для
  лицензионной развилки версию надо называть точно (v2 vs v3 различаются по
  совместимости). Рекомендация: везде писать «GPLv3».
- **[OK, B3]** Формулировка про Falstad «Add A/C Sweep» (стр.30) — корректна:
  feature существует (gain vs frequency), подтверждено демо frequency-response.
- **[OK, B3]** PhET CCK:AC описан как «AC-источник + RLC-резонанс» — точно, без
  ложного приписывания Боде или транзисторов/ОУ.
- **[OK, B6]** Лицензии FluidX3D/SPlisHSPlasH/DualSPHysics указаны верно по
  первоисточникам; формулировка «free for non-commercial use» для FluidX3D точна.
- Прочих фактических расхождений при веб-проверке не обнаружено; ключевые URL из
  iter1 живы и релевантны (доступ к falstad.com/circuit/doc/ через браузер; ботам
  отдаёт 403 — это не признак устаревания).

---

## Сводка вердиктов

1. AC-свип/Боде у ngspice/Qucs-S/Falstad — CONFIRMED; PhET — AC+резонанс (PARTIAL,
   как и заявлено).
2. Источник тока/AC-источник/лампа/транзистор/ОУ — CONFIRMED у Falstad и Qucs/ngspice;
   PhET — подмножество (AC-источник, лампа), как и писал B3.
3. Лицензии: OpenFOAM=GPLv3, FluidX3D=non-commercial source-available,
   SPlisHSPlasH=MIT, DualSPHysics=LGPL-2.1 — все CONFIRMED по первоисточникам.
4. Единственная содержательная правка: в B6 уточнить OpenFOAM до «GPLv3».
</content>
</invoke>
