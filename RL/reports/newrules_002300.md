# HellwalkerRL evaluation — newrules_002300

- policy: `RL/checkpoints/rl2_newrules/policy_002300.hwrl`
- checkpoint: `—`
- date: 2026-10-03 13:40:38

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 998.3 | 492.0 | 65.7 | 0.591 | 0.00 | 0.00 |
| Hellwalker (classic) | 1169.2 | 334.6 | 67.1 | 0.660 | 0.00 | 8.25 |
| Hellwalker (RL) | 1648.0 | 146.9 | 85.2 | 0.730 | 0.00 | 40.22 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 927.0 | 723.6 | 67.3 | 0.525 | 0.00 | 0.00 |
| Hellwalker (classic) | 1055.7 | 518.2 | 65.5 | 0.597 | 0.00 | 4.90 |
| Hellwalker (RL) | 1662.2 | 236.5 | 80.9 | 0.660 | 0.00 | 23.39 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 961.7 | 594.3 | 66.0 | 0.564 | 0.00 | 0.00 |
| Hellwalker (classic) | 1126.7 | 411.4 | 67.7 | 0.645 | 0.00 | 5.50 |
| Hellwalker (RL) | 1647.4 | 168.0 | 84.9 | 0.739 | 0.00 | 37.78 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 929.2 | 680.4 | 66.3 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1111.6 | 491.5 | 66.2 | 0.637 | 0.00 | 8.31 |
| Hellwalker (RL) | 1840.2 | 204.5 | 80.8 | 0.736 | 0.00 | 38.12 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.8 | 477.7 | 65.0 | 0.514 | 0.00 | 0.00 |
| Hellwalker (classic) | 968.8 | 358.7 | 66.2 | 0.570 | 0.00 | 6.08 |
| Hellwalker (RL) | 1449.3 | 169.4 | 79.7 | 0.619 | 0.00 | 27.60 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 578.6 | 348.6 | 58.8 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 773.7 | 214.1 | 61.6 | 0.435 | 0.00 | 5.51 |
| Hellwalker (RL) | 1185.9 | 251.9 | 75.3 | 0.468 | 0.00 | 24.11 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2669.6 | 143.1 | 75.1 | 1.000 | 1.00 | 28.63 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.713 |
| rl_vs_script_dmg_low_alpha | 1.651 |
| classic_vs_script_dmg_low_alpha | 1.171 |
| rl_vs_classic_dmg_low_alpha | 1.409 |
| rl_vs_script_dmg_reference | 2.049 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.724 |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 84 | 0.97 | 0.03 | 501 | 284 | 38 | 45 |
| RL | 0.40 | 0.6 | — | 1.00 | 0.00 | 1110 | 220 | 67 | 20 |
| RL | 0.75 | 0.0 | — | 1.00 | 0.00 | 1261 | 193 | 73 | 18 |
| RL | 1.00 | 0.0 | — | 0.99 | 0.01 | 1277 | 199 | 76 | 18 |
| script | — | — | — | 0.97 | 0.03 | 701 | 441 | 62 | 32 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | PASS |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | FastSlash 65%, DelayedHeavy 26% | FastSlash 62%, DelayedHeavy 20% | 0.51 → 0.42 |
| blocker | FastSlash 51%, DelayedHeavy 37% | HeavyCleave 52%, DelayedHeavy 24% | 0.37 → 0.44 |
| stepleft | FastSlash 50%, DelayedHeavy 34% | HeavySweepLeft 44%, HeavyCleave 31% | 0.40 → 0.42 |
| stepright | FastSlash 49%, DelayedHeavy 34% | FastSlash 32%, DelayedHeavy 31% | 0.39 → 0.46 |

Move-mix divergence early 0.07 → late 0.52 bits; habit switch at swing #30: 0.37 → 0.31 → 0.38.
