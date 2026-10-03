# HellwalkerRL evaluation — keepers_nr2_2766

- policy: `RL/checkpoints/rl2_newrules2/policy_002766.hwrl`
- checkpoint: `RL/checkpoints/rl2_newrules2/ckpt_002766.pt`
- date: 2026-10-03 18:32:33

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.628 | 640 | 0.611 | 640 |
| 6-10 | 0.661 | 640 | 0.666 | 640 |
| 11-15 | 0.681 | 640 | 0.683 | 640 |
| 16-20 | 0.661 | 640 | 0.706 | 640 |
| 21-25 | 0.703 | 640 | 0.713 | 640 |
| 26-30 | 0.691 | 640 | 0.666 | 640 |
| 31-35 | 0.697 | 640 | 0.716 | 640 |
| 36-40 | 0.708 | 640 | 0.717 | 640 |
| 41-45 | 0.695 | 640 | 0.717 | 640 |
| 46-50 | 0.691 | 640 | 0.728 | 640 |
| 51-55 | 0.711 | 640 | 0.716 | 640 |
| 56-60 | 0.725 | 640 | 0.719 | 640 |
| 61-65 | 0.698 | 640 | 0.736 | 640 |
| 66-70 | 0.691 | 640 | 0.720 | 640 |
| 71-75 | 0.677 | 640 | 0.728 | 640 |
| 76-80 | 0.647 | 640 | 0.728 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.074, high alpha 0.100.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.697 | 640 |
| -15..-11 | 0.728 | 640 |
| -10..-6 | 0.734 | 640 |
| -5..-1 | 0.717 | 640 |
| 0..4 | 0.775 | 640 |
| 5..9 | 0.756 | 640 |
| 10..14 | 0.762 | 640 |
| 15..19 | 0.717 | 640 |
| 20..24 | 0.762 | 640 |
| 25..29 | 0.739 | 640 |
| 30..34 | 0.752 | 640 |
| 35..39 | 0.745 | 640 |

before 0.726 → after 0.766 → recovered 0.750

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.672 | 640 |
| -15..-11 | 0.722 | 640 |
| -10..-6 | 0.675 | 640 |
| -5..-1 | 0.702 | 640 |
| 0..4 | 0.723 | 640 |
| 5..9 | 0.727 | 640 |
| 10..14 | 0.714 | 640 |
| 15..19 | 0.716 | 640 |
| 20..24 | 0.714 | 640 |
| 25..29 | 0.769 | 640 |
| 30..34 | 0.762 | 640 |
| 35..39 | 0.772 | 640 |

before 0.688 → after 0.725 → recovered 0.754

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
| Pathbreaker (script) | 998.3 | 492.0 | 65.7 | 0.591 | 0.00 | 0.00 |
| Hellwalker (classic) | 1169.2 | 334.6 | 67.1 | 0.660 | 0.00 | 8.25 |
| Hellwalker (RL) | 1715.8 | 99.3 | 81.2 | 0.727 | 0.00 | 35.54 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 927.0 | 723.6 | 67.3 | 0.525 | 0.00 | 0.00 |
| Hellwalker (classic) | 1055.7 | 518.2 | 65.5 | 0.597 | 0.00 | 4.90 |
| Hellwalker (RL) | 1677.2 | 210.0 | 76.2 | 0.637 | 0.00 | 17.40 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 961.7 | 594.3 | 66.0 | 0.564 | 0.00 | 0.00 |
| Hellwalker (classic) | 1126.7 | 411.4 | 67.7 | 0.645 | 0.00 | 5.50 |
| Hellwalker (RL) | 1661.1 | 152.0 | 80.7 | 0.752 | 0.00 | 35.24 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 929.2 | 680.4 | 66.3 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1111.6 | 491.5 | 66.2 | 0.637 | 0.00 | 8.31 |
| Hellwalker (RL) | 1843.0 | 147.9 | 78.2 | 0.729 | 0.00 | 33.70 |

**learners**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 840.8 | 477.7 | 65.0 | 0.514 | 0.00 | 0.00 |
| Hellwalker (classic) | 968.8 | 358.7 | 66.2 | 0.570 | 0.00 | 6.08 |
| Hellwalker (RL) | 1534.4 | 145.1 | 75.2 | 0.605 | 0.00 | 22.63 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 578.6 | 348.6 | 58.8 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 773.7 | 214.1 | 61.6 | 0.435 | 0.00 | 5.51 |
| Hellwalker (RL) | 1197.3 | 227.4 | 74.1 | 0.448 | 0.00 | 21.68 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2551.1 | 132.4 | 69.5 | 0.952 | 1.00 | 33.09 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.727 |
| rl_vs_script_dmg_low_alpha | 1.719 |
| classic_vs_script_dmg_low_alpha | 1.171 |
| rl_vs_classic_dmg_low_alpha | 1.467 |
| rl_vs_script_dmg_reference | 2.069 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
| rl_vs_script_dmg_learners | 1.825 |

## The three keepers (one network, the identity input)

| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |
|---|---|---|---|---|---|---|---|---|---|---|
| Warden | 1508 | 181 | 77 | 0.60 | 0.00 | 0.61 | 0.14 | 7.48 | 6.56 | 25.28 |
| Sage | 1410 | 177 | 76 | 0.58 | 0.00 | 0.59 | 0.15 | 6.38 | 12.85 | 24.67 |
| Returned | 1425 | 134 | 86 | 0.79 | 0.00 | 0.18 | 0.13 | 3.34 | 2.64 | 51.86 |

| check | result |
|---|---|
| warden_breaks_most_guards | PASS |
| sage_baits_most | PASS |
| returned_reads_most | PASS |

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 84 | 0.99 | 0.01 | 524 | 175 | 38 | 43 |
| RL | 0.40 | 0.6 | — | 0.99 | 0.01 | 1121 | 198 | 66 | 20 |
| RL | 0.75 | 0.0 | — | 1.00 | 0.00 | 1257 | 201 | 72 | 18 |
| RL | 1.00 | 0.0 | — | 1.00 | 0.00 | 1322 | 177 | 75 | 17 |
| script | — | — | — | 0.97 | 0.03 | 701 | 441 | 62 | 32 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | PASS |

## The reading test (pure habits; clean hits)

| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |
|---|---|---|---|
| parrier | FastSlash 57%, DelayedHeavy 35% | FastSlash 58%, DelayedHeavy 29% | 0.47 → 0.43 |
| blocker | DelayedHeavy 46%, FastSlash 40% | HeavyCleave 54%, DelayedHeavy 27% | 0.32 → 0.43 |
| stepleft | DelayedHeavy 39%, FastSlash 26% | HeavySweepLeft 53%, HeavyCleave 31% | 0.27 → 0.42 |
| stepright | FastSlash 39%, DelayedHeavy 39% | FastSlash 37%, DelayedHeavy 36% | 0.32 → 0.49 |

Move-mix divergence early 0.15 → late 0.63 bits; habit switch at swing #30: 0.38 → 0.27 → 0.40.
