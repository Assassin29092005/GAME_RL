# HellwalkerRL evaluation — keepers_ft2_002225

- policy: `RL/checkpoints/rl2_keepers/policy_002225.hwrl`
- checkpoint: `RL/checkpoints/rl2_keepers/ckpt_002225.pt`
- date: 2026-10-03 04:19:20

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.562 | 640 | 0.545 | 640 |
| 6-10 | 0.719 | 640 | 0.608 | 640 |
| 11-15 | 0.680 | 640 | 0.694 | 640 |
| 16-20 | 0.694 | 640 | 0.681 | 640 |
| 21-25 | 0.656 | 640 | 0.702 | 640 |
| 26-30 | 0.666 | 640 | 0.656 | 640 |
| 31-35 | 0.697 | 640 | 0.711 | 640 |
| 36-40 | 0.694 | 640 | 0.698 | 640 |
| 41-45 | 0.716 | 640 | 0.672 | 640 |
| 46-50 | 0.695 | 640 | 0.725 | 640 |
| 51-55 | 0.652 | 640 | 0.702 | 640 |
| 56-60 | 0.695 | 640 | 0.714 | 640 |
| 61-65 | 0.667 | 640 | 0.703 | 640 |
| 66-70 | 0.642 | 640 | 0.680 | 640 |
| 71-75 | 0.658 | 640 | 0.658 | 640 |
| 76-80 | 0.675 | 640 | 0.717 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.121, high alpha 0.152.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.714 | 640 |
| -15..-11 | 0.714 | 640 |
| -10..-6 | 0.738 | 640 |
| -5..-1 | 0.725 | 640 |
| 0..4 | 0.755 | 640 |
| 5..9 | 0.783 | 640 |
| 10..14 | 0.755 | 640 |
| 15..19 | 0.731 | 640 |
| 20..24 | 0.775 | 640 |
| 25..29 | 0.758 | 640 |
| 30..34 | 0.777 | 640 |
| 35..39 | 0.775 | 640 |

before 0.731 → after 0.769 → recovered 0.771

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.703 | 640 |
| -15..-11 | 0.691 | 640 |
| -10..-6 | 0.662 | 640 |
| -5..-1 | 0.672 | 640 |
| 0..4 | 0.706 | 640 |
| 5..9 | 0.680 | 640 |
| 10..14 | 0.689 | 640 |
| 15..19 | 0.725 | 640 |
| 20..24 | 0.691 | 640 |
| 25..29 | 0.708 | 640 |
| 30..34 | 0.698 | 640 |
| 35..39 | 0.720 | 640 |

before 0.667 → after 0.693 → recovered 0.704

| check | result |
|---|---|
| rises_on_habits | PASS |
| reads_not_strength | FAIL |
| dips_after_switch | FAIL |
| recovers_after_switch | FAIL |

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1019.2 | 497.5 | 66.2 | 0.596 | 0.00 | 0.00 |
| Hellwalker (classic) | 1216.1 | 328.4 | 68.0 | 0.674 | 0.00 | 8.66 |
| Hellwalker (RL) | 1756.4 | 100.5 | 82.7 | 0.740 | 0.00 | 38.01 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 939.3 | 726.8 | 68.1 | 0.526 | 0.00 | 0.00 |
| Hellwalker (classic) | 1086.0 | 528.9 | 66.7 | 0.606 | 0.00 | 5.30 |
| Hellwalker (RL) | 1730.9 | 207.7 | 76.8 | 0.655 | 0.00 | 19.50 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 990.5 | 577.0 | 66.5 | 0.575 | 0.00 | 0.00 |
| Hellwalker (classic) | 1144.0 | 416.5 | 68.8 | 0.647 | 0.00 | 5.78 |
| Hellwalker (RL) | 1734.5 | 155.4 | 80.3 | 0.722 | 0.00 | 29.17 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 945.0 | 668.3 | 67.4 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1140.3 | 496.6 | 67.5 | 0.637 | 0.00 | 8.83 |
| Hellwalker (RL) | 1866.2 | 144.3 | 76.9 | 0.719 | 0.00 | 31.87 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.5 | 488.3 | 66.2 | 0.507 | 0.00 | 0.00 |
| Hellwalker (classic) | 998.7 | 351.5 | 67.6 | 0.577 | 0.00 | 6.19 |
| Hellwalker (RL) | 1642.4 | 117.3 | 77.6 | 0.621 | 0.00 | 21.69 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 618.4 | 261.6 | 62.3 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 834.2 | 224.4 | 66.2 | 0.433 | 0.00 | 6.34 |
| Hellwalker (RL) | 1212.4 | 126.5 | 75.3 | 0.461 | 0.00 | 20.77 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2664.3 | 208.7 | 66.1 | 0.947 | 1.00 | 31.30 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.751 |
| rl_vs_script_dmg_low_alpha | 1.723 |
| classic_vs_script_dmg_low_alpha | 1.193 |
| rl_vs_classic_dmg_low_alpha | 1.444 |
| rl_vs_script_dmg_reference | 1.960 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.954 |

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 1541 | 154 | 77 | 0.60 | 0.00 | 0.61 | 0.10 | 7.10 | 7.99 | 24.77 |
| Sage | 1434 | 157 | 76 | 0.57 | 0.03 | 0.58 | 0.10 | 6.52 | 14.57 | 24.37 |
| Returned | 1444 | 102 | 87 | 0.80 | 0.00 | 0.16 | 0.08 | 3.00 | 3.71 | 53.17 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | PASS |
| returned_reads_most | PASS |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 84 | 0.99 | 0.01 | 538 | 277 | 38 | 42 |
| RL | 0.40 | 0.6 | — | 1.00 | 0.00 | 1194 | 166 | 65 | 19 |
| RL | 0.75 | 0.0 | — | 1.00 | 0.00 | 1316 | 140 | 72 | 17 |
| RL | 1.00 | 0.0 | — | 0.99 | 0.01 | 1333 | 153 | 75 | 17 |
| script | — | — | — | 0.99 | 0.01 | 719 | 391 | 64 | 31 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | PASS |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | DelayedHeavy 54%, FastSlash 36% | FastSlash 59%, DelayedHeavy 36% | 0.51 → 0.53 |
| blocker | DelayedHeavy 55%, HeavyCleave 22% | HeavyCleave 60%, DelayedHeavy 21% | 0.27 → 0.40 |
| stepleft | DelayedHeavy 62%, FastSlash 23% | HeavyCleave 31%, FastSlash 23% | 0.23 → 0.47 |
| stepright | DelayedHeavy 57%, FastSlash 19% | HeavyCleave 32%, DelayedHeavy 24% | 0.20 → 0.44 |

Move-mix divergence early 0.08 → late 0.48 bits; habit switch at swing #30: 0.49 → 0.31 → 0.41.
