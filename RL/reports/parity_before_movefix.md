# Unreal vs simulator — parity

- date: 2026-10-03 04:59:00

Same policy, bot, keeper settings, start-distance distribution and 180 s cap; Unreal runs the arena with autoplay (characters, collision, hit-stop), the simulator the training env's 2-D arena. Pooled per-session rates with bootstrap 95% intervals; **gap** = (UE - sim) / sim. A metric is flagged when the gap exceeds its tolerance AND the intervals do not overlap.

## RL vs rhythm 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 951 [854, 1.06e+03] | 1.05e+03 [1.02e+03, 1.08e+03] | -10% | ±15% | ok |
| player damage / min | 1.55e+03 [1.34e+03, 1.8e+03] | 627 [565, 690] | +147% | ±20% | **GAP** |
| keeper swings / min | 79.8 [77.7, 82.3] | 68.2 [67.3, 69.2] | +17% | ±10% | **GAP** |
| player swings / min | 58.6 [52.5, 66.8] | 24.3 [22.1, 26.5] | +141% | ±15% | **GAP** |
| keeper hit rate | 0.363 [0.338, 0.39] | 0.435 [0.424, 0.445] | -16% | ±10% | **GAP** |
| keeper whiff rate | 0.0794 [0.0685, 0.0927] | 0.0776 [0.0733, 0.0818] | +2% | ±25% | ok |
| blocked rate | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| parried rate | 0.274 [0.253, 0.297] | 0.353 [0.344, 0.361] | -22% | ±20% | **GAP** |
| read counters / min | 14.8 [11.7, 17.8] | 19.8 [19.2, 20.4] | -25% | ±20% | **GAP** |
| fight length (s) | 22.1 [20.3, 24.7] | 21.1 [20.5, 21.7] | +5% | ±15% | ok |
| player deaths / fight | 0.833 [0.722, 0.944] | 0.955 [0.938, 0.97] | -13% | ±10% | ok |
| distance at keeper commits | 187 [178, 197] | 201 [200, 203] | -7% | ±10% | ok |

Keeper move mix: JS divergence 0.015 bits; Unreal FastSlash 38%, DelayedHeavy 38%, Grab 7%, DashIn 6%; simulator DelayedHeavy 46%, FastSlash 31%, Grab 6%, DashIn 5%

Unreal wall time 143 s for 18 fights.

## SCRIPT vs rhythm 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 499 [398, 591] | 467 [456, 478] | +7% | ±15% | ok |
| player damage / min | 1.44e+03 [1.31e+03, 1.58e+03] | 380 [354, 406] | +280% | ±20% | **GAP** |
| keeper swings / min | 64.1 [61.6, 66.5] | 51.8 [51.4, 52.3] | +24% | ±10% | **GAP** |
| player swings / min | 61.9 [57.4, 66.3] | 12.3 [11.5, 13] | +404% | ±15% | **GAP** |
| keeper hit rate | 0.306 [0.257, 0.349] | 0.328 [0.322, 0.333] | -6% | ±10% | ok |
| keeper whiff rate | 0.104 [0.0953, 0.115] | 0.0985 [0.0963, 0.1] | +5% | ±25% | ok |
| blocked rate | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| parried rate | 0.318 [0.292, 0.347] | 0.486 [0.48, 0.491] | -35% | ±20% | **GAP** |
| read counters / min | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| fight length (s) | 39.1 [36.1, 42] | 48.2 [47.1, 49.3] | -19% | ±15% | **GAP** |
| player deaths / fight | 0.611 [0.333, 0.889] | 0.982 [0.968, 0.992] | -38% | ±10% | **GAP** |
| distance at keeper commits | 207 [204, 210] | 211 [211, 211] | -2% | ±10% | ok |

Keeper move mix: JS divergence 0.001 bits; Unreal FastSlash 24%, Approach 20%, HeavyCleave 9%, SweepLeft 5%; simulator FastSlash 25%, Approach 17%, HeavyCleave 10%, Guard 5%

Unreal wall time 266 s for 18 fights.

## RL vs habitual 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 820 [776, 863] | 966 [955, 976] | -15% | ±15% | **GAP** |
| player damage / min | 347 [274, 421] | 7.43 [4.04, 11.1] | +4568% | ±20% | **GAP** |
| keeper swings / min | 72.4 [71.3, 73.4] | 72.6 [72.5, 72.8] | -0% | ±10% | ok |
| player swings / min | 16.1 [13, 19.3] | 0.344 [0.195, 0.498] | +4573% | ±15% | **GAP** |
| keeper hit rate | 0.27 [0.251, 0.292] | 0.311 [0.307, 0.315] | -13% | ±10% | **GAP** |
| keeper whiff rate | 0.401 [0.365, 0.435] | 0.42 [0.415, 0.426] | -5% | ±25% | ok |
| blocked rate | 0.174 [0.142, 0.207] | 0.198 [0.194, 0.202] | -12% | ±20% | ok |
| parried rate | 0.0718 [0.054, 0.0903] | 0.0694 [0.0653, 0.0739] | +3% | ±20% | ok |
| read counters / min | 12.8 [11.4, 14.1] | 15.8 [15.5, 16.1] | -19% | ±20% | ok |
| fight length (s) | 27.6 [26.2, 29.2] | 23.5 [23.3, 23.8] | +17% | ±15% | **GAP** |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 260 [253, 267] | 263 [262, 264] | -1% | ±10% | ok |

Keeper move mix: JS divergence 0.030 bits; Unreal DelayedHeavy 35%, Approach 14%, HeavyCleave 14%, FastSlash 12%; simulator DelayedHeavy 24%, HeavySweepLeft 17%, HeavyCleave 17%, Approach 15%

Unreal wall time 144 s for 18 fights.

## SCRIPT vs habitual 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 461 [422, 510] | 441 [433, 448] | +4% | ±15% | ok |
| player damage / min | 1.03e+03 [908, 1.18e+03] | 221 [209, 232] | +365% | ±20% | **GAP** |
| keeper swings / min | 66.9 [65.3, 68.6] | 63.8 [63.6, 64] | +5% | ±10% | ok |
| player swings / min | 47.5 [42, 54.9] | 9.15 [8.65, 9.64] | +418% | ±15% | **GAP** |
| keeper hit rate | 0.263 [0.24, 0.292] | 0.275 [0.271, 0.279] | -4% | ±10% | ok |
| keeper whiff rate | 0.287 [0.259, 0.311] | 0.334 [0.33, 0.338] | -14% | ±25% | ok |
| blocked rate | 0.0266 [0.0185, 0.0332] | 0.04 [0.0379, 0.0421] | -33% | ±20% | **GAP** |
| parried rate | 0.185 [0.167, 0.208] | 0.274 [0.27, 0.278] | -32% | ±20% | **GAP** |
| read counters / min | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| fight length (s) | 46.9 [42.6, 51.4] | 50.9 [50, 51.8] | -8% | ±15% | ok |
| player deaths / fight | 0.778 [0.667, 0.889] | 1 [1, 1] | -22% | ±10% | **GAP** |
| distance at keeper commits | 215 [211, 219] | 220 [219, 220] | -2% | ±10% | ok |

Keeper move mix: JS divergence 0.001 bits; Unreal FastSlash 24%, Approach 19%, HeavyCleave 9%, SweepLeft 5%; simulator FastSlash 25%, Approach 17%, HeavyCleave 10%, SweepLeft 5%

Unreal wall time 249 s for 18 fights.

## RL vs varied 0.5 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 1.13e+03 [1.07e+03, 1.2e+03] | 1.19e+03 [1.18e+03, 1.2e+03] | -5% | ±15% | ok |
| player damage / min | 200 [172, 230] | 12.6 [7.84, 17.5] | +1487% | ±20% | **GAP** |
| keeper swings / min | 75.4 [73.3, 78] | 79.6 [79.2, 80] | -5% | ±10% | ok |
| player swings / min | 10.4 [8.08, 13] | 0.683 [0.455, 0.921] | +1428% | ±15% | **GAP** |
| keeper hit rate | 0.512 [0.453, 0.579] | 0.55 [0.542, 0.559] | -7% | ±10% | ok |
| keeper whiff rate | 0.255 [0.236, 0.273] | 0.247 [0.241, 0.254] | +3% | ±25% | ok |
| blocked rate | 0.105 [0.0713, 0.143] | 0.105 [0.0994, 0.11] | +1% | ±20% | ok |
| parried rate | 0.0967 [0.0852, 0.109] | 0.0958 [0.0916, 0.1] | +1% | ±20% | ok |
| read counters / min | 17.9 [11, 25.3] | 18.1 [16.9, 19.4] | -1% | ±20% | ok |
| fight length (s) | 20.1 [18.9, 21.1] | 19 [18.8, 19.2] | +6% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 246 [243, 250] | 244 [243, 245] | +1% | ±10% | ok |

Keeper move mix: JS divergence 0.027 bits; Unreal FastSlash 31%, DelayedHeavy 31%, HeavyCleave 9%, Approach 7%; simulator FastSlash 37%, DelayedHeavy 27%, HeavyCleave 8%, Approach 7%

Unreal wall time 107 s for 18 fights.

## SCRIPT vs varied 0.5 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 686 [644, 748] | 729 [718, 741] | -6% | ±15% | ok |
| player damage / min | 584 [536, 627] | 170 [158, 182] | +244% | ±20% | **GAP** |
| keeper swings / min | 63.7 [62.3, 65] | 65.4 [65.3, 65.6] | -3% | ±10% | ok |
| player swings / min | 26.1 [24.2, 28] | 7.05 [6.56, 7.57] | +270% | ±15% | **GAP** |
| keeper hit rate | 0.443 [0.412, 0.488] | 0.473 [0.467, 0.479] | -6% | ±10% | ok |
| keeper whiff rate | 0.284 [0.239, 0.319] | 0.288 [0.282, 0.294] | -1% | ±25% | ok |
| blocked rate | 0.0674 [0.0514, 0.0807] | 0.0845 [0.0805, 0.088] | -20% | ±20% | ok |
| parried rate | 0.0819 [0.0571, 0.114] | 0.0998 [0.0958, 0.104] | -18% | ±20% | ok |
| read counters / min | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| fight length (s) | 32.6 [29.8, 34.9] | 30.7 [30.2, 31.2] | +6% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 231 [226, 235] | 233 [232, 234] | -1% | ±10% | ok |

Keeper move mix: JS divergence 0.002 bits; Unreal FastSlash 24%, Approach 21%, HeavyCleave 9%, SweepLeft 5%; simulator FastSlash 25%, Approach 18%, HeavyCleave 9%, SweepLeft 5%

Unreal wall time 168 s for 18 fights.

## Summary

26 flagged metric(s):

- rl vs rhythm 0.7: player damage / min — Unreal 1.55e+03, simulator 627 (+147%)
- rl vs rhythm 0.7: keeper swings / min — Unreal 79.8, simulator 68.2 (+17%)
- rl vs rhythm 0.7: player swings / min — Unreal 58.6, simulator 24.3 (+141%)
- rl vs rhythm 0.7: keeper hit rate — Unreal 0.363, simulator 0.435 (-16%)
- rl vs rhythm 0.7: parried rate — Unreal 0.274, simulator 0.353 (-22%)
- rl vs rhythm 0.7: read counters / min — Unreal 14.8, simulator 19.8 (-25%)
- script vs rhythm 0.7: player damage / min — Unreal 1.44e+03, simulator 380 (+280%)
- script vs rhythm 0.7: keeper swings / min — Unreal 64.1, simulator 51.8 (+24%)
- script vs rhythm 0.7: player swings / min — Unreal 61.9, simulator 12.3 (+404%)
- script vs rhythm 0.7: parried rate — Unreal 0.318, simulator 0.486 (-35%)
- script vs rhythm 0.7: fight length (s) — Unreal 39.1, simulator 48.2 (-19%)
- script vs rhythm 0.7: player deaths / fight — Unreal 0.611, simulator 0.982 (-38%)
- rl vs habitual 0.7: keeper damage / min — Unreal 820, simulator 966 (-15%)
- rl vs habitual 0.7: player damage / min — Unreal 347, simulator 7.43 (+4568%)
- rl vs habitual 0.7: player swings / min — Unreal 16.1, simulator 0.344 (+4573%)
- rl vs habitual 0.7: keeper hit rate — Unreal 0.27, simulator 0.311 (-13%)
- rl vs habitual 0.7: fight length (s) — Unreal 27.6, simulator 23.5 (+17%)
- script vs habitual 0.7: player damage / min — Unreal 1.03e+03, simulator 221 (+365%)
- script vs habitual 0.7: player swings / min — Unreal 47.5, simulator 9.15 (+418%)
- script vs habitual 0.7: blocked rate — Unreal 0.0266, simulator 0.04 (-33%)
- script vs habitual 0.7: parried rate — Unreal 0.185, simulator 0.274 (-32%)
- script vs habitual 0.7: player deaths / fight — Unreal 0.778, simulator 1 (-22%)
- rl vs varied 0.5: player damage / min — Unreal 200, simulator 12.6 (+1487%)
- rl vs varied 0.5: player swings / min — Unreal 10.4, simulator 0.683 (+1428%)
- script vs varied 0.5: player damage / min — Unreal 584, simulator 170 (+244%)
- script vs varied 0.5: player swings / min — Unreal 26.1, simulator 7.05 (+270%)
