# V5 / iter2: Backward Euler демпфирует LC/RLC

ВЕРДИКТ: CONFIRMED

`src/` не менялся. На текущем коде находка подтверждается: live-симуляция по
умолчанию использует Backward Euler, а для осцилляторного LC/RLC авто-скорость
обычно дает `dt ~= T/50`. Для идеального LC это означает численное затухание
амплитуды примерно до `0.676` за один период, без какого-либо физического R.

Источник по методу: Qucs transient integration methods, Backward Euler formula
и A-stability: https://qucs.sourceforge.net/tech/node24.html. Дополнительно:
https://en.wikipedia.org/wiki/Backward_Euler_method.

## Доказательство по коду

- `src/solver/CircuitSolver.h:24-25`: локальный комментарий прямо описывает
  Backward Euler как first-order, A-stable, "never blows up on large dt".
- `src/solver/CircuitSolver.h:51-52`: `stepTransient(..., method =
  IntegrationMethod::BackwardEuler)` имеет BE как default API.
- `src/solver/CircuitSolver.cpp:198-200`: BE companion для C:
  `g = C / dt`, `ieq = g * vOld`.
- `src/solver/CircuitSolver.cpp:209-210`: BE companion для L:
  `g = dt / L`, `ieq = -iOld`.
- `src/solver/CircuitSolver.cpp:194-197` и `:208-210`: даже trapezoidal без
  history делает первый шаг как BE; для дефолта это не исключение, а постоянный
  режим.
- `src/simulation/LiveSim.h:22-37`: дефолты `solveHz = 60`,
  `storySeconds = 2.5`, `method = IntegrationMethod::BackwardEuler`.
- `src/simulation/LiveSim.cpp:69-92`: для цепи с L и C считается период
  `T = 2*pi*sqrt(L*C)` как `oscillationTimescale`.
- `src/simulation/LiveSim.cpp:95-108`: `storyTau = max(tau, osc)`,
  `speed = clamp(3*storyTau/storySeconds, minSpeed, maxSpeed)`.
- `src/simulation/LiveSim.cpp:128-133`: `dt = speed / solveHz`.
- `src/simulation/LiveSim.cpp:172-175`: live-шаг реально вызывает
  `solver.stepTransient(..., m_dt, m_cfg.method)`.
- `tests/test_live_sim.cpp:130-160`: тестовый RLC ожидает именно период звона
  `2*pi*sqrt(LC)` и ту же формулу `3*osc/storySeconds` для live speed.
- `tests/test_transient.cpp:128-165`: energy-тесты проверяют только отдельные
  RC/RL накопители (`1/2*C*V^2`, `1/2*L*I^2`), а не сохранение полной энергии
  свободного LC.
- `tests/test_transient.cpp:188-203`: stability-тест BE проверяет "does not
  blow up" на RC, что не эквивалентно физически без потерь для LC.

## Математика

Идеальный LC в энергетически нормированных координатах

```text
y = [sqrt(C)*v_C, sqrt(L)*i_L]^T
y' = A*y,  A = [[0, -omega], [omega, 0]],  omega = 1/sqrt(L*C)
E = 0.5*||y||^2
```

имеет собственные значения `lambda = +/- i*omega`, поэтому точное решение
вращает `y` без изменения `||y||` и энергии.

Backward Euler для линейной системы:

```text
y_{n+1} = y_n + dt*A*y_{n+1}
y_{n+1} = (I - dt*A)^-1 y_n
```

Если `x = omega*dt`, то матрица шага равна

```text
(I - dt*A)^-1 = 1/(1+x^2) * [[1, -x], [x, 1]]
```

Это поворот плюс сжатие. Поэтому за один шаг:

```text
amplitude_{n+1}/amplitude_n = 1/sqrt(1 + x^2)
energy_{n+1}/energy_n       = 1/(1 + x^2)
```

За один физический период `T = 2*pi/omega`, то есть примерно за
`N = T/dt = 2*pi/x` шагов:

```text
A_period ~= (1 + x^2)^(-N/2) = (1 + x^2)^(-pi/x)
E_period ~= (1 + x^2)^(-N)   = (1 + x^2)^(-2*pi/x)
```

Для обычного не заклампленного live-LC/RLC после `onCircuitEvent()`:

```text
storyTau = T
speed = 3*T / 2.5 = 1.2*T
dt = speed / 60 = T/50
x = omega*dt = (2*pi/T)*(T/50) = 2*pi/50 = 0.125663706
A_period = (1 + x^2)^(-25) ~= 0.676
E_period = A_period^2 ~= 0.457
```

Иными словами, за один период BE оставляет около `67.6%` амплитуды и около
`45.7%` энергии. Это ровно различие `A-stable` и `physically lossless`:
метод не разносит LC, но вносит сильную численную диссипацию.

Оговорка по clamps: если `T >= storySeconds/3 ~= 0.833 s`, `maxSpeed = 1.0`
делает `dt = 1/60 s` и демпфирование за период становится слабее с ростом `T`.
Если `T` попадает в `minTau/minSpeed`-клампы, `omega*dt` надо считать по
фактическому `dt`. Для типичного RLC из `tests/test_live_sim.cpp:130-160`
`T ~= 0.199 s`, клампы не активны, и оценка `dt = T/50`, `A_period ~= 0.676`
применяется напрямую.
