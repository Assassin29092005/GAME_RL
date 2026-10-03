# HellwalkerRL evaluation — newrules_002450

- policy: `RL/checkpoints/rl2_newrules/policy_002450.hwrl`
- checkpoint: `—`
- date: 2026-10-03 13:41:58

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 998.3 | 492.0 | 65.7 | 0.591 | 0.00 | 0.00 |
| Hellwalker (classic) | 1169.2 | 334.6 | 67.1 | 0.660 | 0.00 | 8.25 |
| Hellwalker (RL) | 1700.3 | 102.6 | 82.8 | 0.715 | 0.00 | 34.89 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 927.0 | 723.6 | 67.3 | 0.525 | 0.00 | 0.00 |
| Hellwalker (classic) | 1055.7 | 518.2 | 65.5 | 0.597 | 0.00 | 4.90 |
| Hellwalker (RL) | 1660.0 | 219.0 | 78.5 | 0.638 | 0.00 | 18.61 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 961.7 | 594.3 | 66.0 | 0.564 | 0.00 | 0.00 |
| Hellwalker (classic) | 1126.7 | 411.4 | 67.7 | 0.645 | 0.00 | 5.50 |
| Hellwalker (RL) | 1643.8 | 152.4 | 81.6 | 0.718 | 0.00 | 31.58 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 929.2 | 680.4 | 66.3 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1111.6 | 491.5 | 66.2 | 0.637 | 0.00 | 8.31 |
| Hellwalker (RL) | 1850.6 | 174.5 | 80.3 | 0.727 | 0.00 | 34.83 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.8 | 477.7 | 65.0 | 0.514 | 0.00 | 0.00 |
| Hellwalker (classic) | 968.8 | 358.7 | 66.2 | 0.570 | 0.00 | 6.08 |
| Hellwalker (RL) | 1567.2 | 148.4 | 77.8 | 0.631 | 0.00 | 24.74 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 578.6 | 348.6 | 58.8 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 773.7 | 214.1 | 61.6 | 0.435 | 0.00 | 5.51 |
| Hellwalker (RL) | 1181.8 | 257.5 | 74.7 | 0.441 | 0.00 | 22.15 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2732.0 | 213.4 | 74.7 | 1.000 | 1.00 | 39.13 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.709 |
| rl_vs_script_dmg_low_alpha | 1.703 |
| classic_vs_script_dmg_low_alpha | 1.171 |
| rl_vs_classic_dmg_low_alpha | 1.454 |
| rl_vs_script_dmg_reference | 2.042 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.864 |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 84 | 0.97 | 0.03 | 505 | 269 | 38 | 44 |
| RL | 0.40 | 0.6 | — | 1.00 | 0.00 | 1134 | 228 | 66 | 20 |
| RL | 0.75 | 0.0 | — | 1.00 | 0.00 | 1246 | 229 | 72 | 18 |
| RL | 1.00 | 0.0 | — | 1.00 | 0.00 | 1310 | 198 | 75 | 17 |
| script | — | — | — | 0.97 | 0.03 | 701 | 441 | 62 | 32 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | PASS |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | FastSlash 60%, DelayedHeavy 32% | FastSlash 61%, DelayedHeavy 25% | 0.48 → 0.43 |
| blocker | FastSlash 53%, DelayedHeavy 36% | HeavyCleave 68%, DelayedHeavy 14% | 0.37 → 0.42 |
| stepleft | FastSlash 51%, DelayedHeavy 30% | HeavySweepLeft 45%, HeavyCleave 38% | 0.43 → 0.42 |
| stepright | FastSlash 57%, DelayedHeavy 28% | FastSlash 34%, HeavyCleave 33% | 0.44 → 0.47 |

Move-mix divergence early 0.06 → late 0.61 bits; habit switch at swing #30: 0.40 → 0.30 → 0.40.
