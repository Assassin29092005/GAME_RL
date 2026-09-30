# HellwalkerRL evaluation — hellwalker_rl_final

- policy: `../Content/HellwalkerRL/RL/hellwalker_rl.hwrl`
- checkpoint: `checkpoints/rl2_reader/ckpt_000687.pt`
- date: 2026-09-30 23:30:08

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.668 | 5125 | 0.633 | 5120 |
| 6-10 | 0.712 | 5125 | 0.662 | 5120 |
| 11-15 | 0.770 | 5125 | 0.725 | 5120 |
| 16-20 | 0.789 | 5125 | 0.736 | 5120 |
| 21-25 | 0.807 | 5125 | 0.755 | 5120 |
| 26-30 | 0.802 | 5125 | 0.765 | 5120 |
| 31-35 | 0.825 | 5125 | 0.777 | 5120 |
| 36-40 | 0.822 | 5125 | 0.786 | 5120 |
| 41-45 | 0.830 | 5125 | 0.786 | 5120 |
| 46-50 | 0.838 | 5125 | 0.792 | 5120 |
| 51-55 | 0.838 | 5125 | 0.800 | 5120 |
| 56-60 | 0.838 | 5125 | 0.809 | 5120 |
| 61-65 | 0.844 | 5125 | 0.820 | 5120 |
| 66-70 | 0.812 | 5125 | 0.790 | 5120 |
| 71-75 | 0.806 | 5125 | 0.769 | 5120 |
| 76-80 | 0.818 | 5125 | 0.770 | 5120 |

Rise (k 21-60 minus k 1-5): low alpha 0.157, high alpha 0.151.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 1024 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.770 | 5120 |
| -15..-11 | 0.802 | 5120 |
| -10..-6 | 0.822 | 5120 |
| -5..-1 | 0.820 | 5120 |
| 0..4 | 0.801 | 5120 |
| 5..9 | 0.832 | 5120 |
| 10..14 | 0.830 | 5120 |
| 15..19 | 0.851 | 5120 |
| 20..24 | 0.846 | 5120 |
| 25..29 | 0.855 | 5120 |
| 30..34 | 0.853 | 5120 |
| 35..39 | 0.844 | 5120 |

before 0.821 → after 0.816 → recovered 0.849

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 1025 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.715 | 5125 |
| -15..-11 | 0.750 | 5125 |
| -10..-6 | 0.750 | 5125 |
| -5..-1 | 0.772 | 5125 |
| 0..4 | 0.783 | 5125 |
| 5..9 | 0.790 | 5125 |
| 10..14 | 0.793 | 5125 |
| 15..19 | 0.791 | 5125 |
| 20..24 | 0.806 | 5125 |
| 25..29 | 0.801 | 5125 |
| 30..34 | 0.817 | 5125 |
| 35..39 | 0.796 | 5125 |

before 0.761 → after 0.787 → recovered 0.805

| check | result |
|---|---|
| rises_on_habits | PASS |
| reads_not_strength | FAIL |
| dips_after_switch | FAIL |
| recovers_after_switch | PASS |

## Arms on identical seeded players (C++)

**habit_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1019.2 | 497.5 | 66.2 | 0.596 | 0.00 | 0.00 |
| Hellwalker (classic) | 1216.1 | 328.4 | 68.0 | 0.674 | 0.00 | 8.66 |
| Hellwalker (RL) | 1688.4 | 82.5 | 85.2 | 0.848 | 0.00 | 50.49 |

**habit_mid**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 939.3 | 726.8 | 68.1 | 0.526 | 0.00 | 0.00 |
| Hellwalker (classic) | 1086.0 | 528.9 | 66.7 | 0.606 | 0.00 | 5.30 |
| Hellwalker (RL) | 1679.7 | 150.8 | 81.5 | 0.696 | 0.00 | 27.18 |

**habit_high**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 990.5 | 577.0 | 66.5 | 0.575 | 0.00 | 0.00 |
| Hellwalker (classic) | 1144.0 | 416.5 | 68.8 | 0.647 | 0.00 | 5.78 |
| Hellwalker (RL) | 1696.5 | 94.1 | 84.4 | 0.798 | 0.00 | 43.33 |

**switch_low**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 945.0 | 668.3 | 67.4 | 0.554 | 0.00 | 0.00 |
| Hellwalker (classic) | 1140.3 | 496.6 | 67.5 | 0.637 | 0.00 | 8.83 |
| Hellwalker (RL) | 1624.2 | 104.4 | 82.3 | 0.766 | 0.00 | 43.76 |

**reference**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 618.4 | 261.6 | 62.3 | 0.383 | 0.00 | 0.00 |
| Hellwalker (classic) | 834.2 | 224.4 | 66.2 | 0.433 | 0.00 | 6.34 |
| Hellwalker (RL) | 1298.0 | 33.1 | 82.0 | 0.593 | 0.00 | 30.02 |

**masher**

| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |
|---|---|---|---|---|---|---|
| Pathbreaker (script) | 1498.2 | 390.7 | 69.8 | 0.800 | 1.00 | 0.00 |
| Hellwalker (classic) | 1544.1 | 398.4 | 69.1 | 0.794 | 1.00 | 2.03 |
| Hellwalker (RL) | 2701.8 | 0.0 | 69.7 | 1.000 | 1.00 | 36.66 |

| check | value |
|---|---|
| rl_vs_script_dmg_high_alpha | 1.713 |
| rl_vs_script_dmg_low_alpha | 1.657 |
| classic_vs_script_dmg_low_alpha | 1.193 |
| rl_vs_classic_dmg_low_alpha | 1.388 |
| rl_vs_script_dmg_reference | 2.099 |
| not_a_bully | FAIL |
| thesis_like | PASS |
| aggression_floor | PASS |
| net_exchange | PASS |
| masher_lethal | PASS |
