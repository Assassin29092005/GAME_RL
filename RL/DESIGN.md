# RL/DESIGN.md — how the RL keeper is built

`RL.md` is the plan (why, what, milestones). This file is the engineering contract that the C++ core, the training
environment and the Python trainer implement. Headers are authoritative for signatures; this file is authoritative for
semantics. If the two disagree, fix the disagreement — do not guess.

```
Source/HellwalkerRL/Public/HWCore/          engine-free core — compiled into the GAME and into every tool
  HWBrain.h         IBossBrain, FDuelGeometry, FBrainDecision, FReadMeterEvent, scripts
  HWScriptBrain.h   FScriptBrain — Pathbreaker (the script verbatim)
  HWRLTypes.h       RL layouts: actions (23), observation (103 floats), history tokens (32 x 6), answer classes (12)
  HWRLObserver.h    FRLObserver (senses, masks, action application, labels, READ) + FRLSession (memory about you)
  HWRLPolicy.h      FRLPolicy — .hwrl weights + C++ forward pass
  HWRLBrain.h       FRLBrain — the game's adaptive keeper (observer + policy + session)
Sim/Classic/                                  the reference project's tally brain — TOOLS ONLY (benchmark)
Sim/ThesisSim.cpp, Sim/SimArms.*              B0: Pathbreaker vs an adaptive arm (classic | rl)
RL/native/                                    TOOLS ONLY: the training environment + hwrl.dll C ABI
  HWRLEnv.h/.cpp    FRLEnv / FRLEnvBatch
  hwrl_capi.h/.cpp  extern "C" API for Python (ctypes)
  build.bat         builds RL/native/out/hwrl.dll with the Sim/build.bat flags
RL/*.py                                       the trainer (PyTorch)
  hwcore.py  ctypes binding     env.py  vector env + sessions     players.py  population + curriculum
  model.py   the network        ppo_rnn.py  recurrent PPO         train.py    stages, logging, checkpoints
  export.py  .hwrl + ONNX       eval.py  RL.md §7 checks           tests/      parity & smoke tests
RL/Models/hellwalker_rl.hwrl                  the shipped policy (tracked); the game loads a copy staged in Content
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

## 1. Decision points, actions, masks (FRLObserver)

**Decision point:** `Session != null && !Duel.IsOver() && Boss.IsActionable() && Duel.Frame >= NextDecisionFrame`.
`NextDecisionFrame = decision frame + RL::DecisionGapFrames (6)` after every decision (a Wait lasts exactly 6 frames).

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
7. `bDefendedSinceDecision = false`, `LastDecision = D`, `LastActionIndex = A`, `Decisions++`, `NextDecisionFrame = D + 6`.

"Chain window" = the keeper is Acting in an attack and already actionable (past its cancel frame).

**Mask (BuildMask)** — Wait always 1. Boss move `M = ActionMove(a)` is allowed iff all hold:
* `Duel.CanCommit(ESide::Boss, M)`;
* attacks: perceived distance (latest recorded boss position to the PERCEIVED player position) `<= M.Range + RL::ReachSlack`;
* attacks in a chain window: `StringAttacks < RL::MaxStringAttacks` and `M.Startup >= ChainStartupFloor(current move's symbol)`;
* `BGrab`: `D - LastGrabFrame >= RL::GrabCooldownFrames` (3 s);
* `BKillerThrust`: `D - LastKillerFrame >= RL::KillerCooldownFrames` (10 s). Added after RL-1: against a fixed habitual bot
  the keeper learned to drain the guard with heavy sweeps and then spam the unblockable killer on an empty guard (161
  of 289 decisions, 1870 dmg/min vs the script's 277). The scripts throw it once a cycle (~25-30 s).

**ChainStartupFloor(sym)** is computed once from `BossScript(0 .. NumBossScripts-1)`: for every slot `i` with `bChain`
whose move is an attack and whose previous slot (`i-1`, wrapping) is an attack, `floor[prev.Symbol] = min(floor,
Move(slot.Move).Startup)`. Classes no script chains after return 9999. (Today: BFast 12, BHeavy 12, BFeint 14, BKiller —.)

## 2. Perception (what the keeper may see)

`Snaps[F % SnapRing]` = the state at the START of frame F: recorded at the end of `RecordFrame` after step F-1 (and once
in `BeginEncounter` for the fight's first frame). A player commitment made at frame e (before step e) first appears in
`Snap[e+1]`. At decision frame D the keeper sees **`SnapAt(D - PerceptionFrames)`** for everything about the player
(state, move, phase, progress, guard, parry, i-frames, armor, weapon, chain, health, sha-chi, position). So a commitment
at frame e is invisible at `D = e + 6` and visible at `D = e + 7` (a test asserts exactly this).

Player EVENTS (commits, the player's swing outcomes) wait in a ring and are applied to the perceived state (last player
outcome, last player commit frame) only once `E.Frame <= Duel.Frame - PerceptionFrames - 1` (processed in RecordFrame,
where `Duel.Frame` is the next frame). The keeper's OWN state and own swing outcomes are current (it knows its body).

Geometry in snapshots: `PX, PY` (player) and `BX, BY` (keeper) from the `FDuelGeometry` of that frame, mirrored if asked.
Velocities use `SnapAt(D-6)` and `SnapAt(D-10)` (4 frames apart).

## 3. The observation vector (HWRLTypes.h `EObs`, 103 floats, layout version 2)

Let `B` = the keeper now, `P = SnapAt(D - 6)`, `P2 = SnapAt(D - 10)`, `L` = the latest snapshot (keeper position).
All one-hots are exactly one 1 or all zeros as documented; everything is clipped as the header says.

* SELF: health/max; sha-chi/max; `EFighterState` one-hot (7); current move one-hot over the 22 boss moves (zeros unless
  Acting); phase one-hot (attack startup `T < Startup`, attack active `T < Startup+Active`, attack recovery, other move;
  zeros unless Acting); `T / TotalFrames`; `FramesUntilActionable/60` [0,2]; hyper armor; i-frames; `StunLeft/90` [0,1];
  last own swing outcome one-hot (EHitOutcome index 0..4); frames since it /120 [0,1] (1 if none); defended since the
  last decision; `StringAttacks / 3` [0,1]; grab cooldown left / 180; killer cooldown left / 600; chain window.
* PLAYER (perceived): distance(L keeper, P player)/500 [0,3]; radial velocity = `(|r(D-10)| - |r(D-6)|)/4 / 10` [-2,2]
  with `r = player - keeper` inside each snapshot (+ = closing); lateral velocity = `dot((P.player - P2.player)/4,
  leftOf(facing)) / 10` [-2,2] where `facing = normalize(P.keeper - P.player)` and `leftOf(x, y) = (-y, x)` (+ = the
  player moving to its own left); bearing of the perceived player from the keeper's committed facing when the keeper is
  Acting an attack (`cos = dot(F, d)`, `sin = F.x*d.y - F.y*d.x`, F and d unit, d from L keeper to P player), else
  (sin 0, cos 1); health; sha-chi; state one-hot (7); player move one-hot over `PLight1..PSwitch` (13) (index `Move-1`,
  zeros unless Acting); phase one-hot (4, as for SELF with the player's move); progress; `max(0, P.UntilActionable - 6)/60`
  [0,2]; guard held; guarding; parry live; i-frames; hyper armor; weapon one-hot (2); chain depth /3 [0,1]; the
  perceived last player swing outcome one-hot (5); `(D - perceived last player commit)/60` [0,2].
* CONTEXT: frame advantage `(max(0, P.UntilActionable - 6) - B.FramesUntilActionable)/30` [-2,2]; `D/60/180` [0,1];
  `FightIndex/3` [0,1]; swing deficit `(Target - rate)/60` [-1,1] with `rate = (own attack commits in the last 1800
  frames) * 3600 / max(600, min(D, 1800))`; `(D - LastOwnSwingFrame)/300` [0,1].

Tokens: `Session->Tokens` copied row by row (newest first), zero rows after `NumTokens`.

## 4. History tokens, labels, READ

A token covers one decision window `[OpenFrame, CloseFrame)`. `RecordFrame` updates the OPEN tokens from ground-truth
events (every event of frame f belongs to the token whose window contains f):
* player Commit (incl. guard raise, `Sym = Block`) → the window's first one sets `Answer = E.Sym`, `AnswerFrame = E.Frame`;
* keeper Outcome for the token's move → `Outcome` (first one); `Damage > 0` → `bDealt`;
* player Outcome with `Damage > 0` → `bTaken`; player swing Blocked / Parried by the keeper → `bDefendedSinceDecision`;
* each frame, for the current token: `bGuardSeen |= player guard held`, `Movement = PlayerMovement`.

`AnswerOf(T) = Answer` if set, else `Block` if `bGuardSeen`, else `Movement`.

A token is FINALISED (pushed into the session, newest first) only when `CloseFrame >= 0 && CloseFrame + 6 <= Duel.Frame`
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

`FRLSession::Reset()` zeros everything. `BeginEncounter(Session, Duel, Geo)`: finalise open tokens into the session,
`Session->FightIndex = Session->FightsBegun++`, reset every per-fight member, record `Snap[Duel.Frame]`,
`NextDecisionFrame = Duel.Frame`.

## 6. FRLBrain (the game)

* `BeginEncounter(Seed)`: per-fight reset only (`bBegun = false`, counters, `Last`). The session persists.
* `Think(Duel, Geo, Out)`: if no session → false. First call of a fight: `Obs.BeginEncounter(Session, Duel, Geo)`. If
  not ready (no policy) or not a decision point → false. Else: observe, mask; if `Session->HiddenSize !=
  Policy->HiddenSize()` zero it and set the size; `Forward(obs, tokens, mask, Session->Hidden, Session->Hidden, Out)`;
  `A = Out.Argmax` (greedy, RL.md §8); `Aux(Session->Hidden, A)` → predicted answer + p; `ApplyAction(Duel, A, &read)`.
  Decision record: `Kind = Policy`, `ScriptIndex = -1`, `Scripted = None`, `Chosen` = the committed move (None for
  Wait / continue), `Slot` from the move (attack → Attack, guard / counter → Defend, else Reposition), `bChain` = chain
  window, `PrevOutcome` = last own outcome, `FrameAdvantage = Duel.FrameAdvantage(Boss)`, `ReadBits = log2(12) -
  H(aux)`, `Predicted`/`PredictedP`, `Top[3]` = the three most probable actions (`Move` = ActionMove or None for Wait,
  `Expected` = logit, `Score` = `Decision` = prob), `Value`, and a `Reason` like
  `"policy SweepLeft p=0.83 (Wait 0.08) | read StepL 0.82 | V +0.41"`.
* `OnFrame`: `Obs.RecordFrame(...)` once begun.

## 7. The environment (RL/native)

Per env: `FEncounter Enc` driving an adapter brain (an `IBossBrain` whose `Think` returns false and whose `OnFrame`
forwards to the env's `FRLObserver::RecordFrame`), `FSimArena`, `FPlayerBot`, the player profile, an `FRLSession`, the
observer, and its own `FRandom` (fight seeds, start distances).

Frame (identical order to `RunEncounter`): frame start → `Enc.ThinkBoss(Arena.Geometry())` → if decision point: PAUSE
(return to the trainer) / apply the pending action → `Bot.Act` → `Arena.LatchCommits` → `Arena.SnapshotPreStep` →
`Enc.StepFrame(Arena, Bot.MovementSym())` → `Bot.OnEvents` → `Arena.Integrate` → habit switch check (`switch_after >= 0
&& Session.BossSwingsSeen >= switch_after` → `Bot.SetHabitPhase(1)`) → fight end? → next frame.

`Step(A)`: take `PrevDealt = Stats.PlayerDamageTaken`, `PrevTaken = Stats.BossDamageTaken`, `PrevAttacks`,
`PrevReads`; apply A at the paused frame; simulate until the next decision point of the SAME fight or the fight's end.
* `reward = (dealt / Tuning().PlayerHealthMax) - (taken / Tuning().BossHealthMax)` + terminal: player died +1, keeper
  died -1 (keeper death dominates if both), timeout 0 (fight frame >= `max_fight_seconds * 60`). Normalise by the
  Tuning() maxima (immortal fights have 1e9 health).
* `cost` = the aggression floor as a per-window hinge (RL.md §4.5): `max(0, target_spm - SwingsPerMinute(frame)) / 60 *
  step seconds`, where `SwingsPerMinute` is the keeper's own attack rate over the last 30 s of the fight (at least a
  10 s span); 0 in the first 15 s (the opening approach). The first version was a linear swing debt
  (`target/3600 * frames - swings`): summed over the population, a surplus against some players paid for a deficit
  against others, and B0 caught the keeper swinging 45/min at a constant parrier (the script: up to 57).
* `aux_label = Obs.AnswerToLastDecision()` read BEFORE any fight rollover; `read_counter = ReadCountersLanded() > PrevReads`.
* Fight end: `fight_done`, `result`; `FightInSession++`; if it reaches `fights_in_session` → `session_done`, start a new
  session (pending spec, `Session.Reset()`); else start the next fight on the same session. Either way run to the new
  fight's first decision (damage before it is not attributed to this step).
* New fight: `seed = Rng.GetUnsignedInt() & 0x7fffffff`; `Enc.Begin(&Adapter, seed, immortal)`; `Arena.Reset(uniform
  start distance)`; `Bot.Reset(profile, seed * 7919 + 17)`; habit phase from the session's swing count;
  `Obs.Config.TargetSwingsPerMin = target_spm`; `Obs.BeginEncounter(&Session, Enc.Duel, Arena.Geometry())`.
* Env i's stream: `SplitMix64(batch seed + i)`. Results are identical for any thread count.
* `MakeProfileFromSpec`: kinds 0-5 → `MakeBotProfile(kind, skill)`; kind 6 → `MakeBotProfile(Varied, skill)` with
  `Kind = Habit`, the tables, noise, adapts; then every override `>= 0` replaces the profile value. Kind 7 (exploiter
  policy) is reserved for RL-4: until implemented, treat as kind 3.
* `FRLEnvBatch`: a persistent worker pool (no thread creation per step); `ParallelFor` over envs in contiguous chunks.
* `hwrl_eval_sessions`: `sessions` sessions of `fights_in_session` fights vs the spec's player, the brain deciding in C++
  (arm 0 `FScriptBrain` with `script_index`; arm 1 `FClassicBrain` Hellwalker with a fresh `FPlaystyleModel` per session,
  `RefSwingsPerMin = 62`, `ApplyScriptTuning(script_index)`; arm 2 `FRLBrain` with a fresh `FRLSession` per session),
  same frame loop and habit switches as the env, summed into `HWRLEvalStats`. Deterministic in `seed`.

## 8. The network and the .hwrl format

Defined in `HWRLPolicy.h` (and implemented there). `RL/model.py` must define exactly that network:

```python
class HellwalkerNet(nn.Module):
    def __init__(self, obs_dim=102, num_actions=23, aux_classes=12, token_vocab=(10, 13, 6, 5, 10, 4),
                 history_tokens=32, enc_hidden=256, embed_dim=32, hidden=256, recurrent=True, side=0): ...
    def initial_state(self, batch: int, device) -> Tensor            # [B, hidden] zeros
    def forward(self, obs, tokens, mask, h0, starts):                  # sequence form, for PPO
        # obs [T,B,obs_dim] f32, tokens [T,B,K,F] int (int8 ok), mask [T,B,A] bool, h0 [B,H],
        # starts [T,B] bool: reset the hidden state to zeros BEFORE step t (a new session)
        # -> logits [T,B,A] (masked to -1e9), value [T,B], feats [T,B,H] (hidden after each step), hT [B,H]
    def step(self, obs, tokens, mask, h):                              # one decision, for rollouts / play
        # obs [B,obs_dim] ... -> logits [B,A], value [B], h_new [B,H]
    def aux_logits(self, feats, actions) -> Tensor                    # [..., aux_classes]; actions long [...]
```

Layers: `enc1 = Linear(obs_dim, enc_hidden)`, `enc2 = Linear(enc_hidden, enc_hidden)` (tanh after each);
`tok_emb = ModuleList(Embedding(v, embed_dim) for v in token_vocab)`, `tok_age = Parameter(zeros(K, embed_dim))`
(token vectors = relu(sum of field embeddings + age[k]); pool = masked mean over tokens with any non-zero field;
zeros when none); `gru = GRUCell(enc_hidden + embed_dim, hidden)` or `ff = Linear(...)` + tanh; `pi = Linear(hidden, A)`,
`v = Linear(hidden, 1)`; aux: `aux = Linear(hidden, aux)`, `aux_action = Parameter(zeros(aux, A))`,
`aux_logits = aux(feats) + aux_action[:, actions].T`. Export names: `enc1.*`, `enc2.*`, `tok.emb{f}`, `tok.age`,
`gru.weight_ih/weight_hh/bias_ih/bias_hh` (or `ff.*`), `pi.*`, `v.*`, `aux.weight`, `aux.bias`, `aux.action`, and
the `meta.*` scalars. Parity: torch vs `hwrl_policy_forward` within 1e-4 on logits / value / hidden.

## 9. The trainer

* **Env wrapper (env.py):** owns the batch, numpy buffers, pending player specs from `players.py`; after every
  `session_done` it assigns the env's next-next player (the C++ side starts sessions with the pending spec).
* **Rollouts:** N envs (4096 on the GPU; fewer to debug), T = 128 decisions; store obs, tokens, masks, actions,
  log-probs, values, rewards, costs, session boundaries (`starts`), aux labels, and the hidden state at every BPTT chunk
  start (every 32 steps).
* **Returns:** GAE (γ 0.995, λ 0.95) with session ends as episode ends (fight ends are NOT: the keeper should invest
  in reading you in fight 1 that pays in fight 2 — the meta-RL point). Reward normalisation by a running std of the
  discounted return; the cost is folded in as `r - λ_c * c` with `λ_c >= 0` updated each iteration by
  `λ_c += lr_λ * (mean cost per decision - budget)` (budget 0.003: a hinge never quite reaches 0), clipped at 0.
* **Loss:** PPO clip 0.2 on masked policies; value loss × 0.5; entropy bonus (masked) 0.01 → 0.001 linear; aux
  cross-entropy × 0.1 on steps with a label (aux conditioned on the taken action); 4 epochs × 8 minibatches of whole
  32-step sequences; Adam lr 3e-4 → 3e-5 linear; grad norm 0.5.
* **Population (players.py):** reference bots (kinds 0-5, skill U(0.2, 1)) in ~20% of sessions; habit players (kind 6)
  otherwise: each class row ~ Dirichlet(α · 1) with α per curriculum band — band 0 [0.05, 0.2] strong habits,
  band 1 [0.2, 1], band 2 [1, 5], band 3 [5, 50] near random; timing: parry_aim U(1,7), step_aim U(3,14), timing_sigma
  U(0.8,4.5), react_mean U(10,25), react_sigma U(1.5,4), feint_read U(0,0.6), killer_read U(0.3,0.95), punish_rate
  U(0.2,0.9), aggro_rate U(0.05,0.6), heavy_rate U(0.05,0.4), switch_rate U(0,0.3), preferred_range U(140,230),
  chain_len {2,3}, noise U(0,0.1), adapts ~ Bernoulli(0.5); habit switch with probability p_band (0.1, 0.25, 0.35, 0.35)
  at `switch_after ~ U{15..60}` boss swings, table B a fresh draw; `fights_in_session ~ U{1..4}`. Curriculum: start with
  band 0 unlocked; unlock the next band when the keeper's win rate against the newest band ≥ 60% over the last ~2000
  fights; sample bands ∝ (1, 1, 1.5, 1.5) over unlocked bands so old habits are not forgotten. Held-out players come from
  a separate seed stream.
* **Stages (train.py --stage):** `rl1` feed-forward vs one Habitual bot (skill 0.8), 1-fight sessions (sanity check);
  `rl2` recurrent vs the population (the reader); `rl3` = rl2 settings continued with the evaluation gates; `rl4`
  exploiter league (later). Logs: TensorBoard + CSV in `RL/runs/<run>`; checkpoints + exported `.hwrl` in
  `RL/checkpoints/<run>`.
* **Evaluation (eval.py, RL.md §7):** ThesisSim `--brain rl --policy` (B0 verdict); the adaptation curve on held-out habit
  players (P(keeper attack hits) vs the k-th keeper attack of the session, and around a scheduled habit switch); not a
  bully (vs band-3 players the RL keeper's dmg/min ≈ the script's, via `hwrl_eval_sessions`); classic vs RL on identical
  seeded players; a report in `RL/reports/`.
* **The reading test (habits.py)** — the clearest evidence: four groups of pure-habit held-out players (always parry /
  block / step left / step right). A reader starts with the same mix against everyone and diverges toward each group's
  counter as the session goes on (move-mix Jensen-Shannon divergence early vs late), and after a scheduled habit switch
  its hit rate dips, then recovers with the new counter. The population-average hit-rate curve (eval.py) rises for
  strong and weak habits alike (pressure accumulates over a fight), so it cannot tell reading from strength.
* **Curriculum, as it turned out:** the keeper out-healths every simulated player (1100 vs 360 hp) and wins ~100% of
  fights from the start, so the ≥60% win-rate gate opens every band within a few iterations — the population is broad
  from the beginning (as in RL² practice). The damage exchange, not the win, carries the learning signal.
