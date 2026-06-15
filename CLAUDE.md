# CLAUDE.md — заметки для агентов в репозитории electricity-lab

Учебное приложение по электричеству на C++ / OpenGL / Dear ImGui: цепь считается
честным MNA-солвером, поверх рисуются физические слои (потенциал, поле, дрейф,
поверхностный заряд, магнитное поле, тепло) + аналогии (механика, вода).

## Сборка и тесты

- **Windows (основное окружение):** Ninja + MinGW (UCRT).
  ```
  cmake -S . -B build -G Ninja
  cmake --build build -j
  ./build/current-lab-tests.exe        # тесты (gtest)
  ./build/current-lab.exe              # приложение
  ```
- Линукс: `cmake -S . -B build && cmake --build build -j$(nproc)`.
- Зависимости тянутся через FetchContent (box2d, imgui, glfw, gtest, nlohmann_json).
  Первый конфиг долгий (~1.5 мин). НЕ создавай build-дерево в каждом worktree —
  собирай централизованно в одном `build/` (инкрементально, тёплый кэш).
- C++20, GCC 16 / MinGW. `std::format` доступен. GLM удалён (использовался `Vec2`).

## Рабочий процесс (важно)

- **Коммиты и комментарии — на русском**, кусками; в конце блока работы обновляй
  `docs/HANDOFF.md` (свежая секция сверху, читается первой).
- Сообщения коммитов заканчивай:
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- Правки файлов делай инструментами Edit/Write, не через PowerShell-эхо.
- `current-lab.exe` иногда залочен (запущен) — тогда собирается только тест-таргет.
- **Ветка `main` обычно занята отдельным worktree** `C:/Users/amidn/electricity-lab`.
  Мёрж туда: `git -C C:/Users/amidn/electricity-lab merge --ff-only <ветка>`, затем push.
  Пушить/мёржить — только по просьбе пользователя или в конце блока.

## Архитектура (поток данных)

```
Circuit -> Circuit::toDistributed -> CircuitSolver (DC + transient companion)
        -> physics/* чистые модели -> CircuitCanvas -> MainWindow/InspectorPanel
```
Ключевое: `src/circuit/Circuit.h`, `src/solver/CircuitSolver.{h,cpp}`,
`src/simulation/LiveSim.{h,cpp}` (единая live-модель, DC = предел transient),
`src/physics/*Model.h` (чистые, тестируемые), `src/render/ColorMaps.h` (viridis),
`src/projection/*` (механика/проекции), `src/learning/*` (учебный модуль).

## Уроки / подводные камни (не наступать дважды)

- **Бэклог `docs/RESEARCH_REVIEW_2026-06-14.md` УСТАРЕЛ** — многое уже починено
  в ветке `fix/research-p0p1` (в main). Перед задачей оттуда сверяйся с
  `docs/RESEARCH_BACKLOG_STATUS_2026-06-15.md` и с `origin/main`
  (`git show origin/main:<файл>`), а НЕ с веткой `claude/analysis` (она отстаёт).
- `src/physics/HydraulicSim.cpp` — **карантин** (баги №8/№12, судьба файла не
  решена). Не трогать без решения пользователя.
- Многоагентный веер: запускай агентов в отдельных worktree ИЛИ на непересекающихся
  файлах без git, коммить централизованно после ревью реального диффа (не пересказа
  агента). Сеть бывает рвёт агентов — доделывай инлайн.
- ID узлов/компонентов НЕ непрерывны (`id != индекс`) — не предполагай обратное.

## Что НЕ сделано (настоящие задачи, см. backlog-статус)

AC-источник; `LinearSolveResult` (статус сингулярности самого линейного солвера);
Gear2 / дефолт-интегратор для LC; подключение `CircuitValidator` к solve/UI;
prediction-вопросы под заблуждения (B8); LIC + bloom рендер.
