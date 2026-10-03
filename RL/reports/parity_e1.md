# Unreal vs simulator — parity_e1

- date: 2026-10-03 08:50:07

Same policy, bot, keeper settings, start-distance distribution and 180 s cap; Unreal runs the arena with autoplay (characters, collision, hit-stop), the simulator the training env's 2-D arena. Pooled per-session rates with bootstrap 95% intervals; **gap** = (UE - sim) / sim. A metric is flagged when the gap exceeds its tolerance AND the intervals do not overlap.

## RL vs habitual 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 12 fights / 4 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 1.03e+03 [1.01e+03, 1.04e+03] | 966 [955, 976] | +6% | ±15% | ok |
| player damage / min | 47.9 [0, 96.1] | 7.43 [4.04, 11.1] | +544% | ±20% | ok |
| keeper swings / min | 72 [70.5, 73.1] | 72.6 [72.5, 72.8] | -1% | ±10% | ok |
| player swings / min | 2.22 [0, 4.45] | 0.344 [0.195, 0.498] | +544% | ±15% | ok |
| keeper hit rate | 0.332 [0.323, 0.344] | 0.311 [0.307, 0.315] | +7% | ±10% | ok |
| keeper whiff rate | 0.378 [0.347, 0.414] | 0.42 [0.415, 0.426] | -10% | ±25% | ok |
| blocked rate | 0.215 [0.181, 0.253] | 0.198 [0.194, 0.202] | +9% | ±20% | ok |
| parried rate | 0.0615 [0.0373, 0.0855] | 0.0694 [0.0653, 0.0739] | -11% | ±20% | ok |
| read counters / min | 17.1 [16.2, 17.9] | 15.8 [15.5, 16.1] | +8% | ±20% | ok |
| fight length (s) | 22.6 [22.3, 22.8] | 23.5 [23.3, 23.8] | -4% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 257 [249, 266] | 263 [262, 264] | -2% | ±10% | ok |

Keeper move mix: JS divergence 0.003 bits; Unreal DelayedHeavy 27%, HeavyCleave 16%, HeavySweepLeft 16%, Approach 14%; simulator DelayedHeavy 24%, HeavySweepLeft 17%, HeavyCleave 17%, Approach 15%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.47 [0.454, 0.482] | 0.454 [0.451, 0.457] | +3% |
| frames within its attack range (185) | 0.0766 [0.00895, 0.145] | 0.0364 [0.0319, 0.0412] | +110% |
| ... in range AND able to act | 0.0245 [0.00233, 0.0469] | 0.00973 [0.0083, 0.0112] | +152% |
| frames the keeper was open (punish window) | 0.062 [0.0427, 0.0819] | 0.0553 [0.0531, 0.0577] | +12% |
| ... open AND in range | 0.00935 [0, 0.0188] | 0.00174 [0.001, 0.00253] | +436% |
| frames a keeper swing was in flight | 0.616 [0.612, 0.621] | 0.626 [0.624, 0.627] | -2% |
| frames an answer was pending | 0.142 [0.133, 0.157] | 0.137 [0.136, 0.139] | +3% |
| frames walking in | 0.198 [0.186, 0.215] | 0.219 [0.218, 0.221] | -10% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0.172 [0.151, 0.186] | 0.162 [0.158, 0.165] | +7% |
| punish strings begun / min | 0.665 [0, 1.35] | 0.106 [0.059, 0.158] | +526% |
| aggression strings begun / min | 0.222 [0, 0.665] | 0.068 [0.0378, 0.107] | +226% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 2.22 [0, 4.45] | 0.344 [0.195, 0.498] | +544% |
| mean distance (every frame) | 235 [227, 243] | 239 [239, 240] | -2% |

Unreal wall time 78 s for 12 fights.

## Summary

Every metric is within tolerance or inside the intervals in every cell: the simulator the keeper was trained in predicts the game.
