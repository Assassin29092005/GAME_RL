# PLAN.md — A Boss That Reads You

**Target project:** `D:\GGGG\game` (UE 5.8, currently empty)
**Status:** DRAFT v4 — restructured around the core problem statement. Awaiting your sign-off.

---

## 1. THE CORE PROBLEM STATEMENT

> **A game where the boss understands the playstyle of the player, adapts to it, and counterattacks.**

That sentence is the product. Three verbs, and the project succeeds or fails on all three:

| Verb | What it demands | Where it lives |
|---|---|---|
| **understands** | A model of *this* player, built from watching them fight — not a difficulty slider, not a script | **§2 — the playstyle model** |
| **adapts** | Boss action selection genuinely conditioned on that model | **Phase B** |
| **counterattacks** | The adaptation converts into *pressure* — the boss punishes the habit it found | **§2.4 + Phase B3** |

**Everything else in this document is substrate.** Combat mechanics, sha-chi, ghoststep, weapon
switching, greybox art, animation — none of it is the point. It exists so that the adaptation has
something to adapt *about*, and so an observer can see it happening. When scope has to be cut, it is
cut from the substrate, never from the three verbs.

### 1.1 What "success" looks like
A person who has never seen the game plays it twice. The boss fights them differently the second
time, in a way they can *name* — "it stopped falling for my dodge-left", "it started baiting my
parry". And it is **harder**, not merely different.

### 1.2 The failure mode that nearly sank v1 — and still governs the design
Damage taken = `swings × P(hit) × damage`. An "adaptive" boss that reacts to a good player by
*aborting* unsafe attacks throws **fewer** swings, so it deals **less** damage — it is **strictly
easier** against the player it is supposed to challenge, while converging with the scripted boss
against a button-masher who never parries.

Real adaptive opponents do not get harder by attacking **more**. They get harder by **closing the
free-damage gaps in their own behaviour**.

**Two rules, binding on everything in Phase B:**
1. **Every abort transitions to a defensive or evasive state — never to neutral.** An abort must deny
   the punish it would otherwise hand over.
2. **Aggression floor, measured not asserted:** the adaptive boss's swings/min **and** damage/min must
   be **≥** the scripted boss's at matched player skill.

**The floor is a runtime invariant, not a playtest observation.** *(Grafted from the runner-up design —
representation-agnostic, ~10 lines at the exchange boundary.)*
```cpp
Deficit = RefSwingsPerMin - Telemetry.SwingsPerMin();
if (Deficit > 0) { PressureBias += Deficit * FloorGain; }
```
measured against a reference curve recorded from the scripted boss in Phase A, **with the correction
term logged into A2's CSV — so any chronically-correcting policy self-identifies as wrong.** This
converts §1.2 from something a playtester notices too late into something the design cannot silently
violate.

This is why the boss needs its own **guard and evade** (§3, Phase A3). Without them, "adapt" can only
ever mean "attack less", which means "be easier".

---

## 2. THE PLAYSTYLE MODEL — the heart of the project

> **Design comparison complete.** Three approaches were designed independently and judged blind:
> **predictive sequence model 45** · feature-vector/archetype **44** · online response-learning **38**.
> The sequence model wins narrowly; §2.2 adopts it **with the judge's corrections applied** and with
> the two best ideas from the runners-up grafted on (§1.2's floor controller, §2.4's Read Meter).

### 2.1 What is observed

**Cadence first, because it is the thing that makes the whole design viable.** A record is emitted at
**every commitment event** — not once per exchange, and emphatically not once per dodge:

| Trigger | Emits |
|---|---|
| Player commits — attack startup begins, parry press, ghoststep begins, guard raised, weapon switch | that symbol, with frames elapsed since the last boss commitment |
| A boss damage window resolves with **no** player commitment | a movement symbol (Neutral / Advance / Retreat) |
| Boss commits to any action | its own symbol |
| **Watchdog** — 45 frames with no emission | a movement symbol, **so a turtling player still generates data** |

All emission rides the `UCombatComponent` frame cursor, in the same pass as hit detection. Nothing is
sampled on a timer.

The signals themselves are cheap and exact because the combat layer already computes every one (§3,
Phase A1):

| Signal | Source |
|---|---|
| `EHitOutcome` — Hit / Whiff / Blocked / Parried | the damage-window edge |
| Frame advantage at decision time | integer delta of frames-until-actionable |
| Player defensive choice — block / parry / ghoststep / nothing | input, timestamped |
| Ghoststep **direction in target space** | lock-on (§3 A0) — *camera space would be meaningless* |
| Player attack: weapon, chain depth, whether it whiffed | combat component |
| Range at engagement start; time-to-re-engage after a knockdown | transform |
| Reaction latency — frames from telegraph to player input | frame cursor |

### 2.2 The representation — an interleaved variable-order Markov model

**The fight is one interleaved symbol stream of player *and* boss commitments.** The model learns to
predict the player's next symbol from recent context; "playstyle" is literally the low-entropy
structure in that stream, and **unpredictability is the player's counter-play.**

**Alphabet — 20 symbols.** Player 0‑11: `Neutral, Advance, Retreat, Light, Heavy, Block, Parry, StepF,
StepB, StepL, StepR, Switch`. Boss 12‑19: `Fast, Heavy, Feint, Killer, Guard, Evade, Approach,
Retreat`. Ghoststep direction is **in target space** — which is exactly why A0's lock-on is a hard
dependency.

**Three deliberate exclusions from the symbol**, each protecting estimability:
- **Weapon** — the boss already *knows* the equipped weapon at prediction time; it is observable state,
  not something to predict. It conditions which counters are *feasible*, not the prediction. `Switch`
  stays its own symbol so switch-patterns are still caught.
- **Timing** — bucketing latency into the alphabet multiplies `|A|` by the bucket count and destroys
  order‑2. It rides as a separate per-symbol EMA of reaction delay. **The n-gram answers *what*, the
  delay table answers *when*** — factoring them turns `|A|×T` cells into `|A|+T`.
- **Spacing** — distance gates counter *feasibility*, it does not need predicting.

**Representation:** flat float arrays, direct-indexed by a base‑20 packed context. No hash map, no
allocation after construction, **deterministic iteration order** (which B1's determinism test needs).
Plain struct, not a `USTRUCT`, so UHT never sees the big arrays.

```cpp
static constexpr int32 A = 20, NP = 12;
float N0[A];        //     20 floats
float N1[A][A];     //    400 floats   1.6 KB
float N2[A*A][A];   //  8,000 floats    32 KB
float DelayMean[NP], DelayW[NP];   // timing sidecar, frames
float Gain;  uint8 H[2];  int32 Depth;
```
**~34 KB.** *(The design proposed an order‑3 table at 640 KB; the judge showed it is dead on arrival —*
*see the corrections below — so orders 0‑2 only.)*

**Update — recency by gain-inflation, not a decay pass.** Rather than decaying every cell each symbol,
inflate the increment: `Gain /= Lambda` then add `Gain`; a cell's effective count is `cell/Gain`.
Renormalise on overflow, roughly every 1600 symbols. Two lines instead of a streaming pass at 4 Hz.

**Prediction — Witten-Bell interpolation down the backoff chain:**
```
w   = n / (n + U)                      // trust in THIS order
P_k = w * (count/n) + (1-w) * P_{k-1}  // recurse to a uniform root
```
Witten-Bell specifically, not Katz or Kneser-Ney, for one reason that matters here: **it has no tuned
discount parameter.** Its escape weight is a principled statement of *"this context has 3 samples over
3 symbols, so trust the parent"* — **which is the confidence gate, done continuously inside the model
instead of bolted on as a threshold.** That replaces v3's `|L−R| ≥ 2·√n` step function with graceful
degradation instead of a cliff.

Order‑0 is **seeded with a hand-authored typical-player frequency vector** at ~10 effective
observations, so second 5 of encounter 1 is not a uniform coin flip. Washes out after ~30 real symbols.

**No RNG in the model.** The only randomness in the brain stays the seeded `FRandomStream` used for
tie-breaks, so B1's determinism test holds.

#### Three corrections the judge demanded — applied
1. **Order‑3 is deleted.** Half-life 32 caps *total* effective evidence at `1/(1−λ) ≈ 46.6`
   observations — permanently, at every order, because the decay is in symbol count, not wall clock.
   An order‑3 row would asymptote under n_eff 1. The 640 KB table would be dead weight, and the claim
   that "encounter 3 has ~1000 symbols" buys nothing when the window discards everything past ~100.
   **Half-life is raised (and may be per-order) so order‑2 rows reach usable n_eff.**
2. **Witten-Bell needs a thin-data floor.** At `n=1, U=1` the escape weight is `w = 0.5` — a context
   visited **once** gets half the mass on that single symbol, spikes the distribution and can fire a
   substitution **at exactly the sample size the under-power argument is about.** Fix: an `n_eff` floor
   before blending a row, or `w = n/(n + β·U)` with β ≈ 2–3.
3. **Score expected payoff *per frame of commitment*, not per slot.** The design's swings invariant is
   per-*slot*, and the legibility filter (`Startup(cand) ≥ Startup(scripted)`) only ever removes faster
   candidates — so substitution is systematically biased toward *longer* actions and slots-per-*minute*
   falls. Swings-per-slot is monotone; swings-per-minute is not. Dividing by commitment frames fixes it,
   and §1.2's floor controller catches the residue.

### 2.3 Convergence — the objection dissolves

**The under-power problem was largely an artefact of counting the wrong thing.** v3 assumed 15–40
observations per encounter — but that is the count of *one symbol type* (dodges), not the length of the
stream. A symbol is emitted at **every commitment event**: player attack startup, parry press,
ghoststep, guard raise, weapon switch; boss action commit; a movement symbol when a boss damage window
resolves with no player commitment; and a 45-frame watchdog so **a turtling player still generates
data.**

That is **~3–5 symbols/sec — 270–450 symbols in a 90 s encounter**, roughly 10× the assumed figure.

| Order | Usable after | Wall-clock |
|---|---|---|
| 0 | ~20 symbols | **~5 s** (non-degenerate from symbol 1, via the seeded prior) |
| 1 | n≈8 on the player's top 3–4 symbols | **15–25 s** — and order‑1 over the *interleaved* stream already means *"what you do after the boss does X"*, which is most of the useful signal |
| 2 | n≈8 in a specific bigram context | **60–90 s** — late in encounter 1, comfortably by encounter 2. For the tail it never happens, and backoff handles that silently instead of firing on noise |

**Honest answer to "is order‑2 estimable?" — not as a yes/no.** It is estimable for the contexts the
player actually visits and not for the rest, and Witten-Bell is precisely the mechanism that decides
which is which, per context, with nothing to tune.

**Still true, and still in:** persist within a session (encounter 3 knows what 1–2 learned — this is
what makes §1.1's naming test possible); reset on quit; **parry-timing *variance* stays cut** — the
timing sidecar tracks a mean and an evidence weight, not a variance.

### 2.4 How understanding becomes a counterattack

**The script owns *whether* and *when* the boss acts. The model only decides *which* action fills the
slot.** That single structural choice is what makes §1.2's aggression floor provable instead of hoped
for. Slots are typed `ATTACK / DEFEND / REPOSITION`, and at the instant one opens, **once**:

1. `Predict()` → distribution over the 12 player symbols, plus entropy.
2. `Read = log2(12) − entropy` — **bits of read**, in `[0, 3.585]`.
3. `Margin = lerp(3.0, 0.4, clamp(Read/2, 0, 1))`. **A weak read demands a payoff advantage no
   candidate can produce, so the boss silently plays the script.** A strong read makes substitution
   cheap. *Thin data cannot produce confident behaviour — structurally, not by policy.*
4. Candidates = same-slot-type actions, **plus** — only for a `DEFEND` slot, and only when the predicted
   symbol is whiffable — the attack actions. **The upgrade is one-directional: `ATTACK` can never
   become `DEFEND`.**
5. Filter by `Startup(cand) ≥ Startup(scripted)` — legibility rule (a), one comparison.
6. Score each by expected payoff per frame of commitment (§2.2 correction 3).
7. Substitute only if it beats the scripted action by more than `Margin`.

**The payoff matrix is where a correct prediction becomes a real counter**, and its authoring rule
carries the whole aggression argument: **defensive actions cap at +1; only attacking actions reach
+2/+3.** Feint vs predicted Parry = +3 (they press on the fake, eat the real one). Killer move vs
predicted Block = +3. Fast attack vs Parry = −3. **So payoff maximisation is biased toward attacking
in the *data*, not in code.**

**Timing the counter is what sells the thesis.** Choosing the anti-parry move is not the same as
*baiting* the parry. When the predicted symbol is `Parry` with `DelayMean[Parry] = 14` frames and
enough evidence weight, the brain picks the feint whose *fake* impact lands at frame 14 and whose real
impact lands after the parry window closes. That is the difference between *"the boss picked the right
counter"* and *"the boss baited me at the exact moment I always press."*

#### The Read Meter — grafted from the runner-up, and the single highest-value item here
The boss's **top‑1 predicted player action**, as an icon with a confidence bar, flashed for ~0.4 s
**immediately after** it lands a counter. Always on in debug.

> **Showing the guess *before* is telegraphing. Showing it *after* is testimony.**

It is free — the distribution is already computed — it requires no model change, and it is the only
zero-cost answer anywhere to the risk §8 names as the top design risk. **Build it on day one of B3,
before any tuning.**

Second free item: because the counter came from a predicted *symbol*, every decision emits a reason
string in the player's own vocabulary — `predicted StepL (p=0.61, ctx=B_Fast,StepL) → chose SweepLeft`
— which is **not** true of a feature-vector score or a learned bandit weight.

**Three legibility rules, as acceptance criteria — not a debug HUD.** The top risk on this project is
that adaptation reads as *cheating* rather than *intelligence*:
- **(a)** Every adaptive attack has **startup frames ≥ its scripted counterpart's.** Adaptation changes
  *which* readable attack the boss throws, never *how readable* it is.
- **(b)** **The brain may not re-select after the player's input is committed.** Sample once, at a
  defined point, and honour it. This is the line between reading *animations* and reading *inputs* —
  and it is exactly the line the source report praises Phantom Blade Zero for holding.
- **(c)** **Act on a habit one exchange later**, never instantly, so the player can perceive the causal
  link.

### 2.5 Guard rails
- **Adapt to habits, never to player strength.** A boss that adapts to a *struggling* player makes it
  worse — a death spiral.
- **Cap adaptation strength** so repeated failure cannot compound.
- Persist within a session; **reset on quit**.

---

## 3. MILESTONES

Restructured into three phases so the spine is visible: **A builds the substrate, B is the product,
C is enrichment.**

### Phase A — Substrate *(make adaptation possible and observable)*

**A0 — Skeleton.** `git init` + UE `.gitignore` first. C++ module; `AArenaCharacter` ×2; greybox arena
via **Modeling Mode**; Enhanced Input. **Lock-on (~70 lines)** — set `bUseControllerRotationYaw = true`
and `bOrientRotationToMovement = false` on lock, restore on unlock; interp Yaw only; run in
`PlayerTick`. **No navmesh, no AIController** (§5.1). Skeleton choice (Manny vs GASP UEFN) decided here
and never revisited.
> **Done:** `Build.bat` exit 0; strafe a full circle and the **character's** forward vector stays within
> N° of the boss. *(Asserting the camera would pass while the character faces away — which silently
> voids §2.1's target-space requirement.)*

**A1 — Combat offence.** `UCombatComponent`: health, combo chains from `UCombatAnimConfig`, **frame
cursor** (§5.2), **sweep-based hit detection** (§5.4), per-swing hit guard. `EHitOutcome` on one
delegate. `ResetEncounter()` + console exec.
> **Done:** all four outcomes provoked deliberately and logged correctly.

**A2 — Instrumentation.** *Pulled early: every later done-test consumes it.* On-screen numeric overlay
+ one CSV row per exchange — outcome, frame delta, boss action, tier, run id, **mean frame time**.
**If mean frame time exceeded 20 ms the run is discarded, not reported** — otherwise a slow session
silently produces bad frame data that Phase B then treats as evidence.
> **Done:** a fight produces a CSV that reconciles against the overlay.

**A3 — Defence, both sides.** Sha-chi, block, parry, Ghoststep (§5.3). **Boss guard + evade with their
own sha-chi pool** — mandatory, per §1.2.
**Parry pays a floor, always:** fixed sha-chi damage + a short uncancellable stagger. Adaptation varies
only the **size** of the reward. If the boss could cancel out of it, parry would be the tightest window
in the game for zero payout and play would collapse to block+dodge.
> **Done:** console exec injects a press at an exact frame offset — **9 frames before impact fails,
> 7 succeeds.** Guard breaks after 3 heavy blocks from full.

### Phase B — The product *(the three verbs)*

**B0 — Headless thesis simulator.** *Gates everything.* A few hundred lines, terminal, **no Unreal.**
Abstract player profiles × brains × §6's frame numbers. Sweep player skill; report damage-taken,
encounter length, swings/min.
> **Done:** skilled profile → adaptive damage-per-encounter **≥** scripted; masher profile → both
> lethal. **If this fails, the design is wrong and no engine work repairs it.** Also the permanent
> regression harness for every later tuning change.

**B1 — Brain, adaptation OFF = the control arm.** One `UBossBrainComponent`, plain C++ scoring
function, `bAdaptationEnabled = false`. One brain with a flag — *not* two separately authored brains,
which would differ in aggression and spacing as well as adaptation and confound the whole experiment.
All randomness from one `FRandomStream` seeded per encounter by `ResetEncounter()`.
> **Done:** (a) automation test on the decision function — fixed state, fixed seed, same action N times.
> **Flags are `EAutomationTestFlags_ApplicationContextMask`** — underscore, not `::`, which is a hard
> compile error in 5.8 and which every pre-5.5 snippet gets wrong. (b) At one script index, all four
> outcomes → **identical** output. (c) Same opener 5/5 from the same start distance.

**B2 — The playstyle model.** §2. Observation pipeline, representation, update rule, confidence gate,
recency weighting, session persistence.
> **Done, with a negative control:** dodge left 10× in the same context → **`Read` (bits) rises and
> the top‑1 prediction becomes `StepL`.** **Then play a balanced distribution for 30 symbols and assert
> `Read` falls back far enough that no candidate clears `Margin`** — i.e. the boss returns to the
> script on its own. *A positive test alone passes on a broken model.* Also assert the §2.2 correction‑2
> floor: **a context visited exactly once must not clear `Margin`.**

**B3 — Adapt and counterattack.** `bAdaptationEnabled = true`. Model becomes a term in the scoring
function. §2.4's three legibility rules enforced. **Every abort routes to a defensive/evasive state.**
> **Done:** at one script index, feed all four outcomes → the adaptive brain differs in **≥3** of them
> (the control differed in 0). **Plus §1.2's aggression floor, read from A2's CSV.**

**B4 — Prove it.** Runtime toggle between control and adaptive. **Identical move set, identical
per-move frame data and damage, identical HP — different selection policy.** The stat dump logs
**per-tier move-selection frequencies** alongside the HP/damage bytes: identical per-move numbers do
*not* produce identical damage when policies differ, and concealing that is what would look like
cheating.
> **Done — this is the project's actual deliverable:** **≥3 people**, both modes each, **order
> counterbalanced**, blind. Record damage-taken, attempts-to-clear, and which mode each names as
> harder. **Directional: the adaptive boss must be *harder*, not merely different** — "different" is
> satisfied by "easier", which is exactly §1.2's failure. Plus §1.1's naming test: can they *say* what
> it adapted to?

### Phase C — Enrichment *(cut first if time runs short)*

**C1 — Weapon switching.** Two weapons, genuinely different frame data. Switching is free — variety is
the player's counter-play to B2, so it must be cheap to use.
> **Done:** spam one weapon **or** alternate rigidly → counter-rate climbs in **both** cases. Vary
> genuinely → it falls. Thresholds named in advance.

**C2 — Animation.** C2a GASP locomotion · C2b retarget (one pack, attacks only) · C2c montage authoring.
**The only milestone with a real runtime cost** — motion matching queries a Pose Search database per
character per frame on the game thread. Profile `stat unit` / `stat anim` when it lands.

**C3 — Analysis.** Reason strings, per-mode counter-rate, offline comparison.
> **Done:** log the top three scored candidates with the chosen one; assert `chosen == argmax` on every
> decision. *Implementable only because the scoring is plain C++ (§5.1).*

### 3.1 Order and estimates

`A0 → A1 → A2 → A3 → B0 → B1 → B2 → B3 → B4`, then C. **B0 gates all of Phase B.**

| | A0 | A1 | A2 | A3 | B0 | B1 | **B2** | **B3** | B4 | C1 | C2a/b/c |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Range | 0.5–1 d | 1–2 d | 0.5 d | 2–3 d | 0.5–1 d | 2–3 d | **3–5 d** | **3–5 d** | 1 d + playtest | 1–2 d | 2–4 d each |

**B2 and B3 are the project.** Cut C entirely before touching them.

---

## 4. ASSETS

Nothing imported until a milestone needs it. Phases A and B need **no** art assets at all.

| When | Pack | Verdict |
|---|---|---|
| A + B | *(none)* | Engine content + Modeling Mode |
| C2a | **GASP** | Project template, not an add-to-project pack. Create standalone, diff its plugin list against `game.uproject` and enable the delta, migrate the **CMC** character only. **Epic publishes GASP for 5.8 — version mismatch is not the risk; migration scope is.** 30-min spike to get that one number |
| C2b | **Fighter Animation Pack** (9CG) | IK Rig + IK Retargeter |
| C2b | **Free Animation Library**, **RamsterZ** | Gap-fill |
| C2 | **Paragon character** | **Boss only, native skeleton, NO retarget** — removes a whole pass. Constraint: reference sockets **by name from the weapon data asset**, never assume Manny bone names |
| optional | **Slash Trail FX** | Aids parry readability — legitimately serves §2.4(a) |
| optional | **MetaHumans** | Now affordable on the A5000. Still contributes **zero** to the three verbs. Presentation only |
| rejected | **Lyra** | Its value is GAS + a modular framework — a larger commitment than this entire combat layer |
| rejected | **Stack O Bot** | Saves nothing a greybox floor does not |
| reference | **Content Examples** | Animation map is a 10-min lookup. Not a dependency |
| rejected | **Ultimate Difficulty Scaling** | **B4 fixes HP and damage across modes, so a stat scaler has nothing to scale** |
| rejected | **Lock On Target** (plugin) | Code plugin; support trails 5.8, needs recompiling, may not build. ~70 lines is cheaper and certain |
| deferred | Environment art | After B4 |

**Blender: not in the pipeline.** `ModelingToolsEditorMode` is already enabled, so greybox geometry is
authored in-editor with no export/import step. Additive after B4, never a dependency.

---

## 5. ARCHITECTURE

| Decision | Choice |
|---|---|
| Pawn base | `ACharacter` + CharacterMovementComponent |
| Boss brain | **Plain C++ `UBossBrainComponent`. No StateTree** — §5.1 |
| Frame clock | **Tick-driven frame cursor. Not `FTimerManager`** — §5.2 |
| Frame data | `UCombatAnimConfig` — integer frame counts, designer-editable, no rebuild |
| Ghoststep | **`FRootMotionSource_MoveToForce`** — §5.3 |
| Hit detection | **Per-tick sweeps, not overlap events** — §5.4 |
| Decision arbiter | Frame advantage authoritative; playstyle model and hit outcome are score terms |
| Modes | One brain, `bAdaptationEnabled` |
| Networking | None |

**Plugins:** `ModelingToolsEditorMode` only — **remove `GameplayStateTree`**. `EnhancedInput` is
enabled by default and `Config/DefaultInput.ini` already sets the player-input classes, so no
`.uproject` edit is needed, only the `Build.cs` dependency. `GameplayTags` is an engine module.

**Not built:** RL, GAS, Lumen/Nanite tuning, Motion Warping (it warps *existing* anim root motion, and
there is none before C2), procedural terrain.

### 5.1 Why StateTree is cut *(verified in the installed 5.8.2 source)*
1. **Utility scores are discarded.** `TrySelectChildrenWithHighestUtility` computes per-candidate scores
   into a **function-local** array and throws them away on return — no accessor, no delegate. **C3's
   done-test would be unimplementable.**
2. **No seed injection.** `UStateTreeComponent::StartTree` builds `FStartParameters` with only
   `.InitialGlobalParameters` and `.ExecutionExtension` (StateTreeComponent.cpp:195-199, read directly).
   `RandomSeed` exists at StateTreeExecutionContext.h:452 and is **never set**, so the stream falls back
   to `FPlatformTime::Cycles()`. B1's determinism test would fail intermittently — and worse, *pass* on
   whichever runs you happened to try.

Also removed by this: `FStateTreeMoveToTask` would have silently required an `AAIController` **and** a
`NavMeshBoundsVolume`; its failure mode is the task returning `Failed` and the boss simply never
approaching, which reads as a brain bug. And `SendStateTreeEvent` only *enqueues* — a decision would
land **next frame at the earliest**. All boss movement is code-driven from `UBossBrainComponent`.

### 5.2 Why timers are cut *(verified)*
`GetTimerManager().Tick()` is at **LevelTick.cpp:1816** — after `RunTickGroup(TG_PostPhysics)` (1778),
before `TG_PostUpdateWork` (1877). Enhanced Input runs in `PlayerTick` at PrePhysics. **So a window
opened by a timer on frame N is invisible to frame N's input.** Timers also quantise one-shots up by a
full frame. An 8-frame parry window on timers is not buildable.

Instead, one cursor in `UCombatComponent::TickComponent`:
```cpp
FrameCursor += DeltaTime * 60.0f;
while (FrameCursor >= NextPhaseBoundary) { AdvancePhase(); }  // one long frame crosses
                                                              // several boundaries, in order
```
Input carries its own timestamp — the handler records `PressedAtFrame`, and the combat tick asks
`Press ∈ [WindowOpen, WindowClose)`. **That removes tick ordering from the correctness argument.**

### 5.3 Ghoststep *(verified)*
`AddActorWorldOffset` on a CMC character is wrong: CMC recomputes `Velocity` from the actual move only
*inside its own substep*, so an external offset never becomes velocity — the AnimBP sees speed 0 while
the capsule translates (**foot-sliding by construction**) and braking acts on stale velocity. `bSweep`
defaults false, so the capsule can land in geometry and be popped out along an arbitrary normal
(**non-deterministic**, breaking B1).

Use `ApplyRootMotionSource` (CharacterMovementComponent.h:2807) with `FRootMotionSource_MoveToForce`
(RootMotionSource.h:545): `StartLocation`, `TargetLocation`, `Duration = 24.0f/60.0f`,
`AccumulateMode = Override`, `FinishVelocityParams.Mode = SetVelocity` at zero. Store the returned
`uint16`; `RemoveRootMotionSourceByID` (:2822) on every interrupt path. *This is the primitive GAS's
`UAbilityTask_ApplyRootMotionMoveToForce` wraps — and it needs no GAS.*

### 5.4 Hit detection
`OnComponentBeginOverlap` fires on overlap *state change*. If the weapon volume is **already**
overlapping when the active window opens — common at these ranges, guaranteed after a Ghoststep that
ends inside the boss — **no event fires and a visible connection logs as `Whiff`**, silently corrupting
the playstyle model, the frame-advantage input, and every CSV row. It also breaks i-frames.

Sweep each tick of the active window (`SweepMultiByChannel`, socket previous→current), from the same
frame-cursor pass. Keep `AlreadyHitThisSwing`, cleared at active-open. **Whiff is defined as "leaving
Active for any reason with `AlreadyHitThisSwing` empty"** — so an attack cancelled mid-active still
raises exactly one outcome.

---

## 6. FRAME DATA — placeholders

All wrong on purpose. Their job is to give every done-test a pass criterion and to feed B0.

| Parameter | Placeholder |
|---|---|
| Parry window | 8 frames before impact |
| Parry reward floor (always paid) | 15 boss sha-chi + 20-frame uncancellable stagger |
| Hitstun / Blockstun frames | 18 / 11 |
| CancelWindowStartFrame, fast / heavy | 20 / 44 |
| Ghoststep i-frames / recovery / cost | frames 3–18 of 24 / 10 / 12 sha-chi |
| Sha-chi max / regen | 100 / 8 per sec after 1.5 s |
| Block chip | 20 % |
| Block sha-chi drain, light / heavy | 10 / 35 |
| Fast blade startup/active/recovery | 8 / 4 / 14 |
| Heavy blade startup/active/recovery | 22 / 6 / 30 (hyper-armor from 10) |
| Boss guard sha-chi pool | 80 |

Frame advantage is computed, not stored: `ActionableFrames = min(Startup+Active+Recovery,
CancelWindowStartFrame)`, and the delta is one line in the brain —
`Target->Combat->GetFramesUntilActionable() - Combat->GetFramesUntilActionable()`. *(Hitstun and
blockstun are in this table because without them frame advantage has no data source for the two
outcomes a skilled player generates most — an earlier draft omitted both.)*

---

## 7. TOOLING

**Build** (editor closed):
```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" gameEditor Win64 Development -Project="D:\GGGG\game\game.uproject" -WaitMutex
```

**Headless automation — `-ExecCmds`, not Python.** `-ExecutePythonScript` needs `PythonScriptPlugin`
(experimental, not enabled) and takes an `=`, not a space; without the plugin the switch is parsed by
nothing and the editor just launches, which reads as a hang. Nothing in A or B needs Python:
```bash
UnrealEditor-Cmd.exe "D:\GGGG\game\game.uproject" -ExecCmds="Automation RunTests <Name>;Quit" -unattended -nop4 -nosplash -NullRHI
```

**Source control:** `git init && git lfs install`; ignore `Binaries/ Intermediate/ Saved/
DerivedDataCache/ .vs/`; LFS-track `*.uasset`, `*.umap`. **Tag each milestone when its done-test
passes**, so B4's A/B can be re-run against a known-good control build.

**For every quantitative test run:** `bUseFixedFrameRate=true` / `FixedFrameRate=60` (or at minimum
`t.MaxFPS=60`), so frame counts are deterministic and A3/B2/B3 are reproducible.

**Hard rules:**
- New `UPROPERTY`/`UFUNCTION` → **full rebuild, editor closed.** Live Coding loads stale reflected code.
- **Montage assets are safely shared across characters** — `UAnimMontage` is immutable and play creates
  a per-actor `FAnimMontageInstance`. **The real trap:** `UAnimNotify`/`UAnimNotifyState` objects live
  on the **sequence asset** and are shared by every actor playing it — **never store per-character state
  in a notify member.** Notifies may only read config and call into the owning `UCombatComponent`.
- **Enhanced Input:** one-shot = *either* bind `Started` with no trigger, *or* add a `Pressed` trigger
  and bind `Triggered`. **Two independent routes, not a pair.** Never bind `Triggered` on a trigger-less
  action — that is the "one click = full combo" bug.

---

## 8. RISKS

| Risk | Likelihood | Mitigation |
|---|---|---|
| **The thesis is wrong — adaptive ends up easier** | Was near-certain in v1 | §1.2's two rules + **B0 settles it before any engine work** |
| **Adaptation reads as unfair, not smart** | **High — the top design risk** | §2.4's three legibility rules as acceptance criteria, validated by a **blind** player at B4 |
| **Model never converges in one encounter** | **Was High — now Low–Medium** | §2.3 — the commitment-event cadence gives ~270–450 symbols per encounter, not 15–40. Order‑0/1 are usable in **5–25 s**; order‑2 by encounter 2; backoff degrades gracefully instead of firing on noise |
| Order‑2 contexts stay thin for erratic players | Medium | By design the boss then plays the script — an unpredictable player *is* the counter-play, not a bug |
| Death spiral against a struggling player | Medium | §2.5 — adapt to habits only, cap strength |
| PIE below 60 fps silently corrupts frame data | High, and silent | Fixed frame rate for tests + A2's self-invalidating runs |
| B4 has too few testers to be falsifiable | **Unknown — §9 Q3** | Ask now, not at the end |
| GASP migration scope | Low–Medium | 30-min spike for one number. Not a version risk |
| Fab content pack refuses 5.8 | Medium | Add on a listed version, then **Migrate** — UE loads *older* assets, never newer. **Code plugins** must recompile from source and may fail — never make one a dependency without compiling first |

---

## 9. DECISIONS I NEED FROM YOU

1. **Skeleton — Manny or GASP's UEFN mannequin?** Decided at A0, never revisited.
2. **Paragon boss — yes?** Removes a whole retarget pass.
3. **Blind playtest — can you get 3 people?** B4 *is* the deliverable. Without testers, §1.1's success
   test is unfalsifiable, and it is better to know that now.
4. **MetaHumans — in for presentation, or out?** Affordable on the A5000; contributes nothing to the
   three verbs.
5. *(assumption)* Greybox through B4, dressing only after. Overrule if wrong.

---

## 10. ON APPROVAL

**B0 first — before A0.** The headless simulator needs no engine, no assets and no editor, and it is
the only thing that can tell us the core design is sound before a week goes into building it.
Then A0 and straight down the spine.

---

## APPENDIX A — HARDWARE (ON STANDBY)

**Primary target: the college workstation — 128 GB RAM, RTX A5000 24 GB.** At that spec none of the
constraints below bind: the editor, PIE, GASP and Blender all fit comfortably, MetaHumans are
affordable, and shader compilation is a non-event.

**The laptop is secondary and on standby.** Its constraints are recorded here rather than deleted,
because they bind immediately if you ever build or demo on it:

| | Laptop |
|---|---|
| RAM | **16 GB** — vs Epic's recommended 32 GB for UE 5.8 |
| GPU | RTX 4050 Laptop, **6 GB** (~4 GB usable) — vs recommended 8 GB. *WMI misreports this as 4 GB via a 32-bit overflow; use nvidia-smi* |
| Disk C: | **90.7 GB free** — carries UE (30.5 GB) + local DDC (7.17 GB, growing) + pagefile |
| Disk D: / E: | 146.8 / 140.7 GB free |
| Free RAM measured | **529 MB** on 2026-09-20 with no UE running |

**If working on the laptop, these are gates, not advice:**
- Close browser, Steam and any game first; **if Available Physical Memory is under 8 GB, do not launch
  the editor.**
- Move the DDC (`UE-LocalDataCachePath`, and the Zen data dir) and the pagefile **off C:**.
- Set `PercentageUnusedShaderCompilingThreads=70` in `DefaultEngine.ini` `[DevOptions.Shaders]`. Stock
  `BaseEngine.ini` has `ShaderCompilerCoreCountThreshold=12` and `...Threads=50` (verified); 24 logical
  cores clears the threshold, so UE spawns **~12 ShaderCompileWorkers** at 100–400 MB each — **1.2–5 GB
  on top of the editor.**
- **Pin `UnrealEditor.exe` to the NVIDIA adapter.** This is a hybrid-GPU machine; launching on the Intel
  iGPU fails *silently* — the editor just runs badly.
- First project open compiles engine shaders: **10–40 min, unattended.** Budget it.

**One cross-machine caveat that survives the move.** §6's frame numbers and §2.4's legibility rules are
*felt judgments made at a frame rate*. Tune an 8-frame parry window at 120 fps and it is a different
game at 45 fps. **Tune and judge on whichever machine B4's playtest runs on**, and if that changes,
re-run B4 rather than assuming the numbers carry.

**Moving the project between machines:** git carries code, config and greybox maps. It does **not**
carry the DDC (correctly gitignored), so the second machine rebuilds shaders from scratch. For C2-sized
animation payloads git LFS is the wrong transport — GASP alone is ~10 GB created, at or over GitHub's
free 10 GB LFS quota. **Copy output assets over LAN or a USB SSD instead.** Confirm the A5000 box runs a
**binary Launcher UE 5.8.x** — a source build writes a machine-local GUID instead of a version string
and the `.uproject` association breaks.

---

## APPENDIX B — AUDIT COVERAGE

| Lens | Status |
|---|---|
| Design soundness | ✅ 16 findings, 3 blockers — folded in |
| Asset pipeline / scope | ✅ 16 findings, 2 blockers — folded in |
| UE 5.8 engine correctness | ✅ 13 findings, 5 blockers — folded in, load-bearing ones re-verified against installed 5.8.2 source |
| Hardware / performance | ✅ 11 findings, 3 blockers — folded in, now Appendix A |
| **Playstyle-model design comparison** | ✅ 3 designs judged blind — **sequence 45 · archetype 44 · bandit 38**. Winner adopted in §2.2 **with all three judge corrections applied**; the runners-up's two best ideas grafted on (§1.2 floor controller, §2.4 Read Meter) |

**Residual known-unknowns:** the exact half-life and `β` in §2.2's corrections — both are one-line
tuning knobs that B0's simulator settles before any engine work; GASP's migrated footprint; whether the
A5000 box runs a binary Launcher 5.8.x.

**Ideas kept from the losing designs** *(a 45–44 split is not a mandate; the runner-up was better on
legibility, 9 vs 8)*: the archetype design's **closed-loop aggression-floor controller** (§1.2) and the
bandit's **damage-per-second objective with an idle tax** — it scored highest of all three on
*makes_boss_harder* (8), and its insight, that an objective defined per-*exchange* makes safety free
and passivity optimal, is why §2.2 correction 3 divides payoff by commitment frames.
