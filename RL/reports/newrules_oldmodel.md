# HellwalkerRL evaluation — newrules_oldmodel

- policy: `Content/HellwalkerRL/RL/hellwalker_rl.hwrl`
- checkpoint: `—`
- date: 2026-10-03 10:26:05

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 998.3 | 492.0 | 65.7 | 0.591 | 0.00 | 0.00 |
| Hellwalker (classic) | 1169.2 | 334.6 | 67.1 | 0.660 | 0.00 | 8.25 |
| Hellwalker (RL) | 1661.2 | 114.8 | 82.0 | 0.739 | 0.00 | 39.32 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 927.0 | 723.6 | 67.3 | 0.525 | 0.00 | 0.00 |
| Hellwalker (classic) | 1055.7 | 518.2 | 65.5 | 0.597 | 0.00 | 4.90 |
| Hellwalker (RL) | 1654.4 | 219.9 | 75.6 | 0.645 | 0.00 | 19.10 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 961.7 | 594.3 | 66.0 | 0.564 | 0.00 | 0.00 |
| Hellwalker (classic) | 1126.7 | 411.4 | 67.7 | 0.645 | 0.00 | 5.50 |
| Hellwalker (RL) | 1643.2 | 165.5 | 78.7 | 0.712 | 0.00 | 28.97 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 929.2 | 680.4 | 66.3 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1111.6 | 491.5 | 66.2 | 0.637 | 0.00 | 8.31 |
| Hellwalker (RL) | 1825.8 | 161.4 | 76.1 | 0.721 | 0.00 | 31.28 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.8 | 477.7 | 65.0 | 0.514 | 0.00 | 0.00 |
| Hellwalker (classic) | 968.8 | 358.7 | 66.2 | 0.570 | 0.00 | 6.08 |
| Hellwalker (RL) | 1485.7 | 131.3 | 75.1 | 0.596 | 0.00 | 19.72 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 578.6 | 348.6 | 58.8 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 773.7 | 214.1 | 61.6 | 0.435 | 0.00 | 5.51 |
| Hellwalker (RL) | 1093.7 | 104.6 | 68.9 | 0.451 | 0.00 | 17.27 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2664.3 | 208.7 | 66.1 | 0.947 | 1.00 | 31.30 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.709 |
| rl_vs_script_dmg_low_alpha | 1.664 |
| classic_vs_script_dmg_low_alpha | 1.171 |
| rl_vs_classic_dmg_low_alpha | 1.421 |
| rl_vs_script_dmg_reference | 1.890 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.767 |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 84 | 0.99 | 0.01 | 517 | 278 | 38 | 44 |
| RL | 0.40 | 0.6 | — | 1.00 | 0.00 | 1112 | 167 | 62 | 20 |
| RL | 0.75 | 0.0 | — | 1.00 | 0.00 | 1211 | 179 | 68 | 19 |
| RL | 1.00 | 0.0 | — | 1.00 | 0.00 | 1235 | 169 | 71 | 18 |
| script | — | — | — | 0.97 | 0.03 | 701 | 441 | 62 | 32 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | PASS |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | DelayedHeavy 55%, FastSlash 32% | FastSlash 44%, DelayedHeavy 42% | 0.49 → 0.45 |
| blocker | DelayedHeavy 55%, HeavyCleave 22% | HeavyCleave 60%, DelayedHeavy 21% | 0.27 → 0.41 |
| stepleft | DelayedHeavy 62%, FastSlash 23% | HeavyCleave 31%, FastSlash 21% | 0.23 → 0.46 |
| stepright | DelayedHeavy 57%, FastSlash 20% | HeavyCleave 31%, DelayedHeavy 26% | 0.20 → 0.44 |

Move-mix divergence early 0.09 → late 0.44 bits; habit switch at swing #30: 0.43 → 0.25 → 0.39.
