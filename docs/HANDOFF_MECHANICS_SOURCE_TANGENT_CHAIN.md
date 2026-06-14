# Handoff: Mechanics Source Tangent Chain

> Исторический хэндоф. Переведён на русский 2026-06-15; факты сохранены. (Работа по source-tangent-chain влита в main: `ChainGeometry.h` `SourceDrivePath`, `ChainSpec::driveSprocket`.)

Дата: 2026-06-14
Ветка: `mechanics-source-tangent-chain`
Worktree: `C:\Users\amidn\electricity-lab-source-sprocket`

## Запрос пользователя

Прошлая попытка ведущего привода источника была отклонена: она уменьшала
шестерню источника, а цепь всё равно выглядела физически приклеенной к шестерне.
Нужная концепция — натянутая цепь как у велосипеда:

- сохранить шестерню источника крупной;
- провести цепь под натяжением по прямым касательным пробегам;
- дать контакт со звёздочкой источника лишь нескольким звеньям;
- сделать контактирующие звенья касательными к питч-окружности звёздочки;
- проверить, что звёздочка источника вращается ВМЕСТЕ с движущейся цепью, а не против неё.

## Реализация

- `src/physics/ChainGeometry.h`
  - добавлен `driveSprocketPitchRadius` с минимальным питч-радиусом `15.0`, чтобы
    шестерню источника нельзя было сделать меньше прежнего видимого кривошипа;
  - добавлен `SourceDrivePath`, построенный из общих внешних касательных между
    концевыми ленивыми звёздочками и центральной ведущей звёздочкой источника;
  - добавлен `sourceDrivePointAt` для общего сэмплинга sim/render;
  - добавлен `sourceDriveSprocketPhaseFromChainTravel`, использующий
    `-chainTravel / pitchRadius` для движения зубьев без проскальзывания.

- `src/physics/ChainSim.*`
  - добавлен `ChainSpec::driveSprocket`;
  - петли источника напряжения используют `SourceDrivePath` вместо старого овала.

- `src/projection/ProjectionBuilder.cpp`
  - рендерит источник как крупную зубчатую ведущую звёздочку;
  - рисует касательные направляющие рельсы и короткие дуги контакта источника;
  - сэмплирует фоллбек-звенья с того же касательного пути;
  - держит направление пластины согласованным с фактическим локальным сегментом цепи.

- `src/ui/MainWindow.cpp`
  - помечает спеки цепи источника напряжения как петли с ведущей звёздочкой.

- `src/ui/CircuitCanvas.cpp`
  - расширяет хит-тест источника до большого радиуса ведущей звёздочки.

- `tests/test_chain_gear.cpp`
  - покрывает крупный размер шестерни источника, истинный касательный контакт,
    короткое огибание у источника, размещение sim-звеньев именно для источника,
    отрисованные касательные звенья и направление вращения.

## Валидация

Команды, запущенные из worktree:

```powershell
cmake --build build-ninja --target current-lab-tests
build-ninja\current-lab-tests.exe --gtest_filter=ChainGeometry.*:ChainSimEngagement.*:MechanicsGears.*:MechanicsChain.*:ChainSim.*:MechanicsMapping.*:MechanicsProjection.*:TripleView.*
build-ninja\current-lab-tests.exe
cmake --build build-ninja --target current-lab
```

Результаты:

- целевое подмножество механика/цепь: 41 пройден;
- полный набор: 476 пройдено;
- `current-lab.exe` собрался успешно.

Остаются известные несвязанные предупреждения в `src/ui/I18n.cpp` и
`src/projection/ProjectionBuilder.cpp` по существующему escape `\xC2\xB5F`.
