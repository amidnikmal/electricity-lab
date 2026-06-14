# V17: Learning-промпты — перепроверка B8

Date: 2026-06-14
Finding: B8 (docs/research/B8-learning-science.md)

---

## Пункт 1: prediction-вопросы НЕ адресуют ключевые заблуждения

**ВЕРДИКТ: ПОДТВЕРЖДЕНО.**

Шесть predictionPrompt в `src/learning/TaskGenerator.cpp`:

| TaskFamily | Строка | predictionPrompt | Адресует ли «ток расходуется» / «батарея = источник тока»? |
|---|---|---|---|
| OhmsLaw | :108-109 | «if the resistance doubled, would the current rise or fall?» | Нет — про зависимость I от R по закону Ома |
| SeriesResistors | :147-148 | «which resistor gets the larger share of the source voltage?» | Нет — про деление напряжения |
| ParallelResistors | :173-174 | «is the total current larger or smaller than through R1 alone?» | Нет — про суммирование токов в параллели |
| PowerDissipation | :195-196 | «if the voltage doubled, by what factor would the heat grow?» | Нет — про масштабирование мощности |
| RcTimeConstant | :229-230 | «at t = tau, is Vc above or below half the source voltage?» | Нет — про заряд конденсатора |
| RlTimeConstant | :263-264 | «right after switch-on, is the current zero or maximal?» | Нет — про поведение индуктивности |

**Доказательство:** Ни один из 6 вопросов не спрашивает «одинаков ли ток до и после резистора?» (опровержение «ток расходуется») и не спрашивает «изменится ли ток от батареи при изменении нагрузки?» (опровержение «батарея = источник постоянного тока»).

**Источник:** Küçüközer & Kocakülah 2007, *Journal of Turkish Science Education*, 4(1), 101-115 (175 цит.) — «consumption of current» и «batteries are constant current sources» — наиболее частые заблуждения.

---

## Пункт 2: промпт сократического критика подавляет verification feedback

**ВЕРДИКТ: ЧАСТИЧНО ПОДТВЕРЖДЕНО (пограничный).**

Формулировка `src/assistant/LlmClient.cpp:8-16` (`socraticCriticSystemPrompt`):

> «if the attempt is correct, ask a transfer question instead. Be brief and factual. No praise, no motivational talk.»

**Что в порядке:** промпт явно задаёт поведение при правильном ответе — задать transfer question. Это *поведенчески* эквивалентно verification (модель действует так, как будто подтвердила — переходит к следующему шагу).

**Что под риском:** отсутствует явное разрешение подтвердить правильность хода мысли. Комбинация «No praise, no motivational talk. Be brief and factual.» без инструкции «briefly confirm correctness» может интерпретироваться LLM как запрет на любые оценочные высказывания, включая деловое «That's correct». Мелкие/слабые модели могут пропускать verification entirely и сразу выдавать transfer question без подтверждения.

**Доказательство:** `LlmClient.cpp:15-16` — «if the attempt is correct, ask a transfer question instead. Be brief and factual. No praise, no motivational talk.» — разрешение подтверждать правильность отсутствует; запрет «No praise» сформулирован категорично, без разграничения self-level praise и task-level verification.

**Источник:** Hattie & Timperley 2007, *Review of Educational Research*, 77(1), 81-112 — verification feedback (подтверждение правильности на уровне task) — один из эффективных компонентов обратной связи; отличие от self-level praise (похвала личности), которая наименее эффективна.

**Рекомендация:** Уточнить промпт: «If the attempt is correct, briefly confirm it and ask a transfer question. Do not praise the student personally.»
