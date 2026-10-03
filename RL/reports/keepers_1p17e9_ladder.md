# HellwalkerRL evaluation — keepers_1p17e9_ladder

- policy: `RL/checkpoints/rl2_keepers/keeper_1p17e9.hwrl`
- checkpoint: `—`
- date: 2026-10-03 08:39:51

## Difficulty ladder (mortal fights, held-out + reference players)

| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |
|---|---|---|---|---|---|---|---|---|---|
| RL | 0.00 | 1.0 | 84 | 0.99 | 0.01 | 537 | 275 | 38 | 42 |
| RL | 0.40 | 0.6 | — | 1.00 | 0.00 | 1194 | 166 | 65 | 19 |
| RL | 0.75 | 0.0 | — | 1.00 | 0.00 | 1316 | 140 | 72 | 17 |
| RL | 1.00 | 0.0 | — | 0.99 | 0.01 | 1333 | 153 | 75 | 17 |
| script | — | — | — | 0.99 | 0.01 | 719 | 391 | 64 | 31 |

| check | result |
|---|---|
| monotone_damage | PASS |
| easy_below_script | PASS |
