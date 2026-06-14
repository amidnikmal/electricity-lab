# Хэндоф: исправление багов по CODE_AUDIT_2026-06-14

> Рабочая ветка: `fix/code-audit-2026-06-14` (от `main`, базовый коммит `0a648d5`).
> Процесс: оркестратор раздаёт задачи агентам **kilo** (код) и **codex** (тесты)
> в изолированных git worktree → ревью диффа → cherry-pick в ветку фиксов →
> централизованная сборка+тесты → коммит. Все коммиты и комментарии на русском.

## Методология

- Каждый баг чинится в отдельном worktree (`../wt-<тема>`), агент сам коммитит.
- Оркестратор читает реальный дифф (не пересказ агента), собирает и гоняет тесты
  в **едином** build-дереве (`build/`), чтобы не тянуть FetchContent в каждый worktree.
- Сборка: `ninja current-lab-tests` (тесты) + `ninja current-lab` (GL-приложение).
- Baseline до правок: **491 тест проходит**.

## Известные предсуществующие проблемы (не из аудита, замечены при сборке)

- ⚠️ `src/projection/ProjectionBuilder.cpp:286` — warning «hex escape sequence out of
  range»: строка `tr("\xC2\xB5F")` для символа микрофарад. Компилятор парсит `\xB5F`
  как один hex-escape (выходит за диапазон), `F` съедается. Символ µ (U+00B5) ломается.
  Кандидат на фикс отдельным коммитом: `"\xC2\xB5" "F"` (склейка строковых литералов).

---

## Волна 1 — быстрые победы (ЗАВЕРШЕНА ✅)

Сборка чистая, 491 тест проходит после мёржа.

| Баг | Серьёзность | Файл | Что сделано | Коммит |
|-----|-------------|------|-------------|--------|
| №4 | high | `learning/TaskGenerator.cpp` | В условии задачи «3 резистора последовательно» (difficulty≥3) теперь печатается значение `R3` — задача стала аналитически решаемой. | `8b44231` |
| №10 | medium | `learning/TaskGenerator.cpp` | `format()` больше не пишет в фиксированный `char buf[256]`: размер вычисляется через `snprintf(nullptr,0,...)`, длинные многострочные пояснения не усекаются. | `8b44231` |
| №3 | high | `net/HttpClient.cpp` | Цикл `recv` различает `n==0` (штатный EOF) и `n<0` (ошибка/таймаут → возврат ошибки). Раньше усечённый ответ молча возвращался как успех. | `7b893a5` |
| №2 | high | `ui/InspectorPanel.cpp` | `componentTypeLabel`/`componentModelLabel`/`layerInfoForComponent` покрывают все 8 типов (добавлены Capacitor/Inductor/Diode/Switch — раньше «?»). | `0dbadbd` |

---

## Волна 2 — физика/рендер/проекция/app (ЗАВЕРШЕНА ✅)

Сборка чистая, 494 теста проходят (491 + 3 новых регрессионных).

| Баг | Серьёзность | Файл | Что сделано | Коммит |
|-----|-------------|------|-------------|--------|
| №1 | medium | `physics/SurfaceChargeModel.h` | Удалён мёртвый блок `junctionStrength` (лапласиан линейного потенциала ≡ 0) + неиспользуемый `globalRange` и осиротевший `segmentLen`. | `c75a585`, `f4c2d7d` |
| №5 | medium | `projection/ProjectionBuilder.cpp` (`emitTurbine`) | Угол турбины теперь `spinPhase(ctx, comp.id, flow, …)` — интеграл потока, без рывков (как насос/индуктор). | `15b415c` |
| №6 | medium | `render/PrimitiveRenderer.cpp` | Альфа всех слоёв glow умножается на `glow.intensity` — слабый/сильный источник различаются яркостью. | `b760942` |
| №7,№9 | low | `ui/MathTextParse.cpp` | `parseGroup` заменяет `-`→U+2212 (согласовано с `parseRow`); неизвестная `\command` сохраняет `\`. | `fab2550` |
| №11 | low | `app/App.cpp` | `glfwTerminate()` на пути ошибки `glfwCreateWindow` — нет утечки GLFW. | `e01c474` |

**Регрессионные тесты (codex), коммит `8880d69`:**
- `TaskGenerator.ThreeSeriesResistorsPromptIsSolvable` — баг №4 (R3 в условии, нет «(third)»).
- `MathText.MinusInUnbracedScriptBecomesProperMinus` — баг №7.
- `MathText.UnknownCommandKeepsBackslash` — баг №9.

## Волна 3 — мёртвый код + README (ЗАВЕРШЕНА ✅)

Сборка чистая (µF-warning устранён), 494 теста проходят.

- `refactor` (`86d8281`): удалены `MainWindow::renderToolbar/renderLog` + поле `m_logHeight`
  (нигде не вызывались); `resistorBodyHalfWidth` упрощён до `wt*1.1` (std::max был тождествен);
  починен µF: `tr("\xC2\xB5" "F")` — раньше `\xB5F` парсился как один hex-escape, символ µ ломался.
- `docs(readme)` (предыдущий коммит): убрано неверное «No transient, capacitance or inductance»
  (транзиент RC/RL реализован); уточнено — нет AC-источника, диод идеальный PWL.
- `globalRange` (§3 мёртвый код) уже удалён в волне 2 вместе с фиксом бага №1.
- Прочий мёртвый код из §3 (половина `DualViewState.h`, `PrimLabel::debugOnly`, `cornerStep`)
  — НЕ трогали в этой волне (требует более глубокого анализа реальной используемости; кандидаты
  на отдельную волну).

## Волна 4 — замена велосипедов (решение пользователя: ПОЛНЫЙ объём)

Окружение: GCC 13.3, C++20 → `std::format` доступен (fmt не нужен). GLM скачивался,
но не был подключён ни к одной цели (мёртвая зависимость) — будет задействован в 4d.

### 4a — nlohmann/json (ЗАВЕРШЕНО ✅)
Сборка чистая, 494 теста проходят. Подтянут `nlohmann_json v3.11.3` (FetchContent), слинкован
к обеим целям.
- `feat(json)` (`48dcb27`): `LlmClient` и `AnkiExport` переведены на nlohmann. Главный фикс —
  `extractAssistantReply` теперь честно парсит JSON: **`\uXXXX` декодируется в UTF-8**
  (кириллица в ответах тьютора больше не превращается в `'?'`), структура валидируется.
  `escapeJson`/`buildChatRequest`/`buildAddNotesPayload` — через nlohmann.
- `fix(json)` (`14d0f8d`): `buildChatRequest` использует `ordered_json` — wire-формат запроса
  к LLM остаётся байт-в-байт прежним (обычный json сортирует ключи алфавитно).
- `test(json)` (codex): 4 теста переписаны под корректное поведение (Unicode декодируется,
  невалидные escape отвергаются, числовой content → false, структура валидируется).

### 4b — std::format вместо snprintf-в-буфер (ЗАВЕРШЕНО ✅)
Сборка чистая, 494 теста. ~30 вызовов snprintf-в-фиксированный-буфер переведены на `std::format`
(GCC13/C++20, без новых зависимостей) в `ProjectionBuilder` (+helper'ы `formatCapacitance/Inductance`
теперь возвращают `std::string`), `PrimitiveRenderer`, `InspectorPanel`, `LearningSession.h`,
`AnkiExport`, `MainWindow`.
- **Осознанные исключения:**
  - `TaskGenerator.cpp` `format()` — формат-строка приходит как runtime-параметр; `std::format`
    неприменим. Уже сделан безопасным (динамический буфер) в волне 1.
  - `LearningPanel.cpp` — формат-строка переводимая (`tr()` во время выполнения, плейсхолдеры
    в I18n в printf-стиле); `std::format` требует строку времени компиляции → оставлен `snprintf`.

### 4c — cpp-httplib (в работе)
### 4d — Vec2 → glm::dvec2 (план, высокий риск, последним)
