# HellwalkerRL evaluation — keepers_1p28e9

- policy: `RL/checkpoints/rl2_newrules/keeper_1p28e9.hwrl`
- checkpoint: `RL/checkpoints/rl2_newrules/keeper_1p28e9.pt`
- date: 2026-10-03 13:50:07

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.670 | 640 | 0.630 | 640 |
| 6-10 | 0.673 | 640 | 0.655 | 640 |
| 11-15 | 0.716 | 640 | 0.709 | 640 |
| 16-20 | 0.714 | 640 | 0.731 | 640 |
| 21-25 | 0.700 | 640 | 0.684 | 640 |
| 26-30 | 0.667 | 640 | 0.702 | 640 |
| 31-35 | 0.703 | 640 | 0.730 | 640 |
| 36-40 | 0.666 | 640 | 0.759 | 640 |
| 41-45 | 0.702 | 640 | 0.741 | 640 |
| 46-50 | 0.719 | 640 | 0.727 | 640 |
| 51-55 | 0.692 | 640 | 0.739 | 640 |
| 56-60 | 0.680 | 640 | 0.759 | 640 |
| 61-65 | 0.659 | 640 | 0.720 | 640 |
| 66-70 | 0.684 | 640 | 0.717 | 640 |
| 71-75 | 0.664 | 640 | 0.691 | 640 |
| 76-80 | 0.662 | 640 | 0.698 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.021, high alpha 0.100.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.722 | 640 |
| -15..-11 | 0.761 | 640 |
| -10..-6 | 0.709 | 640 |
| -5..-1 | 0.736 | 640 |
| 0..4 | 0.731 | 640 |
| 5..9 | 0.767 | 640 |
| 10..14 | 0.767 | 640 |
| 15..19 | 0.747 | 640 |
| 20..24 | 0.775 | 640 |
| 25..29 | 0.761 | 640 |
| 30..34 | 0.752 | 640 |
| 35..39 | 0.766 | 640 |

before 0.723 → after 0.749 → recovered 0.763

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.705 | 640 |
| -15..-11 | 0.714 | 640 |
| -10..-6 | 0.738 | 640 |
| -5..-1 | 0.714 | 640 |
| 0..4 | 0.695 | 640 |
| 5..9 | 0.742 | 640 |
| 10..14 | 0.738 | 640 |
| 15..19 | 0.761 | 640 |
| 20..24 | 0.728 | 640 |
| 25..29 | 0.738 | 640 |
| 30..34 | 0.747 | 640 |
| 35..39 | 0.722 | 640 |

before 0.726 → after 0.719 → recovered 0.734

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

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 1499 | 210 | 78 | 0.60 | 0.00 | 0.57 | 0.13 | 6.58 | 6.37 | 26.22 |
| Sage | 1399 | 204 | 76 | 0.56 | 0.00 | 0.58 | 0.13 | 6.38 | 13.43 | 23.73 |
| Returned | 1415 | 153 | 86 | 0.78 | 0.00 | 0.19 | 0.12 | 3.29 | 2.51 | 52.10 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | PASS |
| returned_reads_most | PASS |

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
