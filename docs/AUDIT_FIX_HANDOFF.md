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

## Волна 3 — мёртвый код + README (план)

- Удалить `renderToolbar`/`renderLog`/`m_logHeight`, `globalRange`, `cornerStep`,
  `std::max(wt*0.55, wt*1.1)` в `resistorBodyHalfWidth`.
- README: убрать неактуальное «No transient, capacitance or inductance» (уже реализовано).

## Волна 4 — замена велосипедов (требует решения пользователя)

- nlohmann/json (чинит потерю кириллицы в ответах LLM, баг с `\uXXXX→'?'`).
- cpp-httplib, std::format/fmt, мёртвая GLM (либо миграция `Vec2→glm::dvec2`, либо удалить).
