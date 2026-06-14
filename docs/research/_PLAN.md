# Ресёрч-ревью — план континуации (2026-06-14)

Ветка: `research/deep-review-2026-06-14`. Лимит параллелизма поднят до 16 (.mcp.json).

## Готово (закоммичено в worktree)
- A1 солвер/MNA (codex) — wt-res1
- A2 транзиент (codex) — wt-res2
- A3 диод/ключ — на ветке research (commit 68672d2)
- A4 дрейф/Друде (kilo) — wt-res3

## Осталось (12) — запустить ПАРАЛЛЕЛЬНО после реконнекта MCP
| Задача | Агент | Worktree | Файл |
|--------|-------|----------|------|
| A5 поверхностный заряд/E-поле | kilo | wt-res1 | A5-surface-charge-efield.md |
| A6 магнитное поле | kilo | wt-res2 | A6-magnetic-field.md |
| A7 гидроаналогия | kilo | wt-res3 | A7-hydraulic-analogy.md |
| A8 механоаналогия | kilo | wt-res4 | A8-mechanical-analogy.md |
| B1 тепловая модель | kilo | wt-res5 | B1-thermal.md |
| B2 частицы/Box2D | kilo | wt-res6 | B2-particles.md |
| B4 обучающие симы/игры | kilo | wt-res7 | B4-edu-sims-games.md |
| B5 референсы визуализации | kilo | wt-res8 | B5-visual-references.md |
| B7 рендер/визуал баги | kilo | wt-res9 | B7-render-bugs.md |
| B8 наука обучения | kilo | wt-res10 | B8-learning-science.md |
| B6 вода CFD | codex | wt-res11 | B6-water-cfd.md |
| B3 похожие инструменты | claude | wt-res12 | B3-similar-tools.md |

## Метод каждого агента
Читает реальный код/доки темы → ресёрч (веб если есть, иначе код+литература с пометкой) →
пишет docs/research/<файл> с находками [PHYSICS-BUG|DISCREPANCY|VISUAL-BUG|IDEA] + severity +
"Проект:"(file:line) + "Реальность/источники:"(URL) + "Идеал:" → git commit → вернуть резюме.

## После всех 12
1. Собрать все docs/research/*.md из worktree в ветку research (cherry-pick/копирование).
2. Итерация 2 — deep-dive по сильнейшим лидам + перепроверка спорного.
3. Свести docs/RESEARCH_REVIEW_2026-06-14.md по подсистемам с severity и цитатами.
4. Снести wt-res* worktree.
