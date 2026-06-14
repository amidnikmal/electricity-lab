# V13: HiDPI uiScale — ПЕРЕПРОВЕРКА НАХОДКИ B7

## Вердикт: **ПОДТВЕРЖДЕНО (TRUE)**

`uiScale` не доходит до screen-space примитивов канваса. На мониторе 175% частицы, линии, полилинии и окружности с флагом `screenSpaceWidth`/`screenSpaceRadius` отрисовываются физически в 1.75× меньше относительно UI.

## Доказательство

### 1. `m_uiScale` приватный, геттера нет

`src/app/App.h:21` — `float m_uiScale = 1.0f;` в секции `private:` (строка 13). Ни одного публичного метода `uiScale()` или `getUiScale()` не существует.

### 2. `ScaleAllSizes` скалирует только ImGui-стиль

`src/app/App.cpp:116` — `ImGui::GetStyle().ScaleAllSizes(m_uiScale); // paddings, spacing, scrollbars`. Метод не затрагивает `ImDrawList` и не делает примитивы канваса DPI-aware.

### 3. `drawPrimitives` не принимает `uiScale`

`src/render/PrimitiveRenderer.cpp:120-121` — сигнатура: `drawPrimitives(ImDrawList*, const RenderPrimitives&, const CanvasCamera&, ImVec2, ImVec2)`. Параметра `uiScale` нет. Структура `Mapper` (строка 13-22) содержит только `camera` и `origin`.

### 4. Screen-space размеры не умножаются на uiScale

| Примитив | Строка | Выражение |
|----------|--------|-----------|
| Particles | `:170` | `screenSpaceRadius ? static_cast<float>(particle.radius) : m.px(...)` — **без `* uiScale`** |
| Lines | `:192` | `screenSpaceWidth ? static_cast<float>(line.width) : m.px(...)` — **без `* uiScale`** |
| Polylines | `:200` | `screenSpaceWidth ? static_cast<float>(poly.width) : m.px(...)` — **без `* uiScale`** |
| Circles | `:215` | `screenSpaceRadius ? static_cast<float>(circle.radius) : m.px(...)` — **без `* uiScale`** |

Все четыре типа примитивов: когда флаг screen-space установлен, значение берётся «как есть» из примитива и ни на что не домножается.

## Источник

**Dear ImGui DPI FAQ** (официальная документация):
> `ScaleAllSizes()` scales only the ImGui style (paddings, spacing, scrollbars, etc.). Custom `ImDrawList` calls (`AddCircle`, `AddLine`, `AddPolyline`, etc.) are NOT automatically DPI-aware — the application must manually scale screen-space sizes and coordinates before passing them to `ImDrawList`.

## Резюме

- `m_uiScale` недоступен рендеру — `App` единственный владелец, рендер не имеет к нему доступа.
- Screen-space размеры примитивов (толщина линий, радиусы частиц/кругов) не масштабируются.
- На 175% мониторе все канвасные примитивы с screen-space-флагами в 1.75× меньше, чем должны быть относительно отмасштабированного ImGui UI.
