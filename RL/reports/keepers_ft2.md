# HellwalkerRL evaluation — keepers_ft2

- policy: `RL/checkpoints/rl2_keepers/ft2_1p25e9.hwrl`
- checkpoint: `RL/checkpoints/rl2_keepers/ft2_1p25e9.pt`
- date: 2026-10-03 04:06:58

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.678 | 640 | 0.637 | 640 |
| 6-10 | 0.672 | 640 | 0.572 | 640 |
| 11-15 | 0.716 | 640 | 0.652 | 640 |
| 16-20 | 0.698 | 640 | 0.655 | 640 |
| 21-25 | 0.725 | 640 | 0.689 | 640 |
| 26-30 | 0.713 | 640 | 0.659 | 640 |
| 31-35 | 0.703 | 640 | 0.684 | 640 |
| 36-40 | 0.714 | 640 | 0.661 | 640 |
| 41-45 | 0.713 | 640 | 0.677 | 640 |
| 46-50 | 0.717 | 640 | 0.680 | 640 |
| 51-55 | 0.716 | 640 | 0.659 | 640 |
| 56-60 | 0.738 | 640 | 0.666 | 640 |
| 61-65 | 0.747 | 640 | 0.708 | 640 |
| 66-70 | 0.727 | 640 | 0.711 | 640 |
| 71-75 | 0.698 | 640 | 0.664 | 640 |
| 76-80 | 0.695 | 640 | 0.683 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.039, high alpha 0.034.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.684 | 640 |
| -15..-11 | 0.697 | 640 |
| -10..-6 | 0.700 | 640 |
| -5..-1 | 0.738 | 640 |
| 0..4 | 0.761 | 640 |
| 5..9 | 0.786 | 640 |
| 10..14 | 0.783 | 640 |
| 15..19 | 0.755 | 640 |
| 20..24 | 0.759 | 640 |
| 25..29 | 0.798 | 640 |
| 30..34 | 0.809 | 640 |
| 35..39 | 0.784 | 640 |

before 0.719 → after 0.773 → recovered 0.788

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.666 | 640 |
| -15..-11 | 0.681 | 640 |
| -10..-6 | 0.673 | 640 |
| -5..-1 | 0.683 | 640 |
| 0..4 | 0.692 | 640 |
| 5..9 | 0.695 | 640 |
| 10..14 | 0.719 | 640 |
| 15..19 | 0.731 | 640 |
| 20..24 | 0.703 | 640 |
| 25..29 | 0.722 | 640 |
| 30..34 | 0.725 | 640 |
| 35..39 | 0.753 | 640 |

before 0.678 → after 0.694 → recovered 0.726

| check | result |
|---|---|
| rises_on_habits | FAIL |
| reads_not_strength | FAIL |
| dips_after_switch | FAIL |
| recovers_after_switch | FAIL |

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1019.2 | 497.5 | 66.2 | 0.596 | 0.00 | 0.00 |
| Hellwalker (classic) | 1216.1 | 328.4 | 68.0 | 0.674 | 0.00 | 8.66 |
| Hellwalker (RL) | 1749.2 | 86.8 | 83.8 | 0.737 | 0.00 | 38.93 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 939.3 | 726.8 | 68.1 | 0.526 | 0.00 | 0.00 |
| Hellwalker (classic) | 1086.0 | 528.9 | 66.7 | 0.606 | 0.00 | 5.30 |
| Hellwalker (RL) | 1733.5 | 196.2 | 78.2 | 0.647 | 0.00 | 19.72 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 990.5 | 577.0 | 66.5 | 0.575 | 0.00 | 0.00 |
| Hellwalker (classic) | 1144.0 | 416.5 | 68.8 | 0.647 | 0.00 | 5.78 |
| Hellwalker (RL) | 1780.9 | 116.0 | 83.0 | 0.755 | 0.00 | 34.38 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 945.0 | 668.3 | 67.4 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1140.3 | 496.6 | 67.5 | 0.637 | 0.00 | 8.83 |
| Hellwalker (RL) | 1913.9 | 131.9 | 81.4 | 0.754 | 0.00 | 39.62 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.5 | 488.3 | 66.2 | 0.507 | 0.00 | 0.00 |
| Hellwalker (classic) | 998.7 | 351.5 | 67.6 | 0.577 | 0.00 | 6.19 |
| Hellwalker (RL) | 1714.7 | 93.3 | 80.2 | 0.672 | 0.00 | 26.26 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 618.4 | 261.6 | 62.3 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 834.2 | 224.4 | 66.2 | 0.433 | 0.00 | 6.34 |
| Hellwalker (RL) | 1245.9 | 145.9 | 77.8 | 0.471 | 0.00 | 23.57 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2707.3 | 145.2 | 76.2 | 1.000 | 1.00 | 32.66 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.798 |
| rl_vs_script_dmg_low_alpha | 1.716 |
| classic_vs_script_dmg_low_alpha | 1.193 |
| rl_vs_classic_dmg_low_alpha | 1.438 |
| rl_vs_script_dmg_reference | 2.015 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 2.040 |

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 1560 | 147 | 80 | 0.62 | 0.01 | 0.58 | 0.11 | 6.71 | 8.81 | 27.42 |
| Sage | 1436 | 157 | 77 | 0.57 | 0.03 | 0.59 | 0.11 | 6.32 | 14.93 | 25.33 |
| Returned | 1456 | 106 | 88 | 0.79 | 0.01 | 0.17 | 0.10 | 3.03 | 4.53 | 53.55 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | PASS |
| returned_reads_most | PASS |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 1.00 | 0.00 | 1004 | 222 | 59 | 23 |
| RL | 0.40 | 0.6 | 1.00 | 0.00 | 1170 | 160 | 66 | 19 |
| RL | 0.75 | 0.0 | 1.00 | 0.00 | 1336 | 145 | 73 | 17 |
| RL | 1.00 | 0.0 | 1.00 | 0.00 | 1369 | 130 | 76 | 17 |
| script | — | — | 0.99 | 0.01 | 719 | 391 | 64 | 31 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | FAIL |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | FastSlash 56%, DelayedHeavy 38% | DelayedHeavy 46%, FastSlash 44% | 0.54 → 0.50 |
| blocker | DelayedHeavy 44%, FastSlash 43% | HeavyCleave 69%, DelayedHeavy 19% | 0.33 → 0.41 |
| stepleft | FastSlash 46%, DelayedHeavy 37% | FastSlash 36%, HeavyCleave 29% | 0.39 → 0.51 |
| stepright | FastSlash 42%, DelayedHeavy 36% | HeavyCleave 36%, DelayedHeavy 33% | 0.34 → 0.44 |

Move-mix divergence early 0.04 → late 0.32 bits; habit switch at swing #30: 0.46 → 0.26 → 0.42.
