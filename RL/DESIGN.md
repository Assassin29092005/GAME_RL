# RL/DESIGN.md — how the RL keeper is built

`RL.md` is the plan (why, what, milestones). This file is the engineering contract that the C++ core, the training
environment and the Python trainer implement. Headers are authoritative for signatures; this file is authoritative for
semantics. If the two disagree, fix the disagreement — do not guess.

```
Source/HellwalkerRL/Public/HWCore/          engine-free core — compiled into the GAME and into every tool
  HWBrain.h         IBossBrain, FDuelGeometry, FBrainDecision, FReadMeterEvent, scripts
  HWScriptBrain.h   FScriptBrain — Pathbreaker (the script verbatim)
  HWRLTypes.h       RL layouts: actions (23), observation (107 floats, layout 3), history tokens (32 x 6), answer classes
                    (12), skill parameters (RL::SkillParams), the three keepers (RL::EKeeper)
  HWRLObserver.h    FRLObserver (senses, masks, action application, labels, READ) + FRLSession (memory about you)
  HWRLPolicy.h      FRLPolicy — .hwrl weights + C++ forward pass
  HWRLBrain.h       FRLBrain — the game's adaptive keeper (observer + policy + session) + FRLNotebook
  HWSim.h           the 2-D arena + simulated players (reference bots, habit players, learning players)
Sim/Classic/                                  the reference project's tally brain — TOOLS ONLY (benchmark)
Sim/ThesisSim.cpp, Sim/SimArms.*              B0: Pathbreaker vs an adaptive arm (classic | rl [--skill --identity])
RL/native/                                    TOOLS ONLY: the training environments + hwrl.dll C ABI (v2)
  HWRLEnv.h/.cpp    FRLEnv / FRLEnvBatch (the keeper's env), FRLPlayerEnv / FRLPlayerEnvBatch (the exploiter's env),
                    FWorkerPool, EvalSessions, EvalAttackLog
  HWRLPlayer.h/.cpp FPlayerAgent (the exploiter's senses and hands) + FExploiterDriver (an exploiter in the keeper's env)
  hwrl_capi.h/.cpp  extern "C" API for Python (ctypes)
  build.bat         builds RL/native/out/hwrl.dll (HWRL_OUT overrides the folder) with the Sim/build.bat flags
RL/*.py                                       the trainer (PyTorch)
  hwcore.py  ctypes binding     env.py  vector envs + sessions   players.py  population + curriculum + league entries
  model.py   the network        ppo_rnn.py  recurrent PPO         train.py    stages, logging, gates, the league
  export.py  .hwrl + ONNX       eval.py  RL.md §7 checks           habits.py   the reading test
  exploit.py the exploiters (RL-4)                                 tests/      parity & smoke tests
Content/HellwalkerRL/RL/hellwalker_rl.hwrl    the shipped policy (tracked; Tools\RLShip.bat puts one there)
```

## Ground rules (C++)

* HWCore is engine-free C++20: no Unreal headers, no RTTI, no exceptions, no identifiers that collide with UE / Windows
  macros (`check`, `verify`, `ensure`, `IN`, `OUT`, `min`, `max`, `small`, `DrawText`, ...). It must compile warning-free
  under `Sim/build.bat` (`/W4` + UE's warnings-as-errors list) AND inside Unreal. Match the existing code style (tabs,
  `F`/`E` prefixes, `int32_t`, comments that say *why*).
* Determinism: no `rand()`, no time, no unordered iteration; randomness only through `HW::FRandom` seeded explicitly.
* No allocation per frame in the observer / brain / env hot paths (fixed-capacity members; vectors sized once).
* Handedness: `FDuelGeometry` is in simulator convention (+Y left of +X). If `Geo.bMirrorY`, the observer negates Y of
  positions AND of the boss's `CommitFacingY` before using them.
* A const `FRLPolicy` may be shared by many threads only through the external-scratch `Forward` overload (its own
  scratch is per instance); `FRLBrain` owns its scratch, so brains on different threads may share one policy.

## 0. Skill and identity (layout 3)

`FRLConfig::Skill` (in [0, 1]) and `FRLConfig::Identity` (`RL::EKeeper`: 0 Warden, 1 Sage, 2 Returned) are read at
`BeginEncounter` and fixed for the fight. `RL::SkillParams(Skill)` (`S` clamped, `E = 1 - S`):

| | skill 1 (the Hellwalker tier B0 grades) | skill 0 | rule |
|---|---|---|---|
| perception delay | 6 frames | 16 | `6 + round(10 E)` |
| decision gap (and a Wait's length) | 6 | 12 | `6 + round(6 E)` |
| attacks per string | 3 | 1 | `3` if `S >= 0.6`, `2` if `S >= 0.25`, else `1` |
| grab / killer cooldowns | 180 / 600 | 360 / 1200 | `x (1 + E)` |
| aggression target | `TargetSwingsPerMin` (66) | 50 | `RL::TargetSwingsPerMin(base, S) = base - 16 E` |

Lower skill is a slower, more legible keeper, never one that cheats or throws fights. The network observes its skill and
identity (§3), so one set of weights plays every keeper at every difficulty. The game's presets (UHWSettingsSubsystem):
Easy = skill 0 + sampling at temperature 1 + a breather, Normal 0.4 + temperature 0.6, Hard 0.75, Hellwalker 1
(greedy), Adaptive = a controller moving the skill between fights to keep them close. Sampling (`FRLBrain::SetTemperature`)
draws from `softmax(logits / T)` over the allowed actions with the fight's seeded stream — reproducible per seed.

The breather (`FRLConfig::MinSwingGap`, `RL::EasySwingGap` = 84 frames, 1.4 s): the network at skill 0 still out-hits the
script (eval ladder: ~1000 vs ~730 dmg/min on the same players), so Easy also masks attack OPENERS until 84 frames after
the keeper's last attack COMMIT (strings already started may finish; the rest after a slow attack is shorter — 0.23 s after
a DelayedHeavy, 0.8 s after a FastSlash). Unlike the grab / killer cooldowns it is not observed, and it is never set in
training: the policy meets a mask it never trained with, so its effect is measured, not assumed. Because it caps the
swing rate at 42.9 / min while Easy's aggression target is 50, the swing-deficit input's target is capped at 90 % of what
the breather allows (38.6 / min) — otherwise the keeper would be told it is behind for the whole fight, an input training
never showed it. With it Easy deals ~540 dmg/min (the script ~720 on the same players), below the script (`easy_below_script`). Adaptive ramps it in below skill 0.15 (`UHWSessionSubsystem::AdaptiveSwingGap`: Easy's at
the floor). C ABI: `hwrl_eval_sessions_gap`, `hwrl_easy_swing_gap` (optional exports; older DLLs still load).

Each keeper's health in the open world: `RL::KeeperHealthScale` = 1, 0.9, 1.25 (the shrine specs). The training env
applies it in mortal fights; the reward is normalised by the TUNED maxima.

## 1. Decision points, actions, masks (FRLObserver)

**Decision point:** `Session != null && !Duel.IsOver() && Boss.IsActionable() && Duel.Frame >= NextDecisionFrame`.
`NextDecisionFrame = decision frame + Params().DecisionGap` after every decision (a Wait lasts exactly that long).

**ApplyAction(Duel, A, Read)** at frame D:
1. Recompute the mask; if `A` is out of range or masked → `Illegal++`, `A = Wait`.
2. Wait: no commit.
3. A locomotion move the keeper is already running (`Boss.State == Acting && Boss.Move == M && Kind == Locomotion`) →
   *continue*: no commit.
4. Otherwise `Duel.Commit(ESide::Boss, M)` (facing is latched by the caller after player input, as for every brain).
   If it fails → `Illegal++`, treated as Wait.
5. On a committed attack: `Attacks++`, `Session->BossSwingsSeen++`, `LastOwnSwingFrame = D`, push `D` into the swing
   ring; `StringAttacks = (was in a chain window) ? StringAttacks + 1 : 1`; `BGrab` sets `LastGrabFrame = D`.
   On a committed non-attack: `StringAttacks = 0`. Wait / continue leave it unchanged.
6. Tokens: close the current open token (`CloseFrame = D`) and open a new one — EXCEPT when the action is Wait (or a
   continue) and the current open token is of the same kind (Wait token for Wait; the same move for a continue) and has
   no answer yet: then extend it (keep it open). New token: `Boss` = 9 for Wait, else `SymIndex(M.Symbol) - 11`
   (BFast → 1 … BRetreat → 8); `Move` = the committed move (None for Wait / continue); `OpenFrame = D`;
   `MoveCommitFrame = D` if committed; `Read` = `*Read` (or empty). If `NumOpen == MaxOpen`, finalise the oldest first.
7. `bDefendedSinceDecision = false`, `LastDecision = D`, `LastActionIndex = A`, `Decisions++`, `NextDecisionFrame = D + gap`.

"Chain window" = the keeper is Acting in an attack and already actionable (past its cancel frame).

**Mask (BuildMask)** — Wait always 1. Boss move `M = ActionMove(a)` is allowed iff all hold:
* `Duel.CanCommit(ESide::Boss, M)`;
* attacks: perceived distance (latest recorded boss position to the PERCEIVED player position) `<= M.Range + RL::ReachSlack`;
* attacks in a chain window: `StringAttacks < Params().MaxString` and `M.Startup >= ChainStartupFloor(current move's symbol)`;
* `BGrab`: `D - LastGrabFrame >= Params().GrabCooldown`;
* `BKillerThrust`: `D - LastKillerFrame >= Params().KillerCooldown`. (Added after RL-1: against a fixed habitual bot the
  keeper learned to drain the guard with heavy sweeps and then spam the unblockable killer on an empty guard.)
* attacks outside a chain window, when `MinGap > 0` (the game's Easy only, §0): `D - LastOwnSwingFrame >= MinGap`
  (`Config.MinSwingGap`, latched at BeginEncounter; 0 in training, B0 and every eval except the ladder's Easy rung).

**ChainStartupFloor(sym)** is computed once from `BossScript(0 .. NumBossScripts-1)`: for every slot `i` with `bChain`
whose move is an attack and whose previous slot (`i-1`, wrapping) is an attack, `floor[prev.Symbol] = min(floor,
Move(slot.Move).Startup)`. Classes no script chains after return 9999. (Today: BFast 12, BHeavy 12, BFeint 14, BKiller —.)

## 2. Perception (what the keeper may see)

Let `Pd = Params().Perception` (6 at skill 1, up to 16). `Snaps[F % SnapRing]` = the state at the START of frame F:
recorded at the end of `RecordFrame` after step F-1 (and once in `BeginEncounter` for the fight's first frame). A player
commitment made at frame e (before step e) first appears in `Snap[e+1]`. At decision frame D the keeper sees
**`SnapAt(D - Pd)`** for everything about the player (state, move, phase, progress, guard, parry, i-frames, armor,
weapon, chain, health, sha-chi, position). So a commitment at frame e is invisible at `D = e + Pd` and visible at
`D = e + Pd + 1` (tests assert exactly this at skill 1 and skill 0).

Player EVENTS (commits, the player's swing outcomes) wait in a ring and are applied to the perceived state (last player
outcome, last player commit frame) only once `E.Frame <= Duel.Frame - Pd - 1` (processed in RecordFrame, where
`Duel.Frame` is the next frame). The keeper's OWN state and own swing outcomes are current (it knows its body).
`SnapRing` (32) exceeds `MaxPerceptionFrames + 4 + 1` (static_assert).

Geometry in snapshots: `PX, PY` (player) and `BX, BY` (keeper) from the `FDuelGeometry` of that frame, mirrored if asked.
Velocities use `SnapAt(D-Pd)` and `SnapAt(D-Pd-4)`.

**Health in immortal fights.** `FEncounter`'s rate-measurement mode gives both fighters 1e9-point pools; the observer
then reports health as a fraction of a normal pool (`Tuning()` max, x the keeper's health scale for itself) that
refills at every would-be death (a sawtooth), so evaluation never shows the keeper the "full health forever" state it
never met in (mortal) training. Mortal fights: `Health / HealthMax` as before.

## 3. The observation vector (HWRLTypes.h `EObs`, 107 floats, layout version 3)

Let `B` = the keeper now, `P = SnapAt(D - Pd)`, `P2 = SnapAt(D - Pd - 4)`, `L` = the latest snapshot (keeper position).
All one-hots are exactly one 1 or all zeros as documented; everything is clipped as the header says.

* SELF: health/max; sha-chi/max; `EFighterState` one-hot (7); current move one-hot over the 22 boss moves (zeros unless
  Acting); phase one-hot (attack startup `T < Startup`, attack active `T < Startup+Active`, attack recovery, other move;
  zeros unless Acting); `T / TotalFrames`; `FramesUntilActionable/60` [0,2]; hyper armor; i-frames; `StunLeft/90` [0,1];
  last own swing outcome one-hot (EHitOutcome index 0..4); frames since it /120 [0,1] (1 if none); defended since the
  last decision; `StringAttacks / 3` [0,1]; grab cooldown left / the skill's grab cooldown; killer cooldown left / the
  skill's killer cooldown; chain window.
* PLAYER (perceived): distance(L keeper, P player)/500 [0,3]; radial velocity = `(|r(P2)| - |r(P)|)/4 / 10` [-2,2]
  with `r = player - keeper` inside each snapshot (+ = closing); lateral velocity = `dot((P.player - P2.player)/4,
  leftOf(facing)) / 10` [-2,2] where `facing = normalize(P.keeper - P.player)` and `leftOf(x, y) = (-y, x)` (+ = the
  player moving to its own left); bearing of the perceived player from the keeper's committed facing when the keeper is
  Acting an attack (`cos = dot(F, d)`, `sin = F.x*d.y - F.y*d.x`, F and d unit, d from L keeper to P player), else
  (sin 0, cos 1); health; sha-chi; state one-hot (7); player move one-hot over `PLight1..PSwitch` (13) (index `Move-1`,
  zeros unless Acting); phase one-hot (4, as for SELF with the player's move); progress; `max(0, P.UntilActionable - Pd)/60`
  [0,2]; guard held; guarding; parry live; i-frames; hyper armor; weapon one-hot (2); chain depth /3 [0,1]; the
  perceived last player swing outcome one-hot (5); `(D - perceived last player commit)/60` [0,2].
* CONTEXT: frame advantage `(max(0, P.UntilActionable - Pd) - B.FramesUntilActionable)/30` [-2,2]; `D/60/180` [0,1];
  `FightIndex/3` [0,1]; swing deficit `(Target - rate)/60` [-1,1] with `Target = TargetSwingsPerMin(Config target,
  skill)` and `rate = (own attack commits in the last 1800 frames) * 3600 / max(600, min(D, 1800))`;
  `(D - LastOwnSwingFrame)/300` [0,1].
* KEEPER: skill [0,1]; identity one-hot (3; an out-of-range identity reads as the Warden).

Tokens: `Session->Tokens` copied row by row (newest first), zero rows after `NumTokens`.

## 4. History tokens, labels, READ

A token covers one decision window `[OpenFrame, CloseFrame)`. `RecordFrame` updates the OPEN tokens from ground-truth
events (every event of frame f belongs to the token whose window contains f):
* player Commit (incl. guard raise, `Sym = Block`) → the window's first one sets `Answer = E.Sym`, `AnswerFrame = E.Frame`;
* keeper Outcome for the token's move → `Outcome` (first one); `Damage > 0` → `bDealt`;
* player Outcome with `Damage > 0` → `bTaken`; player swing Blocked / Parried by the keeper → `bDefendedSinceDecision`;
* each frame, for the current token: `bGuardSeen |= player guard held`, `Movement = PlayerMovement`.

`AnswerOf(T) = Answer` if set, else `Block` if `bGuardSeen`, else `Movement`.

A token is FINALISED (pushed into the session, newest first) only when `CloseFrame >= 0 && CloseFrame + Pd <= Duel.Frame`
— i.e. once all of it is perceivable — or when the next fight begins (`BeginEncounter` finalises everything open).

Encoding (`TokenVocab = {10, 13, 6, 5, 10, 4}`): Boss (1..9); Answer `SymIndex(AnswerOf) + 1`; Outcome `1 + EHitOutcome`
for attacks (1 for non-attacks); Exchange `1 + bDealt + 2*bTaken`; Timing: for an attack answered by a player commitment,
`Press = AnswerFrame - MoveCommitFrame`, anchor = `Startup`, or for baits (`FakeImpactFrame >= 0`) `bit = Press*2 <
Fake + Startup`, anchor = bit ? Fake : Startup; `LeadBucket(anchor - Press)`; else 1. Bite: baits answered by a
commitment → bit ? 2 : 3; else 1.

**Label** `AnswerToLastDecision()` = `SymIndex(AnswerOf(the token opened or extended at LastDecision))`, from ground truth
at call time (the env reads it when the step ends — the window is complete by then). -1 before any decision.

**READ banner:** when a keeper swing lands a Hit and its token's `Read.P >= Config.ReadMeterMinP` and
`AnswerOf(token) == Read.Predicted` at that moment → `ReadCounters++`, raise the Read Meter
`{E.Frame, Read.Predicted, Read.P, E.Move}`. Testimony only; it never feeds back into decisions.

## 5. FRLSession and fights

`FRLSession::Reset()` zeros everything. `BeginEncounter(Session, Duel, Geo)`: finalise open tokens into the session
(only if it is still the same, non-reset session), `Session->FightIndex = Session->FightsBegun++`, read the skill /
identity, reset every per-fight member, record `Snap[Duel.Frame]`, `NextDecisionFrame = Duel.Frame`.

## 6. FRLBrain (the game)

* `Bind(Policy, Session, Notebook = null)`; `Configure(Skill, Identity)`; `SetTemperature(T)` (0 = greedy).
* `IsReady()` = a session and `Policy->IsBossPlayable()` (side 0, this build's layout, hidden <= `RL::MaxHidden`). The
  game checks the same when it loads the model and falls back to the script otherwise (a statue boss is never possible).
* `BeginEncounter(Seed)`: per-fight reset only (`bBegun = false`, counters, `Last`), the sampling stream re-seeded from
  `Seed`, the notebook's pending prediction flushed. The session persists.
* `Think(Duel, Geo, Out)`: if no session → false. First call of a fight: `Obs.BeginEncounter(Session, Duel, Geo)`. If
  not ready or not a decision point → false. Else: flush the notebook's pending prediction (its window is complete);
  observe, mask; if `Session->HiddenSize != Policy->HiddenSize()` zero it and set the size; `Forward(...)` with the
  brain's own scratch; `A = argmax` (or a sample at the temperature); `Aux(Session->Hidden, A)` → predicted answer + p;
  `ApplyAction(Duel, A, &read)`. Decision record: `Kind = Policy`, `ScriptIndex = -1`, `Scripted = None`, `Chosen` = the
  committed move (None for Wait / continue), `Slot` from the move, `bChain`, `PrevOutcome`, `FrameAdvantage`,
  `ReadBits = log2(12) - H(aux)`, `Predicted`/`PredictedP`, `Top[3]`, `Value`, and a `Reason` like
  `"policy SweepLeft p=0.83 (Wait 0.08) | read StepL 0.82 | V +0.41"` (+ `" | sampled"` off the argmax).
* `OnFrame`: `Obs.RecordFrame(...)` once begun. `PopReadMeter` also counts the notebook's `ReadsLanded`.
* **FRLNotebook** (per game session, owned by the game): for every non-Wait decision whose answer is known (the next
  decision, or `FlushNotebook()` at a fight's end), `Answers[class][answer]++` (class = the keeper move's symbol BFast ..
  BRetreat) and `Predicted[class][its prediction]++`, accuracy overall / of confident calls (`p >= ReadMeterMinP`) / per
  fight. Ground truth for the player to read; never an input.

## 7. The environment (RL/native)

Per env: `FEncounter Enc` driving an adapter brain (an `IBossBrain` whose `Think` returns false and whose `OnFrame`
forwards to the env's `FRLObserver::RecordFrame`), `FSimArena`, `FPlayerBot` (+ its `FBotMemory`), an optional
`FExploiterDriver`, the player profile, an `FRLSession`, the observer, and its own `FRandom` (fight seeds, start
distances, exploiter streams).

Frame (identical order to `RunEncounter`): frame start → `Enc.ThinkBoss(Arena.Geometry())` → if decision point: PAUSE
(return to the trainer) / apply the pending action → player input (`Bot.Act`, or the exploiter's `Act`) →
`Arena.LatchCommits` → `Arena.SnapshotPreStep` → `Enc.StepFrame(Arena, movement)` → `Bot.OnEvents` (or the exploiter's
`RecordFrame`) → style events → `Arena.Integrate` → habit switch check (`switch_after >= 0 && Session.BossSwingsSeen >=
switch_after` → `Bot.SetHabitPhase(1)`) → fight end? → next frame.

**Sessions.** The spec fixes the player AND the keeper's side: `keeper_skill` (< 0 → 1), `keeper_identity` (< 0 → 0).
New session: `Session.Reset()`, `BotMem.Reset()`, profile from the spec, kind 7 binds the registered player policy
`policy_id` (an unknown id falls back to the profile's bot).

**New fight:** `seed = Rng.GetUnsignedInt() & 0x7fffffff`; `Enc.Begin(&Adapter, seed, immortal)`; the keeper's health x
`KeeperHealthScale(identity)` (mortal fights); `Arena.Reset(uniform start distance)`; `Bot.Reset(profile, seed * 7919 +
17)`, `Bot.SetMemory(&BotMem)`; habit phase from the session's swing count; `Obs.Config` = target spm, skill, identity;
`Obs.BeginEncounter(...)`; the exploiter's `BeginFight`.

`Step(A)`: take `PrevDealt`, `PrevTaken`, `PrevHits`, `PrevAttacks`, `PrevReads`; apply A at the paused frame; finish
the frame; `habit_phase` = the bot's phase NOW (a switch fires on the commit frame of swing #switch_after and the
player plans its answer >= 4 frames later, so this is the table that answers this step's swing — review finding: read
before the commit it lagged one swing); simulate until the next decision point of the SAME fight or the fight's end.
* `reward = dealt / Tuning().PlayerHealthMax - taken / Tuning().BossHealthMax + style` + terminal: player died +1, keeper
  died -1 (keeper death dominates), timeout 0. Normalised by the TUNED maxima.
* `style` (x `style_scale`, 0 = off) — small per-identity shaping on top of the shared reward (the personalities):
  Warden `0.03` per player guard break + `0.006` per draining swing the player blocked; Sage `0.02` per feint bite (the
  player committed a parry / ghoststep nearer a bait's fake impact than its real one — the token's Bite rule — once per
  swing) + `0.012` per player swing that whiffed into the keeper's evade; Returned `0.04` per READ counter (the trainer
  passes the read head's top answer and p every step). `style_events` reports the four counts per step.
* `hits` = the keeper's swings this step with a clean Hit outcome (blocked chip damage is not a hit).
* `cost` = the aggression floor as a per-window hinge: `max(0, Target - SwingsPerMinute(frame)) / 60 * step seconds`,
  `Target` = the fight's skill's target; 0 in the first 15 s. (The first version was a linear swing debt; summed over
  the population a surplus against some players paid for a deficit against others.)
* `aux_label = Obs.AnswerToLastDecision()` read BEFORE any fight rollover; `read_counter = ReadCountersLanded() > PrevReads`.
* Fight end: `fight_done`, `result`; next fight or next session (`session_done`); run to the new fight's first decision.
* Env i's stream: `SplitMix64(batch seed + i)`. Results are identical for any thread count (envbench asserts it).

**Players.** `MakeProfileFromSpec`: kinds 0-5 → `MakeBotProfile(kind, skill)`; kind 6 → habit tables, noise, adapts, and
`learn_rate` / `learn_temp` (a LEARNING player: per (boss swing class, response) a value updated from what the response
earned against this keeper — hit -1 (killer -1.5), blocked +0.25, dodged +1, parried +1.5 — tilting the table:
`P(r) ~ (table(r) + 0.02) * exp(Q(r) / temp)`; kept per session in `FBotMemory`; a ghoststep that falls back to a
block for lack of sha-chi is credited to Block); every override `>= 0` replaces the profile value. Kind 7: an exploiter
policy (§10).

**Evaluation in C++.** `EvalSessions(arm, ...)`: sessions of fights vs one spec, the brain deciding in C++ (0 script,
1 classic `RefSwingsPerMin = 62`, 2 RL with the spec's skill / identity and an optional temperature), the env's frame
loop, habit switches and learning-player memory; stats include the keeper's move histogram, style events, player swings
and the mean distance at the keeper's commits. `EvalAttackLog`: the reading test's raw material — sessions on a worker
pool, one record per keeper attack `{session, k, action, outcome, dealt, habit_phase, fight}`; identical for any thread
count.

## 8. The network and the .hwrl format

Defined in `HWRLPolicy.h` (and implemented there). `RL/model.py` defines exactly that network:

```python
class HellwalkerNet(nn.Module):
    def __init__(self, obs_dim=107, num_actions=23, aux_classes=12, token_vocab=(10, 13, 6, 5, 10, 4),
                 history_tokens=32, enc_hidden=256, embed_dim=32, hidden=256, recurrent=True, side=0): ...
```

Layers: `enc1 = Linear(obs_dim, enc_hidden)`, `enc2 = Linear(enc_hidden, enc_hidden)` (tanh after each);
`tok_emb = ModuleList(Embedding(v, embed_dim) for v in token_vocab)`, `tok_age = Parameter(zeros(K, embed_dim))`
(token vectors = relu(sum of field embeddings + age[k]); pool = masked mean over tokens with any non-zero field;
zeros when none); `gru = GRUCell(enc_hidden + embed_dim, hidden)` or `ff = Linear(...)` + tanh; `pi = Linear(hidden, A)`,
`v = Linear(hidden, 1)`; aux: `aux = Linear(hidden, aux)`, `aux_action = Parameter(zeros(aux, A))`,
`aux_logits = aux(feats) + aux_action[:, actions].T`. Export names: `enc1.*`, `enc2.*`, `tok.emb{f}`, `tok.age`,
`gru.weight_ih/weight_hh/bias_ih/bias_hh` (or `ff.*`), `pi.*`, `v.*`, `aux.weight`, `aux.bias`, `aux.action`, and
the `meta.*` scalars. Parity: torch vs `hwrl_policy_forward` within 1e-4 on logits / value / hidden.

The loader refuses: another `meta.obs_layout_version`; `meta.side` other than 0 / 1; a side-0 policy whose sizes differ
from `HWRLTypes.h` or whose hidden size exceeds `RL::MaxHidden` (512, the session memory); absurd sizes (obs > 4096,
tokens > 256). `model.py` refuses a keeper with hidden > 512 before training starts. Side-1 policies are the exploiters
(`obs = 99`, `actions = 14`, no tokens: `tok.age` is a [1, E] placeholder); `hwrl_policy_dims` reports a loaded policy's
sizes and the C ABI strides by them.

## 9. The trainer

* **Env wrapper (env.py):** owns the batch, a staging buffer laid out for the env (the keeper: obs 107 | tokens 32x6 |
  mask 23; an exploiter: obs 99 | no tokens | mask 14), pending player specs from `players.py`; after every
  `session_done` it assigns the env's next-next player (the C++ side starts sessions with the pending spec). Per-group
  fight stats and per-KEEPER stats (swings/min, READ counters, style events, feint / heavy / evade shares).
* **Rollouts:** N envs (4096 on the GPU), T = 128 decisions; storage sized by the env's staging layout.
* **Returns:** GAE (γ 0.995, λ 0.95) with session ends as episode ends (fight ends are NOT: the keeper should invest
  in reading you in fight 1 that pays in fight 2 — the meta-RL point). Reward normalisation by a running std of the
  discounted return; the cost folded in as `r - λ_c * c`, `λ_c += lr_λ * (mean cost per decision - 0.003)`, clipped at 0.
  A non-finite reward or cost from the env stops the run before it can poison the normaliser.
* **Loss:** PPO clip 0.2 on masked policies; value loss × 0.5; entropy bonus (masked) 0.01 → 0.001 linear; aux
  cross-entropy × 0.1 on steps with a label (aux conditioned on the taken action); 4 epochs × 8 minibatches of whole
  32-step sequences; Adam lr 3e-4 → 3e-5 linear; grad norm 0.5. A minibatch whose loss or gradient norm is non-finite
  skips its optimizer step (counted: `skipped_updates`).
* **Population (players.py):** reference bots (kinds 0-5, skill U(0.2, 1)) in ~20% of sessions; habit players (kind 6)
  otherwise: each class row ~ Dirichlet(α · 1) with α per curriculum band — band 0 [0.05, 0.2] strong habits,
  band 1 [0.2, 1], band 2 [1, 5], band 3 [5, 50] near random; timing ranges as before; habit switch with probability
  p_band (0.1, 0.25, 0.35, 0.35) at `switch_after ~ U{15..60}` boss swings; `fights_in_session ~ U{1..4}`; 35% of habit
  players LEARN (`learn_rate ~ U(0.05, 0.4)`, `learn_temp ~ U(0.2, 0.6)`); exploiters (kind 7) in `exploit_fraction`
  (12%) of sessions once the league has registered any (weights favour later rounds and stronger exploiters). The
  keeper's side of every session (a separate random stream): identity uniform over the three, skill 1 in half the
  sessions (the graded tier), else U(0, 1). Curriculum: as before (the ≥60% win-rate gate opens every band quickly).
* **Stages (train.py --stage):** `rl1` feed-forward vs one Habitual bot (skill 0.8), 1-fight sessions (sanity check);
  `rl2` recurrent vs the population, 1e9 decisions by default, with the reading test every 50 iterations and the B0-lite
  gate every 100; `rl3` = rl2 settings continued (`--init` keeps the source run's λ and return scale); `--league` adds the
  exploiter rounds (§10). Logs: TensorBoard + CSV in `RL/runs/<run>` (incl. `read_*`, `keeper_*`, `league_*`);
  checkpoints + exported `.hwrl` in `RL/checkpoints/<run>`.
* **Resume:** `--resume <ckpt>` restores the stage, sizes, every PPO / env / league setting (unless given again), the
  optimizer, λ, the normaliser, the generator, the population (incl. its exploiters, re-registered in their original
  order) and the counters. A changed `--steps` CONTINUES the lr / entropy decay from its current values to the finals
  at the new total (an anchor in the trainer state). A model that is not finite never overwrites `latest.*`.
* **Evaluation (eval.py, RL.md §7):** B0 via ThesisSim (`--skill`, `--identity`); the adaptation curve on held-out habit
  players (clean hits); arms on identical seeded players (script / classic / RL) incl. learning players; the three
  keepers' styles; the difficulty ladder (mortal fights, the game's presets vs the script); the reading test.
* **The reading test (habits.py)** — the clearest evidence: four groups of pure-habit held-out players (always parry /
  block / step left / step right) over the C++ attack log (greedy, the game's forward pass). A reader starts with the
  same mix against everyone and diverges toward each group's counter as the session goes on (move-mix Jensen-Shannon
  divergence early vs late); after a habit switch at keeper swing #30 (the first swing answered from the new table) its
  clean hit rate dips (10 before vs that swing and the 9 after), then recovers (20-39 after). Logged during training.

## 10. The exploiter league (RL-4)

* **The exploiter** (`RL/native/HWRLPlayer.h`): a player-side agent that sees its own body now and the KEEPER's body 10
  frames late (state, move, phase, progress, armor, i-frames, health, sha-chi, position, its committed swing's facing,
  its last swing outcome and last commitment as perceived) — never the keeper's inputs or network; 99 floats. It decides
  every 6 frames among 14 actions: hold, walk forward / back / left / right, guard (walks and guard persist until the next
  decision), light, heavy, parry, ghoststep forward / back / left / right, switch weapon; commitments obey FDuel's rules.
* **Its training env** (`FRLPlayerEnv`): the frozen keeper (`FRLBrain`, greedy, a fresh memory per session, skill 1, a
  uniform identity, 1-3 fights) thinks first each frame; the player decides at its decision points. Reward: 2 x keeper
  damage / keeper max - its own damage / 360 - 0.01 per decision out of reach (> 400 cm), +1 keeper died, -1 player
  died, -0.5 timeout. (Its job is to find holes, not to survive: without the last three terms it learned to circle and
  stall.) PPO as for the keeper (no cost, no aux), a small net (96 / GRU 64), 4e7 player decisions (~4 minutes).
* **The league** (`train.py --league --league-at 0.3,0.5,0.7,0.85 --league-exploiters 2`): at each fraction of the run,
  export the keeper, train the exploiters against it, register each in the keeper's env batch
  (`hwrl_batch_add_player_policy`) and the population (kind 7, `policy_id`). In the keeper's env an exploiter is run by
  `FExploiterDriver` (C++ forward pass with its own scratch, sampled at temperature 1 from the env's stream, its memory
  carried across the session's fights). The log reports, per round, the exploiters' win rate and exchange against the
  frozen keeper next to the reference bots' (the bar).
* First check (an early 3e7-decision keeper): the reference bots win 0% of fights against it; an exploiter trained for
  2.5e7 decisions won 66-69% — by spacing: backing out of reach (43% of its decisions) until the keeper whiffed, then a
  stepping heavy into its recovery. Exactly the kind of hole the habit population never pokes.
* **The long run** (`rl2_keepers`, four rounds at 0.3 / 0.5 / 0.7 / 0.85 of 10⁹, a fifth in the first fine-tune): each
  round's two exploiters beat the frozen keeper 87-100 % of fights (one 5 %), with exchanges 0.5-8.6 against the
  reference bots' 0.00-0.015. Kept in the population (kind 7, ten in all), they make the next keeper harder to exploit —
  a fresh exploiter against the 10⁹ keeper wins 98 % (exchange 2.0, close pressure: walk in 35 %, guard 13 %, light
  9 %), against the shipped 1.17 × 10⁹ keeper 93 % (exchange 1.5, guard-and-poke: guard 33 %, light 17 %, circling left
  14 %) — but the league has not converged: RL.md's "exploitability under the ceiling after two rounds" is not met.

## 11. Review findings fixed (2026-10-02)

An adversarial review of the RL stack (five reviewers, every finding re-checked by an independent skeptic) confirmed and
this version fixes: the habit-switch reporting (`habit_phase` read before the commit lagged one swing; habits.py /
eval.py windows realigned); hit-rate metrics counted blocked chip damage as hits (now clean hits: `hits` in the step
output, the attack log's outcomes); a non-finite loss used to overwrite `latest.pt` / `latest.hwrl` (guards in the
trainer and the saver); resume silently reverting settings to CLI defaults and restarting the lr decay on an extension;
`--init` dropping λ and the return scale; the loader accepting policies the brain could never run (side, hidden > 512 —
now refused / treated as absent by the game: the script fallback); the C ABI striding a player policy by the boss's
sizes; the READ banner freezing on the end screen; immortal evaluations feeding health inputs stuck at 1.0.

A second review (2026-10-03: four reviewers over the new code — audio, the duel hooks, the parity harness, Easy's
breather — every finding re-checked by a verifier) confirmed and this version fixes: a double KO ended the duel twice
(two parity records, two Adaptive updates, two end broadcasts; now once, the keeper's death deciding it as in `FRLEnv`);
the music's tension read the critic as a win estimate (it is the normalised *remaining* return, which falls as a win
nears; tension now follows both health bars); arena keepers chosen by look or `-HWKeeper` kept the Warden's health
(now `RL::KeeperHealthScale`, as in training and the open world); a relative `-HWParity` path resolved inside the
engine's binaries; parity runs following a saved Adaptive difficulty (now `-HWDifficulty=`), not checking records
against the requested cell, clamping `--fights` silently on one side and hiding cells it dropped; the eval crashing on
a DLL without the breather export; the ladder keying rungs by (skill, T); Easy's swing-deficit input stuck "behind"
under the breather (its target is now capped); the breather's wording ("between strings": it runs commit to opener).

## 12. Unreal vs the simulator (RL/parity.py, Tools\Parity.bat)

The keeper was trained in the 2-D simulator; the game runs the same frame logic, brains and bots, but its fighters are
characters (movement, collision, displacement, hit-stop). The parity harness fights the same autoplay players against
the same keeper in both and compares the C++ eval's own statistics (`hwrl_eval_sessions` ↔ one `-HWParity` JSON line per
Unreal encounter: `FEncounterStats`, the keeper's moves, distance at its commits) plus the bot's own view (`FBotDiag`,
the same counters on both sides). Controlled: policy file, bot kind and skill, keeper identity / skill / temperature /
breather (`-HWDifficulty`, checked from the records), start distance (450-800 drawn as the simulator draws it), the
180 s cap, keeper health. One Unreal launch = one session (`-HWSeedBase`), `-benchmark` fixed 1/60 s steps (the duel
steps one frame per tick, ~2× real time). Rates are pooled per session with bootstrap 95 % intervals; a metric is
flagged when the gap exceeds its tolerance and the intervals do not overlap.

* **First measurement** (3 bots × RL / script, 18 Unreal fights per cell): the keeper side agreed — damage within 15 %,
  swings within 10 % in most cells, move mix ≤ 0.03 bits apart (the script's 0.001) — but the autoplay *player*
  attacked 3-45× as often in Unreal (RL vs habitual 0.7: 16.1 / min vs 0.34) and dealt 2.5-45× the damage.
* **Diagnosis** (`FBotDiag`): equal mean distance (240 vs 239), but in Unreal the bot spent 28 % of its frames within its
  185 attack range against 3.6 % in the simulator, and 8.2 % (vs 0.17 %) with the keeper open *and* in range. The keeper
  had learned to hover just outside that reach; the simulator moves at full speed at once and stops dead, while the
  characters accelerated (3000 cm/s²) and braked (2400 cm/s²: ~37 cm of slide per stop), and the Unreal keeper's
  approach stopped at 150 instead of the simulator's 130. Spacing went soft and the bot kept finding itself in reach.
  With near-instant acceleration and braking (the `-HWMoveAccel` / `-HWMoveBraking` switches) the habitual cell flagged
  nothing (keeper damage +6 %, hit rate +7 %, move mix 0.003 bits).
* **Fix**: the duelists' acceleration and braking are effectively instant (`AHWCharacterBase::DuelMoveAccel`), and the
  approach-stop distance is one constant shared by both (`FSimArena::ApproachStopDistance`).
* **After the fix**: six cells, 18 Unreal fights each: 6 of 72 metrics flagged (26 before). The RL keeper's cells
  agree: keeper damage within 4 %, swings within 4 %, hit rate within 8 %, fight length within 4 %, move mix ≤ 0.013 bits;
  the player side too (RL vs rhythm 0.7: player swings 23.3 vs 24.3 / min, in reach 28.7 % vs 29.3 % of its frames); one
  flag: the keeper's whiff rate vs the rhythm parrier (0.100 vs 0.078). The script's cells agree except against the rhythm
  parrier, where the bot is still in reach more often (19 % vs 13 % of its frames: player swings +58 %, fight +15 %), and
  a blocked-rate difference vs the habitual bot (0.026 vs 0.040). Hit-stop is ruled out (`-HWNoHitstop`: bit-identical
  records — nobody walks during a hit-stop); the likeliest remaining cause is capsule collision, which blocks ghoststeps
  and sidesteps through the opponent that the simulator lets pass. Report: `RL/reports/parity.md` (before the fix:
  `parity_before_movefix.md`).

Remaining known differences: capsules collide (the simulator lets a ghoststep pass through the opponent and keeps 90 cm
apart otherwise; the capsules are 38 + 55 = 93 cm and always solid); the Unreal autoplay walk is forward-only (the bot
never strafes, so this is equal today). Experiment switches: `-HWMoveAccel=`, `-HWMoveBraking=`, `-HWNoHitstop`
(`parity.py --ue-arg=...`).

## 13. Combat changes from play (2026-10-03) and the 1.28 × 10⁹ keeper

Play said the game was too hard. Two rules in the shared core (`FCombatTuning`, PLAN §6), applied to every brain:
the parry window is **12 frames** before impact (was 8), and after a successful parry the keeper starts **no attack
until `ParryAttackLockout` = 45 frames after its stun ends** (~1.1 s from the parry; it may guard, step and move —
`FFighter::NoAttackUntil`, checked in `FDuel::CanCommit`, so the RL keeper's mask inherits it; not observed). Tests:
`A3.ParryWindow` (13 before impact fails, 12 parries — on HeavyCleave, slow enough to press that early),
`A3.ParryLockout`.

The 1.17 × 10⁹ keeper under the new rules (no retraining): ~10 % less damage (reference bots 1094 vs 1212 dmg/min), its
reading intact (0.09 → 0.44 bits), but B0's aggression floor FAILED for all three keepers against the rhythm parrier
(44-49 vs the script's 47-53 swings/min: more parries, and each now costs it a pause it never trained with). So it was
fine-tuned under the new rules (`rl2_newrules`: resume from `keeper_1p17e9.pt`, 1.5 × 10⁸ decisions, λ floor 1.0, 40 %
reference bots; the requested extra league round did not run — the resumed league schedule was spent) and the
checkpoints scanned as before. First shipped: **`keeper_1p28e9`** (ckpt 2450), the one passing all four B0 checks for all three
keepers at 64 sessions (ckpt 2300 failed the Returned's net exchange) and reading best: move-mix divergence 0.06 → 0.61
bits (the highest yet), style checks all pass, ladder monotone with Easy (505) below the script (701). It deals about
what the old keeper did (reference bots 1182 dmg/min, ladder top 1310) but takes far more risk — players deal it 2.4×
the damage (258 vs 105 / min) — so fights are more two-sided than before the change. The population curve's rise fell
again (0.14 for the old keeper under the new rules → 0.02): the fine-tune strengthens the opener (first-5-attack hit
rate 0.56 → 0.67) while the late hit rate stays ~0.70; the pure-habit reading test is the measure that grew.
Report: `RL/reports/keepers_1p28e9.md`.

**Then the league again (`rl2_newrules2`).** A fresh exploiter against `keeper_1p28e9` won 100 % with an exchange of
12.5 (guard / hold 39 %, parry 6 %, heavies and lights 13 %): parry, then punish through the keeper's 45-frame pause —
during which the keeper could have guarded or stepped away, but had never learned to (it does not observe the pause; the
mask only forbids attacks). Two league rounds were added to a resumed run (the resumed league skips rounds whose index
already has exploiters, so `--league-at` listed seven fractions: five done, two new at 0.905 and 0.95 of 1.45 × 10⁹):
round 5's exploiters reached exchanges 33.2 and 4.2, round 6's (against the keeper that had trained on round 5) 15.6
and 5.1. Shipped: **`keeper_1p45e9`** (ckpt 2766): all four B0 checks pass for all three keepers at 64 sessions; the
population curve's rise is back (0.63 → 0.70, `rises_on_habits` PASS); reading 0.15 → 0.63 bits; styles and ladder pass
(Easy 524 vs the script's 701); a fresh exploiter still wins 99 % but at exchange 3.5 (it takes 387 damage/min, was 94).
Report: `RL/reports/keepers_1p45e9.md`.

A third review (2026-10-03, two reviewers over the telemetry uploader, the world map and the website, each finding
re-checked by a verifier; 17 confirmed) and the fixes: fights touched by a tool (hw.Kill, hw.InjectParry, hw.Hold, the
bot) or a scripted launch (-unattended, -HWExec) are no longer uploaded as research data; a fight's notebook numbers are
measured from a snapshot at its start (abandoned fights and a reset memory no longer leak into the next record), and the
record waits for the frame's READ (a READ on the killing blow counts for that fight); the difficulty is the one the fight
began at; queued fights carry `{uid}` (a new identity adopts them) and only the specific "account gone" errors wipe the
identity (not a bad or rotated API key); only ALREADY_EXISTS counts as delivered, and a 400/403 on a commit reads the
fight back before retrying or dropping; test launches use their own save slot and other projects' queued fights are kept,
not deleted; fights queued before a reset on the website are dropped; the stats link opens without the engine logging
its token. Rules: every lifetime counter (and each keeper's record) may grow by at most one fight's worth per write, not
only `totals.fights` (mirrored in the mock; two new attack checks, 82 in all). Docs: the API-key restriction must include
the Token Service API; itch.io's default upload cap; the read quota; CSV export guarded against formula injection.

## 14. Insight, the keeper's damage scale and the parry assist (2026-10-04)

From play: the owner lost every fight, so the project's demonstration — a trick works at first, then the keeper reads it
— never appeared. All three changes are game-side; the trained environment (rules, observation, training, B0 defaults)
is untouched, and every default reproduces the shipped numbers bit for bit.

**Modes and difficulty.** Two play modes: *Normal* (`EHWPlayMode::Pathbreaker`, the script at every shrine — the final
shrine no longer switches to the RL keeper) and *Adaptive AI* (`Hellwalker`, the RL keeper at every shrine); 66 Days is
gone (`SixtySixDays` stays in the enum; such saves load as Adaptive AI). Difficulty is Easy / Normal / Hard / Hellwalker
(`Adaptive` stays in the enum and sanitises to Normal); default Normal. `UHWSettingsSubsystem::PresetFor`:

| | SkillLo → SkillHi | keeper damage | assist slow-motion |
|---|---|---|---|
| Easy | 0 → 0.4 | 0.60 | 0.40 (+ the incoming glow) |
| Normal | 0 → 0.7 | 0.75 | 0.60 |
| Hard | 0.15 → 0.85 | 0.85 | 0.75 |
| Hellwalker | 0.3 → 1.0 | 0.90 | 0.85 |

**`FDuel::KeeperDamageScale`** (configuration, not reset): multiplies the health damage of the boss's hits and block
chip, never sha-chi drain, never the player's damage. 1 in `FRLEnv`, hwrl.dll and ThesisSim's defaults (`x * 1.0f == x`),
so training and parity are unchanged; the game sets it per fight for both modes (autoplay / parity / benchmark: 1). The
observation sees only the player's health falling more slowly. Hit-stop, flash, rumble and camera kick read the move's
unscaled damage, so a keeper heavy still feels heavy. Test: `A3.KeeperDamageScale`.

**`HW::FRLInsight`** (`HWRLBrain.h`): Insight ∈ [0, 0.95], 0 at the start of a session; after each RL fight, with N the
fight's read-head calls on the keeper's attacks (`FRLNotebook::SwingPredictions` / `SwingCorrect`, the delta since the last
record, taken after `FlushNotebook`) and a their top-1 accuracy:

    target = 0.95 * clamp((a - 0.40) / (0.70 - 0.40), 0, 1)
    Insight += rate * N / (N + 8) * (target - Insight)        rate = 0.45 rising, 0.55 falling

`KeeperConfig(Lo, Hi)`: skill = Lo + (Hi - Lo) * Insight; temperature = 2.5 * (1 - Insight / 0.7)+; swing gap (frames from
an attack's commit to the next opener, the `MinSwingGap` mask) = 210 * (1 - Insight / 0.6)+. Fixed for the whole fight
(set before it, like the old difficulty). Session state: `UHWSessionSubsystem` holds it next to the memory and the
notebook; `ResetMemory` (hw.ResetModel, quit) drops it. It replaces the old Adaptive controller (an outcome-driven skill
walk), whose rubber-banding hid the reading. The skill input stays inside its trained range [0, 1] but now changes
between fights of one session (training fixed it per session; the old Adaptive difficulty already did this), and the
3.5 s gap at insight 0 is an unobserved mask like Easy's — both measured, not assumed.

**Calibration (`Thesis.bat --arc`, `RunArcSession` in ThesisSim).** 64 sessions × 6 lethal fights (180 s) per player
against the shipped keeper, one `FRLBrain` bound to a session and a notebook across the fights exactly as the game binds
them, `Configure(skill, identity, gap)` + `SetTemperature` before each fight, `AfterFight` from the swing-call delta
after it. Players: the simulator's habit players executing one answer to every swing (parry / block / step left /
attack, habit noise 0.05) at skill 0.60 and 0.85 (assisted humans react like high-skill bots: at 0.6x slow motion a
273 ms reaction is ~10 simulator frames), switchers (one habit for fights 1–3, another from 4) and near-random players.
`--arc-tune k=v,...` tries the tunables without a rebuild. Tuning history: the agent's first constants (temperature 1,
the 84-frame Easy gap at insight 0) let no trick player win even fight 1; a softer start (temperature 2.5, gap 210)
gives the arc; faster learning (target accuracy 0.65, rise 0.6) countered skill-0.6 players by fight 2. Shipped
constants, Normal preset (player win % per fight): block 0.85 → 98 · 92 · 70 · 28 · 3 · 0; step-left 0.85 → 97 · 94 ·
80 · 48 · 19 · 3; parry 0.60 → 92 · 69 · 45 · 27 · 17 · 22; near-random insight stays ≤ 0.22. Graded check (block /
step at skill ≥ 0.8: fights 1–2 ≥ 70 %, fight 5 ≤ 30 %, keeper rising): PASS on Easy and Normal; Hard and Hellwalker
counter faster (Hellwalker: block 0.85 → 47 · 3 · 0 …) and fail it, as a harder tier should. Not countered: a skill-0.85
parry bot (98–100 % every fight on Normal) — the parry-and-punish weakness (§13) at the easier rules; switching to
another predictable habit does not reset the arc (the new one is read within a fight).

**The parry assist (`HWParryAssist`, presentation only).** `Evaluate(Boss, Player, alpha, distance, hitstop)` gives the
cue between two duel steps: with N = the player's `FramesUntilActionable()` (a press waits in the 3-frame input buffer)
and D = impact − boss T, a press made now parries iff `PParry.Startup ≤ D − N ≤ PParry.Startup + PParry.Active − 1`; lit
(red) exactly then, grey when the window is open but N makes it impossible, nothing for unparryable moves, resolved
swings, swings that cannot reach (distance > max(Range, SweepReach) + 75 cm) or a parry already live at the impact. The
impact is the one the wind-up *shows* (`FakeImpactFrame` for feints and the delayed heavy) until that moment passes —
judged at the step a buffered press commits on — then the real one. Timing the real impact instead would make every
feint and delayed heavy free and leave the keeper only the grab and the killer thrust against a parry habit, so the arc
could not counter the trick the ring invites. `Project.HellwalkerRL.Assist.*` checks every parryable boss move against
a real `FDuel`: on every lit frame a press parries (bait frames excepted: lit, and the press fails), on every other frame
it does not, including presses buffered during recovery.

Slow motion: `UHWDuelSubsystem` advances its frame cursor by `DeltaTime * 60 * TimeScale` and sets `CustomTimeDilation =
TimeScale` on both fighters (movement, root motion, animation and boss locomotion stay in step with the frames; menus,
HUD, automation and game-mode timers stay real-time — never `SetGlobalTimeDilation`). Target = the difficulty's slow scale
while the cue is lit and the player can act; ramps in ~0.06 s, out ~0.15 s of real time; reset to 1 on every exit path
(end, reset, clear, register, deinitialise, not running, autoplay switched on). Rules, hit-stop and the keeper's
perception stay in simulator frames: the simulator never sees the slow motion. A human's reaction measured in simulator
frames shrinks with it (≈ 6.6 frames for a 273 ms reaction at 0.4x — faster than the best training bot), which is why
the assist is logged with every fight. Gating: autoplay, parity runs and `-benchmark` play with no assist and damage 1;
parity runs also force skill 1 / temperature 0 / gap 0 (what parity always measured); scripted launches get the assist
only with `-HWParryAssist=`. The unblockable telegraph moved from red to violet so red means only "parry now".

**Telemetry v2** (`web/CONTRACT.md`): fight documents carry `assist` ("off" | "ring" | "ring+slowmo"), `slowmoScale`,
`keeperDamageScale`, `parryWindowFrames` and `insight` (at fight start; 0 for the script); `adaptive` now means the
insight ramp set the skill. The rules accept v1 (exactly the old keys, forever — offline-queued bodies) or v2; the owner
must publish the new rules before distributing a 1.4.0 build.

**The rules met real Firestore (2026-10-04).** The first player of the released 1.4.0 build reported an empty stats page:
the database held nothing. Replaying the game's own requests with an existing anonymous identity showed real Firestore
refusing every player create (PERMISSION_DENIED) while a minimal create passed; field-by-field updates isolated it — each
piece passed alone, only the combinations failed, and the per-fight totals update failed on its own. Real Firestore
evaluates at most 1,000 rule expressions per request; the rules validated every counter and nested map field by field
(the local mock has no such limit, so all 109 checks had passed). The rules are now lean — shapes, ownership, server time,
the fight's identifying fields and growth caps for fights / wins / losses / timeouts, each keeper's record and the
leaderboards' counts (web/CONTRACT.md "Rules"); the website deletes a reset's fights 50 per request; the game logs the
server's reason for every refusal.
