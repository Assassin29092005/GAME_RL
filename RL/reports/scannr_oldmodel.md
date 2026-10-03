# HellwalkerRL evaluation — scannr_oldmodel

- policy: `RL/checkpoints/rl2_keepers/keeper_1p17e9.hwrl`
- checkpoint: `—`
- date: 2026-10-03 13:38:56

## Adaptation curve (held-out habit players, greedy policy)

P(keeper attack hits) by the index k of the attack in the session:

| k | low alpha (strong habits) | n | high alpha (near random) | n |
|---|---|---|---|---|
| 1-5 | 0.560 | 645 | 0.544 | 640 |
| 6-10 | 0.722 | 645 | 0.627 | 640 |
| 11-15 | 0.704 | 645 | 0.695 | 640 |
| 16-20 | 0.715 | 645 | 0.675 | 640 |
| 21-25 | 0.684 | 645 | 0.709 | 640 |
| 26-30 | 0.691 | 645 | 0.677 | 640 |
| 31-35 | 0.699 | 645 | 0.697 | 640 |
| 36-40 | 0.712 | 645 | 0.681 | 640 |
| 41-45 | 0.724 | 645 | 0.662 | 640 |
| 46-50 | 0.722 | 645 | 0.717 | 640 |
| 51-55 | 0.668 | 645 | 0.688 | 640 |
| 56-60 | 0.704 | 645 | 0.727 | 640 |
| 61-65 | 0.699 | 645 | 0.709 | 640 |
| 66-70 | 0.673 | 645 | 0.650 | 640 |
| 71-75 | 0.676 | 645 | 0.641 | 640 |
| 76-80 | 0.695 | 645 | 0.695 | 640 |

Rise (k 21-60 minus k 1-5): low alpha 0.141, high alpha 0.151.

Around the scheduled habit switch (switch_low_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.722 | 640 |
| -15..-11 | 0.730 | 640 |
| -10..-6 | 0.728 | 640 |
| -5..-1 | 0.761 | 640 |
| 0..4 | 0.770 | 640 |
| 5..9 | 0.787 | 640 |
| 10..14 | 0.761 | 640 |
| 15..19 | 0.739 | 640 |
| 20..24 | 0.764 | 640 |
| 25..29 | 0.748 | 640 |
| 30..34 | 0.789 | 640 |
| 35..39 | 0.780 | 640 |

before 0.745 → after 0.779 → recovered 0.770

Around the scheduled habit switch (switch_high_alpha; offset 0 = first attack answered from table B, 128 sessions reached it):

| offset | P(hit) | n |
|---|---|---|
| -20..-16 | 0.691 | 640 |
| -15..-11 | 0.719 | 640 |
| -10..-6 | 0.681 | 640 |
| -5..-1 | 0.697 | 640 |
| 0..4 | 0.708 | 640 |
| 5..9 | 0.709 | 640 |
| 10..14 | 0.719 | 640 |
| 15..19 | 0.698 | 640 |
| 20..24 | 0.719 | 640 |
| 25..29 | 0.675 | 640 |
| 30..34 | 0.697 | 640 |
| 35..39 | 0.705 | 640 |

before 0.689 → after 0.709 → recovered 0.699

| check | result |
|---|---|
| rises_on_habits | PASS |
| reads_not_strength | FAIL |
| dips_after_switch | FAIL |
| recovers_after_switch | FAIL |
