# Unreal vs simulator — parity

- date: 2026-10-03 09:24:12

Same policy, bot, keeper settings, start-distance distribution and 180 s cap; Unreal runs the arena with autoplay (characters, collision, hit-stop), the simulator the training env's 2-D arena. Pooled per-session rates with bootstrap 95% intervals; **gap** = (UE - sim) / sim. A metric is flagged when the gap exceeds its tolerance AND the intervals do not overlap.

## RL vs rhythm 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 1.09e+03 [1e+03, 1.22e+03] | 1.05e+03 [1.02e+03, 1.08e+03] | +4% | ±15% | ok |
| player damage / min | 586 [367, 767] | 627 [565, 690] | -6% | ±20% | ok |
| keeper swings / min | 71 [67.1, 75.8] | 68.2 [67.3, 69.2] | +4% | ±10% | ok |
| player swings / min | 23.3 [15.6, 31.1] | 24.3 [22.1, 26.5] | -4% | ±15% | ok |
| keeper hit rate | 0.433 [0.402, 0.466] | 0.435 [0.424, 0.445] | -0% | ±10% | ok |
| keeper whiff rate | 0.0998 [0.0855, 0.116] | 0.0776 [0.0733, 0.0818] | +29% | ±25% | **GAP** |
| blocked rate | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| parried rate | 0.34 [0.304, 0.374] | 0.353 [0.344, 0.361] | -4% | ±20% | ok |
| read counters / min | 19.8 [16.7, 23.5] | 19.8 [19.2, 20.4] | +0% | ±20% | ok |
| fight length (s) | 20.7 [18.5, 22.4] | 21.1 [20.5, 21.7] | -2% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 0.955 [0.938, 0.97] | +5% | ±10% | ok |
| distance at keeper commits | 203 [197, 211] | 201 [200, 203] | +1% | ±10% | ok |

Keeper move mix: JS divergence 0.003 bits; Unreal DelayedHeavy 43%, FastSlash 30%, Grab 8%, DashIn 6%; simulator DelayedHeavy 46%, FastSlash 31%, Grab 6%, DashIn 5%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.541 [0.502, 0.586] | 0.562 [0.548, 0.576] | -4% |
| frames within its attack range (185) | 0.287 [0.22, 0.36] | 0.293 [0.269, 0.318] | -2% |
| ... in range AND able to act | 0.0721 [0.056, 0.0914] | 0.0807 [0.074, 0.088] | -11% |
| frames the keeper was open (punish window) | 0.342 [0.293, 0.377] | 0.373 [0.363, 0.383] | -8% |
| ... open AND in range | 0.128 [0.0834, 0.168] | 0.146 [0.131, 0.16] | -12% |
| frames a keeper swing was in flight | 0.466 [0.449, 0.489] | 0.447 [0.441, 0.453] | +4% |
| frames an answer was pending | 0.142 [0.136, 0.151] | 0.138 [0.135, 0.141] | +3% |
| frames walking in | 0.104 [0.0867, 0.125] | 0.0962 [0.0933, 0.0991] | +8% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0 [0, 0] | 0 [0, 0] | +nan% |
| punish strings begun / min | 7.73 [4.7, 10.6] | 8.53 [7.74, 9.37] | -9% |
| aggression strings begun / min | 3.7 [2.49, 4.88] | 3.2 [2.83, 3.58] | +16% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 23.3 [15.6, 31.1] | 24.3 [22.1, 26.5] | -4% |
| mean distance (every frame) | 200 [194, 206] | 195 [194, 197] | +2% |

Unreal wall time 114 s for 18 fights.

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

Unreal wall time 277 s for 18 fights.

## RL vs habitual 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 1e+03 [961, 1.03e+03] | 966 [955, 976] | +4% | ±15% | ok |
| player damage / min | 31.5 [0, 64.3] | 7.43 [4.04, 11.1] | +324% | ±20% | ok |
| keeper swings / min | 72 [71, 72.8] | 72.6 [72.5, 72.8] | -1% | ±10% | ok |
| player swings / min | 1.46 [0, 2.97] | 0.344 [0.195, 0.498] | +324% | ±15% | ok |
| keeper hit rate | 0.33 [0.319, 0.341] | 0.311 [0.307, 0.315] | +6% | ±10% | ok |
| keeper whiff rate | 0.401 [0.366, 0.435] | 0.42 [0.415, 0.426] | -5% | ±25% | ok |
| blocked rate | 0.2 [0.174, 0.231] | 0.198 [0.194, 0.202] | +1% | ±20% | ok |
| parried rate | 0.0607 [0.0448, 0.0774] | 0.0694 [0.0653, 0.0739] | -12% | ±20% | ok |
| read counters / min | 17.2 [16.4, 18.1] | 15.8 [15.5, 16.1] | +9% | ±20% | ok |
| fight length (s) | 22.9 [22.3, 23.7] | 23.5 [23.3, 23.8] | -3% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 260 [253, 267] | 263 [262, 264] | -1% | ±10% | ok |

Keeper move mix: JS divergence 0.002 bits; Unreal DelayedHeavy 26%, HeavySweepLeft 17%, HeavyCleave 15%, Approach 14%; simulator DelayedHeavy 24%, HeavySweepLeft 17%, HeavyCleave 17%, Approach 15%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.461 [0.447, 0.475] | 0.454 [0.451, 0.457] | +2% |
| frames within its attack range (185) | 0.0622 [0.0201, 0.109] | 0.0364 [0.0319, 0.0412] | +71% |
| ... in range AND able to act | 0.019 [0.00505, 0.0359] | 0.00973 [0.0083, 0.0112] | +95% |
| frames the keeper was open (punish window) | 0.0579 [0.0455, 0.0733] | 0.0553 [0.0531, 0.0577] | +5% |
| ... open AND in range | 0.00615 [0, 0.0133] | 0.00174 [0.001, 0.00253] | +253% |
| frames a keeper swing was in flight | 0.622 [0.615, 0.628] | 0.626 [0.624, 0.627] | -1% |
| frames an answer was pending | 0.144 [0.136, 0.153] | 0.137 [0.136, 0.139] | +5% |
| frames walking in | 0.211 [0.194, 0.228] | 0.219 [0.218, 0.221] | -4% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0.157 [0.138, 0.176] | 0.162 [0.158, 0.165] | -3% |
| punish strings begun / min | 0.437 [0, 1.04] | 0.106 [0.059, 0.158] | +312% |
| aggression strings begun / min | 0.146 [0, 0.443] | 0.068 [0.0378, 0.107] | +114% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 1.46 [0, 2.97] | 0.344 [0.195, 0.498] | +324% |
| mean distance (every frame) | 237 [231, 243] | 239 [239, 240] | -1% |

Unreal wall time 117 s for 18 fights.

## SCRIPT vs habitual 0.7 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 424 [381, 468] | 441 [433, 448] | -4% | ±15% | ok |
| player damage / min | 236 [161, 322] | 221 [209, 232] | +7% | ±20% | ok |
| keeper swings / min | 64.6 [63.4, 65.9] | 63.8 [63.6, 64] | +1% | ±10% | ok |
| player swings / min | 10.2 [6.61, 14.2] | 9.15 [8.65, 9.64] | +11% | ±15% | ok |
| keeper hit rate | 0.265 [0.243, 0.279] | 0.275 [0.271, 0.279] | -4% | ±10% | ok |
| keeper whiff rate | 0.351 [0.317, 0.381] | 0.334 [0.33, 0.338] | +5% | ±25% | ok |
| blocked rate | 0.0264 [0.0213, 0.0321] | 0.04 [0.0379, 0.0421] | -34% | ±20% | **GAP** |
| parried rate | 0.278 [0.263, 0.291] | 0.274 [0.27, 0.278] | +1% | ±20% | ok |
| read counters / min | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| fight length (s) | 52.8 [48, 58.5] | 50.9 [50, 51.8] | +4% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 220 [216, 224] | 220 [219, 220] | +0% | ±10% | ok |

Keeper move mix: JS divergence 0.000 bits; Unreal FastSlash 25%, Approach 17%, HeavyCleave 10%, SweepLeft 5%; simulator FastSlash 25%, Approach 17%, HeavyCleave 10%, SweepLeft 5%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.617 [0.602, 0.633] | 0.623 [0.62, 0.625] | -1% |
| frames within its attack range (185) | 0.0836 [0.0642, 0.106] | 0.0973 [0.0931, 0.101] | -14% |
| ... in range AND able to act | 0.0237 [0.0203, 0.0273] | 0.0298 [0.0288, 0.0308] | -20% |
| frames the keeper was open (punish window) | 0.199 [0.184, 0.212] | 0.199 [0.196, 0.202] | -0% |
| ... open AND in range | 0.0455 [0.0307, 0.062] | 0.0485 [0.046, 0.0511] | -6% |
| frames a keeper swing was in flight | 0.37 [0.365, 0.374] | 0.366 [0.365, 0.368] | +1% |
| frames an answer was pending | 0.0477 [0.0452, 0.0506] | 0.0478 [0.0471, 0.0485] | -0% |
| frames walking in | 0.191 [0.179, 0.201] | 0.195 [0.194, 0.197] | -2% |
| frames backing off | 0 [0, 0] | 8.74e-06 [0, 2.57e-05] | -100% |
| frames guarding | 0.0269 [0.0212, 0.0329] | 0.0339 [0.0325, 0.0353] | -21% |
| punish strings begun / min | 2.4 [1.48, 3.41] | 2.42 [2.25, 2.59] | -1% |
| aggression strings begun / min | 1.83 [1.35, 2.44] | 1.97 [1.85, 2.09] | -7% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 10.2 [6.61, 14.2] | 9.15 [8.65, 9.64] | +11% |
| mean distance (every frame) | 223 [219, 226] | 222 [222, 223] | +0% |

Unreal wall time 259 s for 18 fights.

## RL vs varied 0.5 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 1.15e+03 [1.09e+03, 1.22e+03] | 1.19e+03 [1.18e+03, 1.2e+03] | -3% | ±15% | ok |
| player damage / min | 6.75 [0, 19.3] | 12.6 [7.84, 17.5] | -46% | ±20% | ok |
| keeper swings / min | 78 [76.3, 79.9] | 79.6 [79.2, 80] | -2% | ±10% | ok |
| player swings / min | 0.506 [0, 1.13] | 0.683 [0.455, 0.921] | -26% | ±15% | ok |
| keeper hit rate | 0.504 [0.467, 0.533] | 0.55 [0.542, 0.559] | -8% | ±10% | ok |
| keeper whiff rate | 0.26 [0.227, 0.294] | 0.247 [0.241, 0.254] | +5% | ±25% | ok |
| blocked rate | 0.121 [0.0899, 0.153] | 0.105 [0.0994, 0.11] | +16% | ±20% | ok |
| parried rate | 0.115 [0.0937, 0.135] | 0.0958 [0.0916, 0.1] | +20% | ±20% | ok |
| read counters / min | 12.5 [7.08, 18.4] | 18.1 [16.9, 19.4] | -31% | ±20% | ok |
| fight length (s) | 19.8 [18.4, 21.1] | 19 [18.8, 19.2] | +4% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 247 [241, 252] | 244 [243, 245] | +1% | ±10% | ok |

Keeper move mix: JS divergence 0.013 bits; Unreal FastSlash 31%, DelayedHeavy 28%, HeavyCleave 11%, Approach 8%; simulator FastSlash 37%, DelayedHeavy 27%, HeavyCleave 8%, Approach 7%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.529 [0.513, 0.546] | 0.524 [0.521, 0.527] | +1% |
| frames within its attack range (185) | 0.0335 [0.00649, 0.0672] | 0.0321 [0.0273, 0.037] | +4% |
| ... in range AND able to act | 0.00999 [0.00123, 0.023] | 0.00955 [0.00792, 0.0113] | +5% |
| frames the keeper was open (punish window) | 0.0766 [0.0641, 0.0878] | 0.0714 [0.069, 0.0741] | +7% |
| ... open AND in range | 0.00323 [0, 0.00924] | 0.0042 [0.00287, 0.00557] | -23% |
| frames a keeper swing was in flight | 0.57 [0.558, 0.582] | 0.551 [0.547, 0.554] | +3% |
| frames an answer was pending | 0.104 [0.0875, 0.12] | 0.0976 [0.0953, 0.0999] | +7% |
| frames walking in | 0.206 [0.194, 0.219] | 0.213 [0.21, 0.216] | -3% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0.109 [0.0899, 0.133] | 0.0946 [0.0907, 0.0987] | +16% |
| punish strings begun / min | 0.169 [0, 0.482] | 0.216 [0.142, 0.296] | -22% |
| aggression strings begun / min | 0.169 [0, 0.508] | 0.168 [0.11, 0.232] | +0% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 0.506 [0, 1.13] | 0.683 [0.455, 0.921] | -26% |
| mean distance (every frame) | 238 [233, 242] | 240 [239, 241] | -1% |

Unreal wall time 103 s for 18 fights.

## SCRIPT vs varied 0.5 (keeper 0, skill 1.00, T 0.0, gap 0) — UE 18 fights / 6 sessions, sim 600 / 200

| metric | Unreal | simulator | gap | tolerance | |
|---|---|---|---|---|---|
| keeper damage / min | 758 [710, 807] | 729 [718, 741] | +4% | ±15% | ok |
| player damage / min | 181 [109, 279] | 170 [158, 182] | +6% | ±20% | ok |
| keeper swings / min | 65 [64.1, 65.7] | 65.4 [65.3, 65.6] | -1% | ±10% | ok |
| player swings / min | 7.7 [4.67, 11.7] | 7.05 [6.56, 7.57] | +9% | ±15% | ok |
| keeper hit rate | 0.488 [0.449, 0.517] | 0.473 [0.467, 0.479] | +3% | ±10% | ok |
| keeper whiff rate | 0.279 [0.244, 0.314] | 0.288 [0.282, 0.294] | -3% | ±25% | ok |
| blocked rate | 0.0836 [0.0628, 0.103] | 0.0845 [0.0805, 0.088] | -1% | ±20% | ok |
| parried rate | 0.0958 [0.0788, 0.11] | 0.0998 [0.0958, 0.104] | -4% | ±20% | ok |
| read counters / min | 0 [0, 0] | 0 [0, 0] | +nan% | ±20% | ok |
| fight length (s) | 29.5 [27.6, 31.4] | 30.7 [30.2, 31.2] | -4% | ±15% | ok |
| player deaths / fight | 1 [1, 1] | 1 [1, 1] | +0% | ±10% | ok |
| distance at keeper commits | 233 [228, 237] | 233 [232, 234] | +0% | ±10% | ok |

Keeper move mix: JS divergence 0.000 bits; Unreal FastSlash 25%, Approach 18%, HeavyCleave 9%, SweepLeft 6%; simulator FastSlash 25%, Approach 18%, HeavyCleave 9%, SweepLeft 5%

The simulated player's own view (the same bot code on both sides; FBotDiag):

| | Unreal | simulator | gap |
|---|---|---|---|
| frames it could act | 0.596 [0.579, 0.611] | 0.595 [0.593, 0.598] | +0% |
| frames within its attack range (185) | 0.11 [0.0768, 0.15] | 0.102 [0.0971, 0.108] | +8% |
| ... in range AND able to act | 0.043 [0.0322, 0.0552] | 0.0382 [0.0363, 0.0404] | +13% |
| frames the keeper was open (punish window) | 0.12 [0.108, 0.134] | 0.122 [0.12, 0.125] | -2% |
| ... open AND in range | 0.0443 [0.0283, 0.0666] | 0.0401 [0.0375, 0.0428] | +10% |
| frames a keeper swing was in flight | 0.377 [0.368, 0.384] | 0.381 [0.38, 0.382] | -1% |
| frames an answer was pending | 0.0464 [0.0433, 0.0496] | 0.047 [0.046, 0.048] | -1% |
| frames walking in | 0.203 [0.194, 0.212] | 0.209 [0.207, 0.211] | -3% |
| frames backing off | 0 [0, 0] | 0 [0, 0] | +nan% |
| frames guarding | 0.0617 [0.051, 0.0712] | 0.0612 [0.0587, 0.0635] | +1% |
| punish strings begun / min | 2.15 [0.885, 3.82] | 1.89 [1.73, 2.06] | +14% |
| aggression strings begun / min | 2.15 [1.46, 2.93] | 1.99 [1.82, 2.17] | +8% |
| keeper swings answered by swinging / min | 0 [0, 0] | 0 [0, 0] | +nan% |
| attacks committed / min | 7.7 [4.67, 11.7] | 7.05 [6.56, 7.57] | +9% |
| mean distance (every frame) | 231 [226, 235] | 232 [231, 233] | -1% |

Unreal wall time 148 s for 18 fights.

## Summary

6 flagged metric(s):

- rl vs rhythm 0.7: keeper whiff rate — Unreal 0.0998, simulator 0.0776 (+29%)
- script vs rhythm 0.7: player damage / min — Unreal 614, simulator 380 (+62%)
- script vs rhythm 0.7: player swings / min — Unreal 19.4, simulator 12.3 (+58%)
- script vs rhythm 0.7: keeper hit rate — Unreal 0.289, simulator 0.328 (-12%)
- script vs rhythm 0.7: fight length (s) — Unreal 55.6, simulator 48.2 (+15%)
- script vs habitual 0.7: blocked rate — Unreal 0.0264, simulator 0.04 (-34%)
