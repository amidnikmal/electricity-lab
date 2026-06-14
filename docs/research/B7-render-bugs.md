# B7: Визуальные/рендер баг-паттерны (ImGui, HiDPI, screen-space)

> Исследование: визуальные баги, расхождения и архитектурные идеи в рендер-конвейере
> `PrimitiveRenderer` ↔ `ProjectionBuilder` ↔ `App` (HiDPI).

## Сводка находок

| # | Тип | Severity | Суть |
|---|-----|----------|------|
| 1 | VISUAL-BUG | **high** | `uiScale` не доходит до screen-space примитивов канваса |
| 2 | VISUAL-BUG | medium | Текстовая отсечка меток — жёсткие 120×16 px |
| 3 | DISCREPANCY | medium | Легенда потенциала — фиксированные px-размеры |
| 4 | IDEA | low | Тесселяция окружностей: кэп 40 сегментов видим при сильном зуме |
| 5 | IDEA | low | `ImDrawFlags_None` у полилиний: нет антиалиасинга |
| 6 | IDEA | low | Дробный `ScaleAllSizes(1.75)` даёт неровные границы UI-элементов |
| 7 | DISCREPANCY | low | `PrimGlow::intensity` влияет на альфу, но кольца жёстко `28 − ring*4` |
| 8 | IDEA | low | Нет сортировки по глубине внутри категории примитивов |
| 9 | IDEA | low | Градиентный кэп 96 сегментов: бэндинг при экстремальном зуме |
| 10 | DISCREPANCY | info | `FontGlobalScale` не используется — растрирование в целых px |

Счётчик: **3 VISUAL-BUG + 4 DISCREPANCY + 6 IDEA = 13** находок (10 основных + 3 вторичных).

---

## 1. [VISUAL-BUG] `uiScale` не доходит до screen-space примитивов

**Severity:** high  
**Проект:** `src/app/App.h:21`, `src/app/App.cpp:49,116`, `src/render/PrimitiveRenderer.cpp:170,192,200,215`  

**Реальность:**
- `App::m_uiScale` — **private** поле, нет геттера (`App.h:21`).
- `ScaleAllSizes(m_uiScale)` в `App.cpp:116` скалирует **только** ImGui-стиль (паддинги, спейсинги, скроллбары).
- Примитивы канваса, у которых `screenSpaceWidth == true` или `screenSpaceRadius == true`, проходят через ветку `static_cast<float>(primitive.radius)` **без умножения на `uiScale`** (`PrimitiveRenderer.cpp:170,192,200,215`).
- На мониторе с 175% scaling (`uiScale = 1.75`): частицы дрейфа, контуры ключей, толщина линий, E-field стрелки, глифы переключателей — **физически в 1.75× меньше**, чем должны быть относительно отмасштабированного UI.

**Источники:**
- `HANDOFF.md:689-690`: «дробный ScaleAllSizes(1.75) + screen-space размеры канвасных примитивов не умножаются на uiScale»
- `CODE_AUDIT_2026-06-14.md:241-242`: «UI грубый, неравномерное скалирование — дробный ScaleAllSizes(1.75) + screen-space примитивы канваса не умножаются на uiScale»
- Dear ImGui FAQ: `ScaleAllSizes` scales only the style; custom `ImDrawList` calls must be manually scaled by the application. No automatic DPI-awareness for `AddCircle`, `AddLine`, etc.

**Идеал:**
1. Добавить геттер `float App::uiScale() const` (или глобальный/синглтон `UiScale`).
2. В `PrimitiveRenderer::drawPrimitives` (или `Mapper`) передавать `uiScale` и умножать screen-space размеры: `r *= uiScale`, `w *= uiScale`.
3. Альтернатива: `ImDrawList` не поддерживает transform — все координаты должны быть домножены вручную до вызова `Add*`.

---

## 2. [VISUAL-BUG] Жёсткая отсечка текстовых меток

**Severity:** medium  
**Проект:** `src/render/PrimitiveRenderer.cpp:235`

```cpp
if (offscreen(p.x, p.y, p.x + 120.0f, p.y + 16.0f)) continue; // ~text extent
```

**Реальность:**
- Отсечка предполагает максимум ~120×16 px на метку — константа, не привязанная ни к шрифту, ни к `uiScale`.
- Длинные тексты (метки узлов, отладочные подписи, значения компонентов) могут быть **обрезаны визуально** или **не нарисованы вообще**, если их bounding box на экране больше 120×16, но частично в кадре.
- При `uiScale = 1.75` шрифт 28 px — метка высотой 28 px, но отсечка предполагает 16 px: метка может мерцать (появляться/исчезать) при панорамировании, когда вершина box'а на границе.

**Источники:**
- Dear ImGui: `ImGui::CalcTextSize()` возвращает реальный размер строки; жёсткая оценка всегда менее точна.
- Знания: типичный паттерн — кэшировать `ImGui::CalcTextSize(label)` на стороне построителя проекции или использовать `ImDrawList::AddText(nullptr, 0.0f, pos, col, text)` для автоизмерения (требует активного шрифта).

**Идеал:**
- Либо передавать в `PrimLabel` предвычисленные `textWidth, textHeight` из построителя проекции.
- Либо использовать `ImDrawList::AddText(ImVec2 clip_min, ImVec2 clip_max, ...)` — ImGui сам отсечёт.
- Минимум: домножить константы на `uiScale / ImGui::GetFontSize()`.

---

## 3. [DISCREPANCY] Легенда потенциала — фиксированные px-размеры

**Severity:** medium  
**Проект:** `src/render/PrimitiveRenderer.cpp:56-88`

```cpp
float barW = 12.0f;
float barH = std::min(150.0f, size.y * 0.4f);
```

**Реальность:**
- `barW = 12.0f`, отступы в px (`barX + barW + 4`, `barY0 - 6`, `barY0 - 18`) — **не скалируются** под `uiScale`.
- При `uiScale = 1.75` шкала легенды и подписи «Potential», «V», значения вольт — в 1.75× меньше относительно UI.
- Текст рендерится через `AddText` с тем шрифтом, что установлен для ImGui (уже отмасштабирован), но координаты — нет → подписи «выезжают» за границы бара.

**Источники:** Прямое чтение кода — константы не параметризованы.

**Идеал:**
- Умножать `barW`, `barH`, отступы на `uiScale`.
- Передавать `uiScale` в `drawLegend()` (или читать из глобального контекста).

---

## 4. [IDEA] Тесселяция окружностей: полигональность при сильном зуме

**Severity:** low  
**Проект:** `src/render/PrimitiveRenderer.cpp:109-118`

```cpp
inline int particleSegs(float r) { return std::clamp(r * 0.8f, 6.0f, 14.0f); }
inline int circleSegs(float r)   { return std::clamp(r * 0.5f, 10.0f, 40.0f); }
```

**Реальность:**
- Кэп 40 сегментов для кругов означает: при `camera.scale = 50×`, окружность радиусом 10 wu → 500 px на экране → `circleSegs(500)` = 40 сегментов → каждый сегмент ~39 px → **видимые углы**.
- ImGui по умолчанию авто-тесселирует круги с ошибкой ~0.3 px (до 512 сегментов) — это и вызвало оригинальный «zoom lag» баг, который кэп исправляет.
- Компромисс: при обычном зуме (scale 0.5–5) кэп адекватен, при экстремальном — проявляется.

**Источники:**
- Dear ImGui `imgui_draw.cpp`: `AddCircle` → `PathArcToFast` → `IM_DRAWLIST_CIRCLE_AUTO_SEGMENT_MAX = 512`, `IM_DRAWLIST_CIRCLE_AUTO_SEGMENT_MIN = 12`, `_CalcCircleAutoSegmentCount` → `IM_TRUNC(radius * 0.6 + error)`, clamped.
- Знания: стандартная техника — динамический кэп: `min(maxAuto, zoomDependentCap)`.

**Идеал:**
- Вместо жёсткого кэпа 40 — адаптивный: `std::min(IM_DRAWLIST_CIRCLE_AUTO_SEGMENT_MAX, static_cast<int>(r * 0.5f))` с потолком, зависящим от `camera.scale`.
- Либо: принять компромисс как осознанный и задокументировать (zoom > 20× — ожидаемая потеря качества).

---

## 5. [IDEA] `ImDrawFlags_None` для AddPolyline — нет антиалиасинга толстых линий

**Severity:** low  
**Проект:** `src/render/PrimitiveRenderer.cpp:211`

```cpp
dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), poly.color, ImDrawFlags_None, w);
```

**Реальность:**
- ImGui включает антиалиасинг для линий толщиной ≤ 1.0 px через флаг `ImDrawFlags_AntiAliasedLines` (текстурированные квады с градиентом).
- Для линий толщиной > 1.0 px ImGui НЕ применяет AA автоматически — рендерятся как геометрия (quad strips) без сглаживания краёв.
- Большинство полилиний в проекте имеют `screenSpaceWidth = true` с толщиной 1.25–4.0 px → потенциально **зубчатые** (aliased) края.
- Особенно заметно на диагональных линиях (E-field линии, индукторные дуги).

**Источники:**
- Dear ImGui `imgui_draw.cpp`: `AddPolyline` → если `thickness > 1.0f`, используется `AddConvexPolyFilled` без текстуры AA.
- Знания: обходной путь — `ImDrawFlags_AntiAliasedLinesUseTex` + ручная настройка UV, или переход на `AddLine` по сегментам (но медленнее).

**Идеал:**
- Для критичных линий (E-field, индуктор, контуры цепи) передавать `ImDrawFlags_AntiAliasedLines` и толщину 1.0, либо рисовать «halo»-слой (более толстая полупрозрачная копия под основной линией).
- Для некритичных: оставить как есть, считать осознанным trade-off производительности.

---

## 6. [IDEA] Дробный `ScaleAllSizes(1.75)` → неровные границы UI

**Severity:** low  
**Проект:** `src/app/App.cpp:116`

```cpp
ImGui::GetStyle().ScaleAllSizes(m_uiScale); // 1.75 → fractional pixels
```

**Реальность:**
- `ScaleAllSizes(1.75)` умножает все стилевые размеры: `FramePadding`, `ItemSpacing`, `WindowPadding`, `ScrollbarSize`, скругления, etc.
- При дробном масштабе (1.25, 1.5, 1.75) многие значения становятся нецелыми (1.75 × 5 px = 8.75 px) → GPU-интерполяция даёт размытие на границах UI-элементов.
- Это часть задокументированной жалобы «UI выглядит грубо/неаккуратно на Windows» (`HANDOFF.md:748`).

**Источники:**
- Dear ImGui issue #1676, #3418: дробное масштабирование стиля — известное ограничение; ImGui не округляет позиции вершин до пиксельной сетки при масштабировании стиля.
- Официальная рекомендация: использовать `FontGlobalScale` + integer-sized стиль, если нужна pixel-perfect отрисовка.

**Идеал:**
- Два подхода (взаимоисключающие):
  - **A (текущий):** Смириться с дробным `ScaleAllSizes` — субпиксельное позиционирование даёт «честный» размер ценой резкости.
  - **B (pixel-perfect):** `FontGlobalScale = m_uiScale`, стиль НЕ скалировать, шрифт рендерить в 16 px и масштабировать GPU. Текст чуть мягче, границы UI — чёткие.
- Текущий подход (A) — разумный компромисс, но требует осознания.

---

## 7. [DISCREPANCY] `PrimGlow::intensity` — жёсткие кольца

**Severity:** low  
**Проект:** `src/render/PrimitiveRenderer.cpp:137-148, 144-147`

```cpp
auto scaleA = [k](int a) { return std::lround(a * k); };
// ...
for (int ring = 1; ring <= 4; ++ring) {
    float rr = r * (0.45f + 0.32f * ring);
    dl->AddCircle(c, rr, withAlpha(glow.color, scaleA(std::max(0, 28 - ring * 4))), 64, 1.0f);
}
```

**Реальность:**
- `glow.intensity` влияет на альфу заливки `AddCircleFilled` (через `scaleA`), но **кольца свечения** используют жёсткую формулу `28 − ring * 4` (значения 24, 20, 16, 12 альфы) — они **одинаковы** для слабого и сильного источника.
- Визуальное различие между glow силой 0.1 и 1.0: отличается только радиус заливки, кольца идентичны.
- `CODE_AUDIT_2026-06-14.md` баг №6 утверждал «интенсивность НЕ читается» — это неточно: читается для `AddCircleFilled`, но не для колец.

**Источники:** Прямое чтение кода — подтверждено.

**Идеал:**
- Умножать альфу колец на `k`: `scaleA(std::max(0, 28 − ring * 4))`.
- Либо убрать кольца, если задумка отменена.

---

## 8. [IDEA] Нет сортировки по глубине внутри категории

**Severity:** low  
**Проект:** `src/render/PrimitiveRenderer.cpp:120-239`

**Реальность:**
- Порядок отрисовки: glows → gradients → filled quads → particles → unfilled quads → lines → polylines → circles → arrows → labels → legend.
- Внутри категории примитивы рисуются в **порядке добавления** (== порядке прохода по компонентам в `ProjectionBuilder`).
- При наложении двух элементов одного типа (например, пересекающиеся провода с градиентами, или пересекающиеся глифы) — тот, что добавлен позже, рисуется поверх.
- Для проводников это правильно (нет физического наложения), но для частиц/стрелок/глифов может давать неверный z-order.

**Источники:**
- Dear ImGui: `ImDrawList` — immediate mode, порядок команд = порядок вызовов. Нет встроенного z-буфера.
- Знания: стандартный подход — либо рисовать в правильном порядке, либо сортировать перед вызовом.

**Идеал:**
- Для текущего масштаба проблемы — не критично, но задокументировать архитектурное ограничение.
- При появлении багов с z-order — добавить `std::stable_sort` по условной глубине.

---

## 9. [IDEA] Градиентный кэп 96 сегментов: бэндинг

**Severity:** low  
**Проект:** `src/render/PrimitiveRenderer.cpp:43`

```cpp
int nSeg = std::max(2, std::min(96, static_cast<int>(len * m.camera.scale / 2.5)));
```

**Реальность:**
- При `camera.scale = 50×` и длине проводника 500 wu → `len * scale / 2.5` = 10000 → кэпится в 96 сегментов → каждый сегмент ~260 px → потенциальный **цветовой бэндинг** (видимые полосы вместо плавного градиента).
- 96 квадов на градиент — разумно для обычного зума, но при экстремальном может проявиться.
- Аналогичная проблема с кэпом кругов, но менее заметна (цветовой градиент vs геометрическая форма).

**Источники:** Прямое чтение кода, тот же класс проблем что и #4.

**Идеал:**
- Динамический кэп: `std::min(256, static_cast<int>(len * m.camera.scale / 2.5))` — не взрывается при обычном зуме, но адаптируется.
- Либо: считать ограничением и задокументировать.

---

## 10. [DISCREPANCY] `FontGlobalScale` не используется

**Severity:** info  
**Проект:** `src/app/App.cpp:69-116`

**Реальность:**
- Текущий подход: `scaledFontSize(16.0f, m_uiScale)` — шрифт растрируется в целое число пикселей (напр. 28 px для 1.75×), затем `ScaleAllSizes(m_uiScale)` скалирует стиль.
- `io.FontGlobalScale` остаётся = 1.0 (значение по умолчанию).
- Альтернативный подход: `FontGlobalScale = m_uiScale`, шрифт 16 px → GPU-масштабирование → текст мягче, UI-стиль остаётся целочисленным → границы чётче.

**Источники:**
- Dear ImGui FAQ: «Rasterizing fonts at the correct size (as you do) gives crisp text; `FontGlobalScale` is a fallback when you cannot control font loading.»
- Знания: текущий подход (растрирование в целых px) — **правильный** для качества текста. Альтернатива ухудшит текст, но улучшит границы UI.

**Идеал:**
- Оставить как есть. Это осознанное решение в пользу читаемости текста (учебное приложение).
- Оформить как архитектурную заметку: «HiDPI-стратегия: растрирование в целых px + дробный ScaleAllSizes».

---

## Дополнительные находки (вторичные)

### 11. [IDEA] Фиксированные 48/64 сегментов для `PrimGlow`
**Проект:** `PrimitiveRenderer.cpp:142-143` — хардкод `48` и `64` сегментов для glow-кругов. Не зависят от радиуса. При большом зуме glow-круги могут быть полигональными.

### 12. [IDEA] Сетка `drawGrid` не учитывает `uiScale`
**Проект:** `PrimitiveRenderer.cpp:93` — `gridSpacing = 50.0f * camera.scale` — spacing в screen px без `uiScale`. Сетка будет мельче чем ожидается на HiDPI.

### 13. [DISCREPANCY] `offscreenSeg` для `PrimGradient` — фиксированный пад 2 px
**Проект:** `PrimitiveRenderer.cpp:152` — `+ 2.0f` константа без учёта `uiScale`. При большом масштабе может давать ложное отсечение.

---

## Источники

| Источник | Тип | Релевантность |
|----------|-----|---------------|
| Dear ImGui `imgui_draw.cpp` (`AddCircle`, `AddPolyline`, `AddLine`) | Исходный код | Тесселяция, антиалиасинг, AutoSegmentCount |
| Dear ImGui FAQ / Wiki (DPI/scaling) | Документация | `ScaleAllSizes`, `FontGlobalScale`, framebuffer vs window size |
| `src/app/UiScale.h` + `App.cpp` | Код проекта | `clampUiScale`, `scaledFontSize`, `ScaleAllSizes(m_uiScale)` |
| `src/render/PrimitiveRenderer.cpp` | Код проекта | Все screen-space пути, тесселяция, отсечение, легенда |
| `src/render/RenderPrimitives.h` | Код проекта | Структуры `screenSpaceWidth`/`screenSpaceRadius` |
| `src/projection/ProjectionBuilder.cpp` | Код проекта | Как заполняются screen-space флаги и размеры |
| `docs/HANDOFF.md:688-751` | Документация | Задокументированные жалобы на «грубый UI» и «неравномерное скалирование» |
| `docs/CODE_AUDIT_2026-06-14.md:241-242` | Аудит | Подтверждение HiDPI-бага в аудите |
| Dear ImGui issue tracker (#1676, #3418, #4003) | Issues | Дробное ScaleAllSizes, HiDPI в GLFW, кастомная отрисовка |
| GLFW documentation (`glfwGetMonitorContentScale`, `glfwGetFramebufferSize`) | API docs | Content scale vs framebuffer size |
