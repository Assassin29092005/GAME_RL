# HellwalkerRL evaluation — keepers_ft

- policy: `checkpoints\rl2_keepers\ft_1p15e9.hwrl`
- checkpoint: `checkpoints\rl2_keepers\ft_1p15e9.pt`
- date: 2026-10-03 03:32:25

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.531 | 640 | 0.495 | 640 |
| 6-10 | 0.714 | 640 | 0.614 | 640 |
| 11-15 | 0.689 | 640 | 0.661 | 640 |
| 16-20 | 0.684 | 640 | 0.664 | 640 |
| 21-25 | 0.681 | 640 | 0.673 | 640 |
| 26-30 | 0.708 | 640 | 0.672 | 640 |
| 31-35 | 0.717 | 640 | 0.661 | 640 |
| 36-40 | 0.675 | 640 | 0.675 | 640 |
| 41-45 | 0.714 | 640 | 0.688 | 640 |
| 46-50 | 0.697 | 640 | 0.713 | 640 |
| 51-55 | 0.694 | 640 | 0.661 | 640 |
| 56-60 | 0.703 | 640 | 0.681 | 640 |
| 61-65 | 0.720 | 640 | 0.714 | 640 |
| 66-70 | 0.659 | 640 | 0.681 | 640 |
| 71-75 | 0.652 | 640 | 0.677 | 640 |
| 76-80 | 0.708 | 640 | 0.702 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.167, high alpha 0.183.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.694 | 640 |
| -15..-11 | 0.709 | 640 |
| -10..-6 | 0.720 | 640 |
| -5..-1 | 0.720 | 640 |
| 0..4 | 0.767 | 640 |
| 5..9 | 0.792 | 640 |
| 10..14 | 0.761 | 640 |
| 15..19 | 0.787 | 640 |
| 20..24 | 0.769 | 640 |
| 25..29 | 0.769 | 640 |
| 30..34 | 0.798 | 640 |
| 35..39 | 0.789 | 640 |

before 0.720 → after 0.780 → recovered 0.781

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.630 | 640 |
| -15..-11 | 0.686 | 640 |
| -10..-6 | 0.689 | 640 |
| -5..-1 | 0.683 | 640 |
| 0..4 | 0.733 | 640 |
| 5..9 | 0.717 | 640 |
| 10..14 | 0.713 | 640 |
| 15..19 | 0.695 | 640 |
| 20..24 | 0.675 | 640 |
| 25..29 | 0.717 | 640 |
| 30..34 | 0.691 | 640 |
| 35..39 | 0.714 | 640 |

before 0.686 → after 0.725 → recovered 0.699

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
| Hellwalker (RL) | 1763.6 | 107.0 | 80.0 | 0.735 | 0.00 | 33.90 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 939.3 | 726.8 | 68.1 | 0.526 | 0.00 | 0.00 |
| Hellwalker (classic) | 1086.0 | 528.9 | 66.7 | 0.606 | 0.00 | 5.30 |
| Hellwalker (RL) | 1714.3 | 180.5 | 75.3 | 0.660 | 0.00 | 19.96 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 990.5 | 577.0 | 66.5 | 0.575 | 0.00 | 0.00 |
| Hellwalker (classic) | 1144.0 | 416.5 | 68.8 | 0.647 | 0.00 | 5.78 |
| Hellwalker (RL) | 1723.5 | 133.2 | 79.4 | 0.730 | 0.00 | 29.53 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 945.0 | 668.3 | 67.4 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1140.3 | 496.6 | 67.5 | 0.637 | 0.00 | 8.83 |
| Hellwalker (RL) | 1861.2 | 136.1 | 76.7 | 0.742 | 0.00 | 34.68 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.5 | 488.3 | 66.2 | 0.507 | 0.00 | 0.00 |
| Hellwalker (classic) | 998.7 | 351.5 | 67.6 | 0.577 | 0.00 | 6.19 |
| Hellwalker (RL) | 1668.5 | 98.2 | 78.7 | 0.682 | 0.00 | 28.74 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 618.4 | 261.6 | 62.3 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 834.2 | 224.4 | 66.2 | 0.433 | 0.00 | 6.34 |
| Hellwalker (RL) | 1178.6 | 28.7 | 70.2 | 0.443 | 0.00 | 20.54 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2664.3 | 208.7 | 66.1 | 0.947 | 1.00 | 31.30 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.740 |
| rl_vs_script_dmg_low_alpha | 1.730 |
| classic_vs_script_dmg_low_alpha | 1.193 |
| rl_vs_classic_dmg_low_alpha | 1.450 |
| rl_vs_script_dmg_reference | 1.906 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.985 |

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 1540 | 99 | 75 | 0.61 | 0.00 | 0.66 | 0.10 | 7.21 | 7.47 | 24.55 |
| Sage | 1399 | 94 | 72 | 0.56 | 0.06 | 0.63 | 0.11 | 6.59 | 15.51 | 23.14 |
| Returned | 1422 | 53 | 85 | 0.81 | 0.00 | 0.18 | 0.08 | 2.97 | 4.04 | 53.25 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | PASS |
| returned_reads_most | PASS |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 1.00 | 0.00 | 977 | 226 | 56 | 23 |
| RL | 0.40 | 0.6 | 1.00 | 0.00 | 1175 | 132 | 63 | 19 |
| RL | 0.75 | 0.0 | 1.00 | 0.00 | 1320 | 97 | 69 | 17 |
| RL | 1.00 | 0.0 | 1.00 | 0.00 | 1342 | 91 | 71 | 17 |
| script | — | — | 0.99 | 0.01 | 719 | 391 | 64 | 31 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | FAIL |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | DelayedHeavy 81%, FastSlash 11% | FastSlash 54%, DelayedHeavy 45% | 0.55 → 0.55 |
| blocker | DelayedHeavy 63%, HeavyCleave 26% | HeavyCleave 65%, DelayedHeavy 23% | 0.24 → 0.39 |
| stepleft | DelayedHeavy 63%, FastSlash 21% | HeavyCleave 36%, HeavySweepLeft 24% | 0.22 → 0.46 |
| stepright | DelayedHeavy 59%, FastSlash 14% | HeavyCleave 40%, DelayedHeavy 24% | 0.17 → 0.41 |

Move-mix divergence early 0.09 → late 0.52 bits; habit switch at swing #30: 0.50 → 0.30 → 0.39.
