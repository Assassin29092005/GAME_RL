# Unreal vs simulator — parity_nohitstop

- date: 2026-10-03 09:31:27

Same policy, bot, keeper settings, start-distance distribution and 180 s cap; Unreal runs the arena with autoplay (characters, collision, hit-stop), the simulator the training env's 2-D arena. Pooled per-session rates with bootstrap 95% intervals; **gap** = (UE - sim) / sim. A metric is flagged when the gap exceeds its tolerance AND the intervals do not overlap.

## SCRIPT vs rhythm 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 401 [361, 443] | 467 [456, 478] | -14% | ±15% | ok |
| player damage / min | 614 [418, 805] | 380 [354, 406] | +62% | ±20% | **GAP** |
| keeper swings / min | 51.6 [49.8, 53.9] | 51.8 [51.4, 52.3] | -0% | ±10% | ok |
| player swings / min | 19.4 [15.2, 23] | 12.3 [11.5, 13] | +58% | ±15% | **GAP** |
| keeper hit rate | 0.289 [0.269, 0.309] | 0.328 [0.322, 0.333] | -12% | ±10% | **GAP** |
| keeper whiff rate | 0.0976 [0.0864, 0.108] | 0.0985 [0.0963, 0.1] | -1% | ±25% | ok |
| blocked rate | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| parried rate | 0.489 [0.464, 0.512] | 0.486 [0.48, 0.491] | +1% | ±20% | ok |
| read counters / min | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| fight length (s) | 55.6 [50.4, 60.5] | 48.2 [47.1, 49.3] | +15% | ±15% | **GAP** |
| player deaths / fight | 0.944 [0.833, 1] | 0.982 [0.968, 0.992] | -4% | ±10% | ok |
| distance at keeper commits | 212 [209, 214] | 211 [211, 211] | +0% | ±10% | ok |

Keeper move mix: JS divergence 0.000 bits; Unreal FastSlash 24%, Approach 17%, HeavyCleave 10%, SweepLeft 5%; simulator FastSlash 25%, Approach 17%, HeavyCleave 10%, Guard 5%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.704 [0.678, 0.73] | 0.737 [0.731, 0.742] | -4% |
| frames within its attack range (185) | 0.189 [0.132, 0.244] | 0.134 [0.126, 0.142] | +42% |
| ... in range AND able to act | 0.0568 [0.0355, 0.0764] | 0.042 [0.0395, 0.0447] | +35% |
| frames the keeper was open (punish window) | 0.462 [0.438, 0.484] | 0.432 [0.426, 0.438] | +7% |
| ... open AND in range | 0.128 [0.0859, 0.167] | 0.0837 [0.0781, 0.0894] | +52% |
| frames a keeper swing was in flight | 0.268 [0.259, 0.278] | 0.278 [0.276, 0.28] | -4% |
| frames an answer was pending | 0.053 [0.0492, 0.0562] | 0.0544 [0.0538, 0.055] | -2% |
| frames walking in | 0.102 [0.0977, 0.108] | 0.108 [0.107, 0.11] | -5% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0 [0, 0] | 0 [0, 0] | +nan% |
| punish strings begun / min | 4.02 [3.03, 4.98] | 3.25 [3.04, 3.47] | +23% |
| aggression strings begun / min | 5.09 [3.63, 6.36] | 2.94 [2.72, 3.14] | +74% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 19.4 [15.2, 23] | 12.3 [11.5, 13] | +58% |
| mean distance (every frame) | 206 [203, 208] | 206 [206, 207] | -0% |

Unreal wall time 264 s for 18 fights.

## Summary

4 flagged metric(s):

- script vs rhythm 0.7: player damage / min — Unreal 614, simulator 380 (+62%)
- script vs rhythm 0.7: player swings / min — Unreal 19.4, simulator 12.3 (+58%)
- script vs rhythm 0.7: keeper hit rate — Unreal 0.289, simulator 0.328 (-12%)
- script vs rhythm 0.7: fight length (s) — Unreal 55.6, simulator 48.2 (+15%)
