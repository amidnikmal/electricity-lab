# B8: Наука обучения — исследование

Date: 2026-06-14
Scope: `src/learning/`, `src/assistant/LlmClient.*`, `docs/LEARNING_MODULE.md`, `tests/test_learning.cpp`

---

## Отчёт о находках

### [IDEA] Усилить predict-задания конкретными заблуждениями

**Severity:** MODERATE
**Проект:** `src/learning/TaskGenerator.cpp:108-109` (predictionPrompt для Ohm's law)
**Реальность/источники:**
- Küçüközer & Kocakülah 2007, *Journal of Turkish Science Education* (175 цит.): «ток расходуется» и «батарея — источник постоянного тока» — наиболее частые заблуждения.
- Wainwright 2007, *Association for Science Teacher Education*: добавляет Battery Origin и Battery Agency.
- Nasri 2020, *Learning Science and Mathematics* (19 цит.): POE-стратегия эффективна именно для концепций электрических цепей.
- Latifah et al. 2019, *Journal of Physics: Conf. Series* (43 цит.): POE значимо снижает процент misconceived ответов.
**Идеал:** predictionPrompt для Ohm's law сейчас: «если сопротивление удвоится, ток вырастет или упадёт?» — это правильно, но не задевает заблуждение «батарея даёт постоянный ток». Предлагается: добавить в ротацию prediction-вопрос, который сталкивает студента с заблуждением, например: «Если мы подключим ДВА резистора вместо одного, батарея будет давать тот же ток или другой?» (целится в battery-as-constant-current-source). Для series/parallel — вопрос про «расходование» тока: «Одинаков ли ток в начале и в конце цепочки резисторов?»

---

### [IDEA] Pre-testing перед уроком (errorful generation)

**Severity:** LOW-MODERATE
**Проект:** `src/learning/Lessons.h:96-155` (структура уроков)
**Реальность/источники:**
- Pre-testing effect (Richland, Kornell & Kao 2009; Pan & Sana 2021): неудачная попытка ответить до изучения материала улучшает последующее усвоение при условии получения правильного ответа после.
- Даже ошибочная генерация (errorful generation) усиливает кодирование в памяти — Kornell, Hays & Bjork 2009.
- Pan et al. 2020: pre-testing снижает mind-wandering во время лекций.
- Wikipedia: «Pre-testing effect… testing material before it has been learned leads to better subsequent learning performance provided that feedback is given».
**Идеал:** Перед показом derivation arc (situation → action → words → symbols → formula) задавать один pre-question: «Как ты думаешь, что произойдёт с X, если Y?» Ответ не оценивается, но факт попытки записывается. Это не требует изменения структуры Lessons.h — достаточно обёртки в UI/LearningSession, которая показывает pre-question до arc.

---

### [IDEA] Толерантность ответа: добавить adaptive tolerance

**Severity:** LOW
**Проект:** `src/learning/TaskGenerator.cpp:28-30` (toleranceFor), `src/learning/LearningSession.h:96` (submitAttempt)
**Реальность/источники:**
- Butler & Roediger 2008, *Memory & Cognition*: feedback качественно улучшает learning outcomes тестирования; tolerance — форма feedback-gating.
- Формирующее оценивание (Black & Wiliam 1998, 2009): ключевой элемент — не бинарное правильно/неправильно, а информация о близости к цели.
- Текущая tolerance: `max(|truth| * 0.02, absFloor)` — статична. Research по desirable difficulties (Bjork) предполагает, что убывающая tolerance по мере практики может быть полезнее: начинать с широкого допуска (10%), сужать до 2%.
**Идеал:** Добавить в TaskGenerator параметр `toleranceTightness`, зависящий от числа успешных попыток студента в данной теме. Либо в LearningSession добавить `toleranceScale` с прогрессией.

---

### [IDEA] FSRS vs SM-2: валидация выбора

**Severity:** LOW (подтверждение, не изменение)
**Проект:** `docs/LEARNING_MODULE.md:42-43`, `src/learning/AnkiExport.h:8-10`
**Реальность/источники:**
- SRS Benchmark (open-spaced-repetition/srs-benchmark, 2023-2026): на 10 000 пользователях, ~350 млн ревью (без same-day), ~519 млн (с same-day):
  - FSRS-7: Log Loss 0.3437, RMSE(bins) 0.0655, AUC 0.7069
  - SM-2 (примерно FSRS v3/v4): Log Loss ~0.37-0.44, RMSE ~0.08-0.11
  - Разница: FSRS-7 даёт на ~30% меньше работы при том же retention или на ~30% выше retention при той же нагрузке.
- FSRS использует трёхкомпонентную модель памяти (retrievability, stability, difficulty) + стохастический shortest-path алгоритм (Ye et al. 2022, ACM KDD; Ye et al. 2023, IEEE TKDE).
- SM-2 — детерминированный алгоритм 1987 года с фиксированными параметрами (ease factor, interval modifier).
- Anki перешёл на FSRS по умолчанию с версии 23.10 (октябрь 2023).
**Идеал:** Текущий подход — «экспорт в Anki, планирование — FSRS» — полностью соответствует наилучшим доступным evidence. Единственное: убедиться, что в UI/документации явно рекомендовано включать FSRS в Anki (Settings → FSRS → Enable), а не оставлять legacy SM-2.

---

### [DISCREPANCY] «No praise, no motivational talk» vs evidence по formative feedback

**Severity:** MODERATE
**Проект:** `src/assistant/LlmClient.cpp:16` (socraticCriticSystemPrompt: «No praise, no motivational talk. Be brief and factual.»)
**Реальность/источники:**
- Hattie & Timperley 2007, *Review of Educational Research*: модель feedback на 4 уровнях (task, process, self-regulation, self). Feedback на уровне self (похвала) действительно наименее эффективен и может быть вреден. Feedback на уровне task и process — наиболее эффективен.
- Butler & Roediger 2008: feedback, confirming correct responses, enhances learning.
- Shute 2008, *Review of Educational Research*: formative feedback guidelines — «provide feedback about the correctness of the response, not about the student».
- Kluger & DeNisi 1996: praise может отвлекать от task-learning.
- НО: полное отсутствие подтверждения правильности (не похвалы, а verification feedback) противоречит evidence — студенту нужно знать, что его ход мысли верен, чтобы двигаться дальше.
**Идеал:** Разделить «no praise» (правильно — не хвалить студента как личность) и «verification feedback» (подтвердить, что направление мысли верно). Prompt можно уточнить: «If the attempt is correct, briefly confirm it and ask a transfer question. Do not praise the student personally. If the reasoning is partially correct, acknowledge the correct part before asking your counter-question.» Текущая формулировка «No praise, no motivational talk. Be brief and factual.» может интерпретироваться моделью как «не подтверждай правильность», что снижает effectiveness фидбека.

---

### [DISCREPANCY] LLM-тьютор: качество зависит от модели — валидация

**Severity:** MODERATE (подтверждение, не изменение)
**Проект:** `src/assistant/LlmClient.h:12-16` (честное ограничение), `src/assistant/LlmClient.cpp:8-16` (socraticCriticSystemPrompt)
**Реальность/источники:**
- Bastani et al. 2024, «Generative AI Can Harm Learning» (SINGLE-STUDY, крупное полевое RCT, ~1000 студентов): доступ к LLM улучшает practice performance, но ухудшает unassisted performance, если LLM не ограничен ролью тьютора-критика.
- Santos-Guevara et al. 2026, *Education Sciences* (MDPI, 4 цит.): Socratic dialogue через ChatGPT — эффективность сильно зависит от качества scaffolding и размера модели; небольшие модели не способны удерживать Socratic-позицию.
- Koenig et al. 2007, *Physical Review ST Physics Education Research* (85 цит.): Socratic dialogue в physics tutorials показывает strong support — но с человеческими тьюторами.
- Ali et al. 2026, *Regional Lens*: structured Socratic dialogue scaffolds работают, но «Socratic AI tutoring effectiveness depends strongly on the AI's ability to maintain the questioning stance».
**Идеал:** Текущая позиция в коде (safeguards — это code, не prompt; critic-not-solver; attempt-first gating; documented honest limitation) полностью валидирована research. Единственное дополнение: рассмотреть добавление confidence score / uncertainty indicator в UI рядом с ответом ассистента (а-la «этот ответ сгенерирован локальной моделью, качество не гарантировано»).

---

### [DISCREPANCY] Interleaving: код гарантирует смену family, но не difficulty

**Severity:** LOW
**Проект:** `src/learning/TaskGenerator.cpp:59-67` (generateNext)
**Реальность/источники:**
- Rohrer & Taylor 2007, Dunlosky et al. 2013: interleaving problem types эффективно (MODERATE-STRONG).
- НО: Rohrer 2012, *Applied Cognitive Psychology*: interleaving difficulty levels внутри одного типа тоже даёт прирост (MODERATE, меньше репликаций).
- Текущий код: `generateNext(difficulty)` получает difficulty извне — если пользователь всегда на difficulty=2, interleaving работает только по families. Без варьирования difficulty студент не получает desirable difficulty gradient.
**Идеал:** В режиме «Mixed practice» варьировать difficulty случайно (±1 от текущего уровня студента) — это даст interleaving и по типам задач, и по сложности.

---

### [PHYSICS-BUG] Заблуждение «ток расходуется» не адресовано в prediction-заданиях

**Severity:** MODERATE-HIGH
**Проект:** `src/learning/TaskGenerator.cpp:147-148` (SeriesResistors predictionPrompt), `src/learning/TaskGenerator.cpp:173-174` (ParallelResistors predictionPrompt)
**Реальность/источники:**
- Küçüközer & Kocakülah 2007: «consumption of current» — одно из трёх наиболее частых заблуждений об электричестве (175 цит.).
- Goris & Dyrenfurth 2013, *ASEE* (16 цит.): даже студенты electrical engineering technology разделяют это заблуждение на первых курсах.
- Sefton 2002, *Science Teachers' Workshop* (40 цит.): «current is not consumed by electric devices» — ключевой пункт, который учебники часто обходят.
- Aligo et al. 2021, *Manila Journal of Science* (29 цит.): misconceptions сохраняются даже у science teachers.
- Osman 2017, *Springer* (19 цит.): learning cycle approach эффективно снижает эти misconceptions.
**Идеал:** Текущие prediction-вопросы:
- Series: «which resistor gets the larger share of the source voltage?» — не про ток.
- Parallel: «is the total current larger or smaller than through R1 alone?» — не про расходование.
Предлагается: для series добавить predictionPrompt: «Before solving: is the current through R1 larger than, smaller than, or equal to the current through R2?» Это напрямую адресует misconception «ток уменьшается после каждого резистора». Для parallel: «Before solving: if we remove R2, will the current through R1 change?» Адресует misconception, что батарея «выдаёт постоянный ток» (на самом деле — постоянное напряжение, а ток зависит от нагрузки).

---

### [PHYSICS-BUG] Заблуждение «батарея = источник тока» не адресовано

**Severity:** MODERATE
**Проект:** `src/learning/Lessons.h:98-104` (урок Ohm's law), `src/learning/TaskGenerator.cpp:95-115` (Ohm's law task generation)
**Реальность/источники:**
- Küçüközer & Kocakülah 2007: «batteries are constant current sources» — частое заблуждение.
- Goris & Dyrenfurth 2013: «beliefs that a battery is a source of constant current» зафиксированы на всех курсах.
- Lee 2007, *International Journal of Science Education* (57 цит.): conceptions about battery consumption — отдельная категория misconceptions.
- Hesti et al. 2017, *Journal of Physics: Conf. Series* (16 цит.): analogy-based instruction помогает, но требует явного указания на различие источник напряжения vs источник тока.
**Идеал:** В урок Ohm's law (Lessons.h:98) добавить явное указание: «Батарея держит постоянное напряжение, а не ток. Ток зависит от того, ЧТО подключено.» В prediction-вопросах Ohm's law: «Если мы подключим два резистора параллельно вместо одного, ток от батареи: (а) не изменится, (б) увеличится, (в) уменьшится?» — целится в battery-as-constant-current-source.

---

### [IDEA] Генеративное обучение: prediction-prompts можно усилить

**Severity:** LOW
**Проект:** Все predictionPrompt в `src/learning/TaskGenerator.cpp`
**Реальность/источники:**
- Generation effect: Slamecka & Graf 1978; McNamara & Healy 2000; DeWinstanley & Bjork 2004 — STRONG, многократно реплицирован.
- Самообъяснение: Chi et al. 1989; Chi 2013 (1501 цит.) — MODERATE-STRONG.
- Текущие prediction-prompts — бинарные/качественные («rise or fall?», «larger or smaller?»). Это generation, но слабой глубины.
- Исследования показывают: открытые генеративные задания («объясни, почему…») дают больший эффект, чем выбор из двух вариантов (Roelle et al. 2023, *Educational Psychology Review*, 56 цит.).
**Идеал:** Часть prediction-prompts (особенно на difficulty 3) заменить на открытые: «Before solving: explain why the capacitor voltage grows slower as it approaches the source voltage.» Это generation + self-explanation одновременно.

---

## Сводная таблица evidence-базы

| Evidence | Strength | Ключевые источники |
|---|---|---|
| Retrieval practice / testing effect | STRONG | Roediger & Karpicke 2006; Dunlosky et al. 2013; Agarwal et al. 2021 |
| Spaced repetition (FSRS > SM-2) | STRONG | SRS Benchmark 10k users; Ye et al. 2022 ACM KDD; Ye et al. 2023 IEEE TKDE |
| Predict-Observe-Explain (POE) | MODERATE-STRONG | Nasri 2020; Latifah et al. 2019; Venida & Sigua 2020 |
| Generation effect / generative learning | STRONG | Slamecka & Graf 1978; Chi 2013 |
| Socratic dialogue (human tutor) | STRONG | Koenig et al. 2007; Valiotis 2008 |
| Socratic dialogue (LLM tutor) | MODERATE | Santos-Guevara et al. 2026; Ali et al. 2026 |
| LLM harms unassisted performance | SINGLE-STUDY | Bastani et al. 2024 (~1000 students) |
| Pre-testing / errorful generation | MODERATE | Richland et al. 2009; Pan & Sana 2021 |
| Formative assessment / tolerance | STRONG | Black & Wiliam 1998; Hattie & Timperley 2007 |
| Electricity: «current consumed» | STRONG (misconception prevalence) | Küçüközer & Kocakülah 2007; 20+ репликаций |
| Electricity: «battery = constant current» | STRONG (misconception prevalence) | Küçüközer & Kocakülah 2007; Goris 2013 |
| Feedback: praise can harm, verification helps | STRONG | Hattie & Timperley 2007; Kluger & DeNisi 1996; Butler & Roediger 2008 |
| Interleaving (problem types) | MODERATE-STRONG | Rohrer & Taylor 2007; Dunlosky et al. 2013 |

---

## Источники

1. Roediger, H. L., & Karpicke, J. D. (2006). Test-Enhanced Learning: Taking Memory Tests Improves Long-Term Retention. *Psychological Science*, 17(3), 249-255.
2. Dunlosky, J., Rawson, K. A., Marsh, E. J., Nathan, M. J., & Willingham, D. T. (2013). Improving students' learning with effective learning techniques. *Psychological Science in the Public Interest*, 14(1), 4-58.
3. Cepeda, N. J., Pashler, H., Vul, E., Wixted, J. T., & Rohrer, D. (2006). Distributed practice in verbal recall tasks: A review and quantitative synthesis. *Psychological Bulletin*, 132(3), 354-380.
4. Slamecka, N. J., & Graf, P. (1978). The generation effect: Delineation of a phenomenon. *Journal of Experimental Psychology: Human Learning and Memory*, 4(6), 592-604.
5. Chi, M. T. H., Bassok, M., Lewis, M. W., Reimann, P., & Glaser, R. (1989). Self-explanations: How students study and use examples in learning to solve problems. *Cognitive Science*, 13(2), 145-182.
6. Bjork, R. A. (1994). Memory and metamemory considerations in the training of human beings. In J. Metcalfe & A. Shimamura (Eds.), *Metacognition: Knowing about knowing* (pp. 185-205). MIT Press.
7. Sweller, J. (1988). Cognitive load during problem solving: Effects on learning. *Cognitive Science*, 12(2), 257-285.
8. Rohrer, D., & Taylor, K. (2007). The shuffling of mathematics problems improves learning. *Instructional Science*, 35(6), 481-498.
9. Parasuraman, R., & Riley, V. (1997). Humans and automation: Use, misuse, disuse, abuse. *Human Factors*, 39(2), 230-253.
10. Risko, E. F., & Gilbert, S. J. (2016). Cognitive offloading. *Trends in Cognitive Sciences*, 20(9), 676-688.
11. Bastani, H., Bastani, O., et al. (2024). Generative AI Can Harm Learning. *Working paper* (~1000 students, large field RCT).
12. Küçüközer, H., & Kocakülah, S. (2007). Secondary school students' misconceptions about simple electric circuits. *Journal of Turkish Science Education*, 4(1), 101-115.
13. Wainwright, C. L. (2007). Toward learning and understanding electricity: Challenging persistent misconceptions. *Association for Science Teacher Education*.
14. Goris, T. V., & Dyrenfurth, M. J. (2013). How electrical engineering technology students understand concepts of electricity. *ASEE Annual Conference*.
15. Nasri, N. M. (2020). The effectiveness of predict-observe-explain-animation (POE-A) strategy to overcome students' misconceptions about electric circuits concepts. *Learning Science and Mathematics*, 15, 1-15.
16. Latifah, S., Irwandani, I., Saregar, A., Diani, R., et al. (2019). How the Predict-Observe-Explain (POE) learning strategy remediates students' misconception on Temperature and Heat materials? *Journal of Physics: Conference Series*, 1171, 012051.
17. Venida, A. C., & Sigua, E. M. S. (2020). Predict-observe-explain strategy: Effects on students' achievement and attitude towards physics. *Jurnal Pendidikan MIPA*, 21(2), 150-163.
18. Koenig, K. M., Endorf, R. J., & Braun, G. A. (2007). Effectiveness of different tutorial recitation teaching methods and its implications for TA training. *Physical Review Special Topics — Physics Education Research*, 3(1), 010104.
19. Santos-Guevara, A., Aquines-Gutiérrez, O., et al. (2026). Fostering Student Engagement and Learning Perception Through Socratic Dialogue with ChatGPT. *Education Sciences*, MDPI.
20. Kornell, N., Hays, M. J., & Bjork, R. A. (2009). Unsuccessful retrieval attempts enhance subsequent learning. *Journal of Experimental Psychology: Learning, Memory, and Cognition*, 35(4), 989-998.
21. Ye, J., Su, J., & Cao, Y. (2022). A Stochastic Shortest Path Algorithm for Optimizing Spaced Repetition Scheduling. *Proceedings of the 28th ACM SIGKDD Conference* (KDD '22), 4381-4390.
22. Ye, J., Su, J., Nie, L., Cao, Y., & Chen, Y. (2023). Optimizing Spaced Repetition Schedule by Capturing the Dynamics of Memory. *IEEE Transactions on Knowledge and Data Engineering*, 35(10), 10085-10097.
23. SRS Benchmark. open-spaced-repetition/srs-benchmark. GitHub. https://github.com/open-spaced-repetition/srs-benchmark
24. Hattie, J., & Timperley, H. (2007). The Power of Feedback. *Review of Educational Research*, 77(1), 81-112.
25. Butler, A. C., & Roediger, H. L. (2008). Feedback enhances the positive effects and reduces the negative effects of multiple-choice testing. *Memory & Cognition*, 36(3), 604-616.
26. Black, P., & Wiliam, D. (1998). Assessment and classroom learning. *Assessment in Education: Principles, Policy & Practice*, 5(1), 7-74.
27. Richland, L. E., Kornell, N., & Kao, L. S. (2009). The pretesting effect: Do unsuccessful retrieval attempts enhance learning? *Journal of Experimental Psychology: Applied*, 15(3), 243-257.
28. Pan, S. C., & Sana, F. (2021). Pretesting versus posttesting: Comparing the pedagogical benefits of errorful generation and retrieval practice. *Journal of Experimental Psychology: Applied*, 27(2), 237-257.
29. Osman, K. (2017). Addressing secondary school students' misconceptions about simple current circuits using the learning cycle approach. In *Overcoming students' misconceptions in science* (pp. 223-242). Springer.
30. Aligo, B. L., Branzuela, R. L., Faraon, C. A. G., et al. (2021). Teaching and learning electricity — A study on students' and science teachers' common misconceptions. *Manila Journal of Science*, 14, 60-80.
31. Lee, S. J. (2007). Exploring students' understanding concerning batteries — Theories and practices. *International Journal of Science Education*, 29(4), 497-516.
32. Anki Manual. Background. https://docs.ankiweb.net/background.html
33. FSRS4Anki Wiki. https://github.com/open-spaced-repetition/fsrs4anki/wiki
34. Wikipedia. Testing effect, Generation effect, Spaced repetition. Retrieved 2026-06-14.
