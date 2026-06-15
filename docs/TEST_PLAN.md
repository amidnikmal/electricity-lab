# План тестирования

> Статус 2026-06-15: тест-план в основном реализован — добавленные в этот проход пункты покрыты (см. tests/test_solver.cpp, tests/test_circuit.cpp, tests/test_canvas.cpp). Незакрытыми остаются «Оставшиеся пробелы» (рендер-снапшоты, smoke-тесты `render/`, тесты `CircuitValidator`, UI-автоматизация). Переведён на русский.

## Проверено на существующем собранном бинарнике

Наблюдаемый доступный тестовый бинарник:

- `./build/current-lab-tests`
- результат: 138 тестов / 10 наборов / pass

Этот прогон отражает заранее собранный артефакт сборки, имеющийся в рабочем дереве.

## Добавлено в этом проходе

Запланированные дополнения на уровне исходников теперь покрывают:

- стабильность ID цепи после удаления (есть: tests/test_circuit.cpp — `AddNodeAfterRemovalKeepsUniqueId`, `AddComponentAfterRemovalKeepsUniqueId`, `RemoveComponentMiddlePreservesOrder`)
- неконтинуальные ID узлов (есть: tests/test_solver.cpp — `NonContiguousNodeIdsAreSolvedCorrectly`)
- сохранение маппинга распределённого источника (есть: tests/test_circuit.cpp — `DistributedPreservesOriginalNodeIdsAcrossGaps`)
- проверку мощности на резисторе для случая `5 V + 1 kOhm` (есть: tests/test_solver.cpp — `SeriesCircuitResistorPowerIs25mW`)
- хелперы масштабирования сопротивления провода (есть: tests/test_solver.cpp — `WireResistanceScalesWithLength`, `SegmentCountDoesNotChangeTotalWireResistance`)
- равенство тока через сегменты распределённого провода (есть: tests/test_solver.cpp — `DistributedWireSegmentsCarrySameSeriesCurrent`)
- направление и масштабирование чистой модели поля (есть: tests/test_canvas.cpp — `FieldDirectionFollowsPotentialDrop`, `FieldMagnitudeScalesWithVoltageAndLength`)
- знак и границы чистой модели дрейфа (есть: tests/test_canvas.cpp — `DriftSpeedIsExactlyZeroAtZeroCurrent`, `DriftSpeedScalesLinearlyWithCurrent`)
- масштабирование и реверс чистого магнитного поля (есть: tests/test_canvas.cpp — `MagneticFieldIncreasesWithCurrentAndDecreasesWithRadius`, `MagneticFieldDirectionReversesWithCurrent`)
- знаковая прогрессия чистого поверхностного заряда (есть: tests/test_canvas.cpp — `SurfaceChargeChangesSignAlongPotentialGradient`)

## Оставшиеся пробелы

- Снапшот-тестов рендерера пока нет
- Нет compile-only smoke-тестов под будущее выделение модуля `render/`
- Нет тестов валидатора на некорректные цепи, потому что `CircuitValidator` ещё не реализован
- Нет UI-автоматизации для пресетов/тултипов

## Следующий шаг тестирования

Ввести промежуточное представление рендер-примитивов и снапшотить его как JSON.
Это даст детерминированные тесты, не зависящие от попиксельных diff-ов GPU.
