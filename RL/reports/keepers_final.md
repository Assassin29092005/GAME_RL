# HellwalkerRL evaluation — keepers_final

- policy: `checkpoints\rl2_keepers\latest.hwrl`
- checkpoint: `checkpoints\rl2_keepers\latest.pt`
- date: 2026-10-02 22:47:37

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.495 | 640 | 0.464 | 640 |
| 6-10 | 0.661 | 640 | 0.581 | 640 |
| 11-15 | 0.683 | 640 | 0.608 | 640 |
| 16-20 | 0.709 | 640 | 0.594 | 640 |
| 21-25 | 0.722 | 640 | 0.662 | 640 |
| 26-30 | 0.659 | 640 | 0.647 | 640 |
| 31-35 | 0.708 | 640 | 0.666 | 640 |
| 36-40 | 0.692 | 640 | 0.662 | 640 |
| 41-45 | 0.678 | 640 | 0.645 | 640 |
| 46-50 | 0.688 | 640 | 0.681 | 640 |
| 51-55 | 0.692 | 640 | 0.666 | 640 |
| 56-60 | 0.700 | 640 | 0.708 | 640 |
| 61-65 | 0.695 | 640 | 0.644 | 640 |
| 66-70 | 0.667 | 640 | 0.625 | 640 |
| 71-75 | 0.616 | 640 | 0.602 | 640 |
| 76-80 | 0.698 | 640 | 0.659 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.197, high alpha 0.203.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.662 | 640 |
| -15..-11 | 0.653 | 640 |
| -10..-6 | 0.675 | 640 |
| -5..-1 | 0.659 | 640 |
| 0..4 | 0.719 | 640 |
| 5..9 | 0.759 | 640 |
| 10..14 | 0.787 | 640 |
| 15..19 | 0.769 | 640 |
| 20..24 | 0.792 | 640 |
| 25..29 | 0.756 | 640 |
| 30..34 | 0.783 | 640 |
| 35..39 | 0.739 | 640 |

before 0.667 → after 0.739 → recovered 0.768

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.620 | 640 |
| -15..-11 | 0.666 | 640 |
| -10..-6 | 0.655 | 640 |
| -5..-1 | 0.645 | 640 |
| 0..4 | 0.637 | 640 |
| 5..9 | 0.675 | 640 |
| 10..14 | 0.656 | 640 |
| 15..19 | 0.691 | 640 |
| 20..24 | 0.684 | 640 |
| 25..29 | 0.700 | 640 |
| 30..34 | 0.658 | 640 |
| 35..39 | 0.672 | 640 |

before 0.650 → after 0.656 → recovered 0.679

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
| Hellwalker (RL) | 1747.1 | 97.2 | 76.7 | 0.712 | 0.00 | 31.51 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 939.3 | 726.8 | 68.1 | 0.526 | 0.00 | 0.00 |
| Hellwalker (classic) | 1086.0 | 528.9 | 66.7 | 0.606 | 0.00 | 5.30 |
| Hellwalker (RL) | 1710.7 | 179.4 | 72.7 | 0.641 | 0.00 | 17.32 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 990.5 | 577.0 | 66.5 | 0.575 | 0.00 | 0.00 |
| Hellwalker (classic) | 1144.0 | 416.5 | 68.8 | 0.647 | 0.00 | 5.78 |
| Hellwalker (RL) | 1717.5 | 119.9 | 76.0 | 0.716 | 0.00 | 26.44 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 945.0 | 668.3 | 67.4 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1140.3 | 496.6 | 67.5 | 0.637 | 0.00 | 8.83 |
| Hellwalker (RL) | 1854.6 | 126.5 | 73.8 | 0.720 | 0.00 | 32.12 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.5 | 488.3 | 66.2 | 0.507 | 0.00 | 0.00 |
| Hellwalker (classic) | 998.7 | 351.5 | 67.6 | 0.577 | 0.00 | 6.19 |
| Hellwalker (RL) | 1582.8 | 105.1 | 73.9 | 0.622 | 0.00 | 22.04 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 618.4 | 261.6 | 62.3 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 834.2 | 224.4 | 66.2 | 0.433 | 0.00 | 6.34 |
| Hellwalker (RL) | 1164.0 | 29.7 | 68.6 | 0.439 | 0.00 | 20.79 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2664.3 | 208.7 | 66.1 | 0.947 | 1.00 | 31.30 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.734 |
| rl_vs_script_dmg_low_alpha | 1.714 |
| classic_vs_script_dmg_low_alpha | 1.193 |
| rl_vs_classic_dmg_low_alpha | 1.437 |
| rl_vs_script_dmg_reference | 1.882 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.883 |

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 1512 | 94 | 72 | 0.59 | 0.01 | 0.70 | 0.11 | 7.34 | 9.41 | 23.32 |
| Sage | 1335 | 92 | 68 | 0.52 | 0.06 | 0.70 | 0.11 | 6.98 | 16.92 | 20.33 |
| Returned | 1390 | 57 | 82 | 0.81 | 0.00 | 0.17 | 0.05 | 2.73 | 4.38 | 51.75 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | PASS |
| returned_reads_most | PASS |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 1.00 | 0.00 | 960 | 178 | 53 | 24 |
| RL | 0.40 | 0.6 | 1.00 | 0.00 | 1129 | 122 | 61 | 20 |
| RL | 0.75 | 0.0 | 1.00 | 0.00 | 1273 | 96 | 65 | 18 |
| RL | 1.00 | 0.0 | 1.00 | 0.00 | 1315 | 95 | 67 | 17 |
| script | — | — | 0.99 | 0.01 | 719 | 391 | 64 | 31 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | FAIL |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | DelayedHeavy 90%, KillerThrust 5% | DelayedHeavy 65%, FastSlash 34% | 0.56 → 0.57 |
| blocker | DelayedHeavy 65%, HeavyCleave 28% | HeavyCleave 68%, DelayedHeavy 20% | 0.23 → 0.40 |
| stepleft | DelayedHeavy 71%, FastSlash 10% | HeavyCleave 34%, DelayedHeavy 23% | 0.15 → 0.45 |
| stepright | DelayedHeavy 69%, HeavyCleave 17% | HeavyCleave 36%, DelayedHeavy 33% | 0.09 → 0.39 |

Move-mix divergence early 0.11 → late 0.46 bits; habit switch at swing #30: 0.54 → 0.30 → 0.38.
