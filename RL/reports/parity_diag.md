# Unreal vs simulator — parity_diag

- date: 2026-10-03 08:46:14

Same policy, bot, keeper settings, start-distance distribution and 180 s cap; Unreal runs the arena with autoplay (characters, collision, hit-stop), the simulator the training env's 2-D arena. Pooled per-session rates with bootstrap 95% intervals; **gap** = (UE - sim) / sim. A metric is flagged when the gap exceeds its tolerance AND the intervals do not overlap.

## RL vs habitual 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 12 fights / 4 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 808 [752, 868] | 966 [955, 976] | -16% | ±15% | **GAP** |
| player damage / min | 383 [292, 463] | 7.43 [4.04, 11.1] | +5055% | ±20% | **GAP** |
| keeper swings / min | 72.5 [70.9, 73.9] | 72.6 [72.5, 72.8] | -0% | ±10% | ok |
| player swings / min | 17.8 [14.4, 21.1] | 0.344 [0.195, 0.498] | +5078% | ±15% | **GAP** |
| keeper hit rate | 0.268 [0.24, 0.3] | 0.311 [0.307, 0.315] | -14% | ±10% | **GAP** |
| keeper whiff rate | 0.391 [0.347, 0.424] | 0.42 [0.415, 0.426] | -7% | ±25% | ok |
| blocked rate | 0.174 [0.14, 0.208] | 0.198 [0.194, 0.202] | -12% | ±20% | ok |
| parried rate | 0.0811 [0.0579, 0.101] | 0.0694 [0.0653, 0.0739] | +17% | ±20% | ok |
| read counters / min | 12.1 [10.8, 13.6] | 15.8 [15.5, 16.1] | -23% | ±20% | **GAP** |
| fight length (s) | 28.1 [26.1, 30.3] | 23.5 [23.3, 23.8] | +19% | ±15% | **GAP** |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 258 [248, 264] | 263 [262, 264] | -2% | ±10% | ok |

Keeper move mix: JS divergence 0.041 bits; Unreal DelayedHeavy 36%, Approach 14%, HeavyCleave 14%, FastSlash 13%; simulator DelayedHeavy 24%, HeavySweepLeft 17%, HeavyCleave 17%, Approach 15%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.42 [0.402, 0.437] | 0.454 [0.451, 0.457] | -8% |
| frames within its attack range (185) | 0.278 [0.216, 0.341] | 0.0364 [0.0319, 0.0412] | +662% |
| ... in range AND able to act | 0.0932 [0.0634, 0.125] | 0.00973 [0.0083, 0.0112] | +857% |
| frames the keeper was open (punish window) | 0.128 [0.104, 0.149] | 0.0553 [0.0531, 0.0577] | +132% |
| ... open AND in range | 0.082 [0.0613, 0.102] | 0.00174 [0.001, 0.00253] | +4602% |
| frames a keeper swing was in flight | 0.582 [0.571, 0.592] | 0.626 [0.624, 0.627] | -7% |
| frames an answer was pending | 0.125 [0.124, 0.126] | 0.137 [0.136, 0.139] | -9% |
| frames walking in | 0.227 [0.206, 0.241] | 0.219 [0.218, 0.221] | +3% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0.154 [0.129, 0.187] | 0.162 [0.158, 0.165] | -5% |
| punish strings begun / min | 4.27 [2.65, 5.72] | 0.106 [0.059, 0.158] | +3926% |
| aggression strings begun / min | 3.56 [2.8, 4.16] | 0.068 [0.0378, 0.107] | +5142% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 17.8 [14.4, 21.1] | 0.344 [0.195, 0.498] | +5078% |
| mean distance (every frame) | 240 [232, 246] | 239 [239, 240] | +0% |

Unreal wall time 101 s for 12 fights.

## RL vs rhythm 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 12 fights / 4 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 1.03e+03 [933, 1.12e+03] | 1.05e+03 [1.02e+03, 1.08e+03] | -2% | ±15% | ok |
| player damage / min | 1.42e+03 [1.22e+03, 1.57e+03] | 627 [565, 690] | +127% | ±20% | **GAP** |
| keeper swings / min | 79.1 [76.8, 83.2] | 68.2 [67.3, 69.2] | +16% | ±10% | **GAP** |
| player swings / min | 55.2 [49.4, 60.1] | 24.3 [22.1, 26.5] | +127% | ±15% | **GAP** |
| keeper hit rate | 0.386 [0.369, 0.408] | 0.435 [0.424, 0.445] | -11% | ±10% | **GAP** |
| keeper whiff rate | 0.071 [0.0646, 0.0757] | 0.0776 [0.0733, 0.0818] | -9% | ±25% | ok |
| blocked rate | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| parried rate | 0.278 [0.242, 0.315] | 0.353 [0.344, 0.361] | -21% | ±20% | **GAP** |
| read counters / min | 16.8 [14.2, 19.4] | 19.8 [19.2, 20.4] | -15% | ±20% | ok |
| fight length (s) | 20.5 [19.8, 21.2] | 21.1 [20.5, 21.7] | -3% | ±15% | ok |
| player deaths / fight | 0.833 [0.667, 1] | 0.955 [0.938, 0.97] | -13% | ±10% | ok |
| distance at keeper commits | 195 [188, 202] | 201 [200, 203] | -3% | ±10% | ok |

Keeper move mix: JS divergence 0.013 bits; Unreal DelayedHeavy 40%, FastSlash 36%, DashIn 6%, Grab 5%; simulator DelayedHeavy 46%, FastSlash 31%, Grab 6%, DashIn 5%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.384 [0.34, 0.426] | 0.562 [0.548, 0.576] | -32% |
| frames within its attack range (185) | 0.672 [0.575, 0.774] | 0.293 [0.269, 0.318] | +129% |
| ... in range AND able to act | 0.169 [0.137, 0.209] | 0.0807 [0.074, 0.088] | +110% |
| frames the keeper was open (punish window) | 0.396 [0.366, 0.436] | 0.373 [0.363, 0.383] | +6% |
| ... open AND in range | 0.317 [0.269, 0.356] | 0.146 [0.131, 0.16] | +117% |
| frames a keeper swing was in flight | 0.41 [0.384, 0.428] | 0.447 [0.441, 0.453] | -8% |
| frames an answer was pending | 0.124 [0.106, 0.138] | 0.138 [0.135, 0.141] | -10% |
| frames walking in | 0.109 [0.101, 0.116] | 0.0962 [0.0933, 0.0991] | +13% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0 [0, 0] | 0 [0, 0] | +nan% |
| punish strings begun / min | 21 [17, 25.2] | 8.53 [7.74, 9.37] | +146% |
| aggression strings begun / min | 5.62 [4.39, 6.86] | 3.2 [2.83, 3.58] | +75% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 55.2 [49.4, 60.1] | 24.3 [22.1, 26.5] | +127% |
| mean distance (every frame) | 189 [183, 194] | 195 [194, 197] | -3% |

Unreal wall time 80 s for 12 fights.

## Summary

11 flagged metric(s):

- rl vs habitual 0.7: keeper damage / min — Unreal 808, simulator 966 (-16%)
- rl vs habitual 0.7: player damage / min — Unreal 383, simulator 7.43 (+5055%)
- rl vs habitual 0.7: player swings / min — Unreal 17.8, simulator 0.344 (+5078%)
- rl vs habitual 0.7: keeper hit rate — Unreal 0.268, simulator 0.311 (-14%)
- rl vs habitual 0.7: read counters / min — Unreal 12.1, simulator 15.8 (-23%)
- rl vs habitual 0.7: fight length (s) — Unreal 28.1, simulator 23.5 (+19%)
- rl vs rhythm 0.7: player damage / min — Unreal 1.42e+03, simulator 627 (+127%)
- rl vs rhythm 0.7: keeper swings / min — Unreal 79.1, simulator 68.2 (+16%)
- rl vs rhythm 0.7: player swings / min — Unreal 55.2, simulator 24.3 (+127%)
- rl vs rhythm 0.7: keeper hit rate — Unreal 0.386, simulator 0.435 (-11%)
- rl vs rhythm 0.7: parried rate — Unreal 0.278, simulator 0.353 (-21%)
