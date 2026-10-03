# HellwalkerRL evaluation — smoke_v3

- policy: `.pip-tmp/rand_v3.hwrl`
- checkpoint: `—`
- date: 2026-10-02 17:50:14

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1078.9 | 790.9 | 67.8 | 0.593 | 0.00 | 0.00 |
| Hellwalker (classic) | 1246.8 | 476.4 | 67.7 | 0.679 | 0.00 | 10.67 |
| Hellwalker (RL) | 661.3 | 233.8 | 46.9 | 0.824 | 0.00 | 0.00 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 988.2 | 675.5 | 68.3 | 0.553 | 0.00 | 0.00 |
| Hellwalker (classic) | 1088.4 | 522.1 | 66.4 | 0.620 | 0.00 | 6.06 |
| Hellwalker (RL) | 623.4 | 145.4 | 44.8 | 0.775 | 0.00 | 0.00 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 971.5 | 575.7 | 65.7 | 0.585 | 0.00 | 0.00 |
| Hellwalker (classic) | 1165.5 | 467.3 | 67.4 | 0.673 | 0.00 | 5.17 |
| Hellwalker (RL) | 482.3 | 120.1 | 33.9 | 0.803 | 0.00 | 0.00 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1047.8 | 475.1 | 65.6 | 0.600 | 0.00 | 0.00 |
| Hellwalker (classic) | 1289.8 | 348.9 | 68.7 | 0.675 | 0.00 | 11.06 |
| Hellwalker (RL) | 903.0 | 35.9 | 57.2 | 0.894 | 0.00 | 0.00 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 808.5 | 445.1 | 66.0 | 0.512 | 0.00 | 0.00 |
| Hellwalker (classic) | 1018.2 | 284.1 | 67.7 | 0.594 | 0.00 | 6.61 |
| Hellwalker (RL) | 562.8 | 101.9 | 39.9 | 0.798 | 0.00 | 0.00 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 623.6 | 250.0 | 62.3 | 0.386 | 0.00 | 0.00 |
| Hellwalker (classic) | 850.3 | 231.0 | 66.6 | 0.441 | 0.00 | 6.61 |
| Hellwalker (RL) | 337.4 | 2.6 | 43.2 | 0.395 | 0.00 | 0.00 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1481.7 | 339.9 | 69.5 | 0.778 | 1.00 | 0.00 |
| Hellwalker (classic) | 1496.0 | 357.2 | 69.0 | 0.765 | 1.00 | 0.00 |
| Hellwalker (RL) | 846.7 | 0.0 | 42.7 | 1.000 | 1.00 | 0.00 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 0.496 |
| rl_vs_script_dmg_low_alpha | 0.613 |
| classic_vs_script_dmg_low_alpha | 1.156 |
| rl_vs_classic_dmg_low_alpha | 0.530 |
| rl_vs_script_dmg_reference | 0.541 |
| not_a_bully | PASS |
| thesis_like | FAIL |
| aggression_floor | FAIL |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 0.696 |

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 399 | 42 | 42 | 0.50 | 0.06 | 0.01 | 0.18 | 2.16 | 0.89 | 0.00 |
| Sage | 63 | 61 | 8 | 0.38 | 0.04 | 0.12 | 0.08 | 0.18 | 0.29 | 0.00 |
| Returned | 39 | 79 | 4 | 0.62 | 0.01 | 0.05 | 0.06 | 0.04 | 0.01 | 0.00 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | FAIL |
| returned_reads_most | PASS |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 0.80 | 0.20 | 294 | 543 | 39 | 70 |
| RL | 0.40 | 0.6 | 0.89 | 0.11 | 383 | 567 | 46 | 57 |
| RL | 0.75 | 0.0 | 0.45 | 0.00 | 130 | 23 | 20 | 124 |
| RL | 1.00 | 0.0 | 0.49 | 0.00 | 142 | 21 | 23 | 122 |
| script | — | — | 0.98 | 0.02 | 614 | 308 | 63 | 36 |

| check | result |
|---|---|
| monotone_damage | FAIL |
| easy_below_script | PASS |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | SweepRight 93%, SweepRightLate 7% | SweepRight 90%, SweepRightLate 8% | 0.31 → 0.30 |
| blocker | SweepRight 62%, SweepRightLate 38% | SweepRight 53%, SweepRightLate 41% | 0.24 → 0.28 |
| stepleft | SweepRight 60%, SweepRightLate 40% | SweepRight 82%, SweepRightLate 18% | 0.26 → 0.41 |
| stepright | SweepRightLate 51%, SweepRight 48% | SweepRightLate 76%, SweepRight 21% | 0.36 → 0.39 |

Move-mix divergence early 0.11 → late 0.26 bits; habit switch at swing #30: 0.26 → 0.31 → 0.36.
