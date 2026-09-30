# RL.md — the Hellwalker boss, rebuilt as a reinforcement-learning agent trained from nothing

This document specifies how to recreate the Hellwalker boss — a keeper that notices your habits and punishes them —
with a **reinforcement-learning (RL) policy trained from random initialisation**, while keeping every game mechanic
of the current build: the frame data, the moves, the duel rules, the fairness rules, the Read Meter and the Unreal
presentation.

> **Status (HellwalkerRL, 2026-09-30): built.** RL-0 to RL-3 and RL-5 are done; the shipped keeper passes every B0
> check and the reading test; RL-4 (exploiter league) is not built and RL-6 is the human playtest. The engineering
> contract is [RL/DESIGN.md](RL/DESIGN.md); results, status and the deviations the work forced (C++ inference instead
> of NNE, the killer cooldown, the per-window aggression hinge, how reading is measured) are in [README.md](README.md).
> The brain that plays in Unreal is the engine-free `FRLBrain` (driven by the duel subsystem) rather than a separate
> `UHWRLBrain`. The text below is the plan as written.

---

## 0. The idea in one paragraph

Train the boss **offline**, on millions of fights against thousands of *different* simulated players, with a policy
that has **memory**. Because no single strategy beats all of those players, the only way to score well is to work out,
during each fight, which kind of player it is facing — and then exploit that. The weights are frozen when training
ends; at play time the boss "learns you" inside its memory (its recurrent hidden state), exactly the way the current
boss learns you inside its tally tables. This is **meta-RL** ("learning to learn", RL²). A plain RL policy without
memory, or one trained against a single opponent, would only learn one fixed best strategy against the average
player — it would be hard, but it would not *read you*.

---

## 1. How the current boss learns (no RL) — the baseline to match

The shipped boss (`HWCore/HWPlaystyleModel`, `HWPayoffTable`, `HWBossBrain`) is *opponent modelling plus a best
response*, not RL:

1. **It watches.** Every exchange is written down as symbols: 8 boss symbols (fast, heavy, feint, killer, guard, evade,
   approach, retreat) and 12 player symbols (neutral, advance, retreat, light, heavy, block, parry, step
   forward/back/left/right, weapon switch). It only sees what your character visibly does (a 6-frame perception
   delay, animation reading — never button reading).
2. **It keeps tallies.** A variable-order Markov model counts "after boss move X (and your previous answer Y), you
   answered Z", with old evidence fading (half-lives of 48/96/160 symbols by order). Side notes record *when* you
   press relative to the visible impact (timing sidecar) and whether you bite on feints (bite sidecar).
3. **It predicts.** From the tallies: your most likely answer to the move it is about to throw, and how sure it is
   (`ReadBits` = 0 when you are random, up to 3.58 bits when you always do the same thing).
4. **It counters.** A payoff table — measured by running tiny duels through `FDuel`, not hand-authored — scores every
   boss move against every player answer. The boss replaces its scripted move with the best-scoring counter only if
   it beats the script by a margin that shrinks from 3.0 to 0.4 as its certainty grows. About ten identical answers in
   the same situation are enough; when you vary, the certainty falls and it returns to its script on its own.
5. **Guard rails.** An adaptive attack never has fewer startup frames than the scripted one it replaces; an
   aggression floor keeps it from turtling; a threat abort only reacts to a wind-up visible for 6 frames; the READ
   banner appears only *after* a counter lands (testimony, not telegraph).

The RL boss must reproduce the *experience* — punished habits, broken reads when you vary, fair and legible
play — without the tallies, the payoff table or the script doing the deciding.

---

## 2. What stays, what changes

| Part | Current | RL version |
|---|---|---|
| Rules of combat | `HWCore` (`FDuel`, `FFighter`, move table, 60 fps) | **unchanged** — the RL env steps the same C++ |
| Unreal presentation | characters, frame-locked animation, HUD, open world | **unchanged** |
| Who decides the boss's next move | `FBossBrain::Decide` (script + model + payoff) | a neural policy, called at the same decision points |
| How it "learns you" | tallies in `FPlaystyleModel` | the policy's recurrent hidden state (in-context adaptation) |
| READ banner / F3 overlay | model prediction + `ReadBits` | an auxiliary head that predicts your next answer (§5.3) |
| Fairness rules | code in the brain | observation design + action masks + a constrained objective (§4.5) |
| Pathbreaker tier (control) | the script alone | **unchanged** — the classic script stays as the control arm |

Nothing about the moves changes: no new attacks, no changed frame data, the same telegraphs. The policy chooses
*which* existing move and *when*, exactly the choice `FBossBrain` makes today.

---

## 3. The environment

### 3.1 Build it on the engine-free core

`HWCore` already compiles without Unreal (the B0 simulator proves it). Add:

- `HWCore/HWRLEnv.h/.cpp` — `HW::FRLEnv`: owns an `FDuel`, a simulated player, the boss decision loop, and exposes
  `Reset(seed, playerSpec)`, `Observe(float* out)`, `ActionMask(uint8* out)`, `Step(int action) -> {reward, cost,
  done, info}`. Engine-free C++ like the rest of `HWCore`, so the same code later runs inside Unreal.
- `HW::FRLEnvBatch` — N environments stepped in one call (one thread per chunk). Python overhead per step would
  otherwise dominate: the simulation itself is cheap.
- `RL/hwcore_py/` — a pybind11 module compiled with the same MSVC flags as `Sim/build.bat`, exposing `FRLEnvBatch`
  to Python as NumPy arrays (Gymnasium `VectorEnv` interface).

Determinism: every environment is seeded; `Reset(seed)` must reproduce a fight bit-for-bit (the B1 determinism tests
already hold for `FDuel` and `HWSim`).

### 3.2 Episodes and sessions

- **Decision step:** whenever the boss is free to act (the same moments `FBossBrain::Decide` is called today: at the
  end of a move, at a chain/cancel window, after a stun). Between decisions the env steps frames internally. A fight
  has roughly 50–200 boss decisions.
- **Episode = one fight** (ends on a death or a 180 s timeout).
- **Session = K fights against the same simulated player** (K random in 1–4). The hidden state is carried across
  fights within a session and reset between sessions. This mirrors the game: all three keepers read the same you,
  and everything is forgotten when you quit.

### 3.3 Actions

`Discrete(23)`: the 22 boss moves of `HW::EMoveId` (attacks `BFastSlash … BGrab`; defence and movement `BGuard,
BCounterStance, BBackstep, BSideStepL/R, BApproach, BDashIn, BRetreat`) plus `Wait` (hold for 6 frames, then decide
again). Illegal actions are masked every step (logits set to −∞), never punished — masking is how hard rules stay
hard (§4.5).

### 3.4 Observations — only what a fair opponent could see

Per decision, a fixed-size vector (normalised) plus a short event history:

- **Self:** health, posture/sha-chi, current move and phase, frames until actionable, weapon reach.
- **Player, as visible:** distance, bearing, facing; the *visible* move class and phase **delayed by 6 frames**;
  guard raised; health; stun/hitstun state; weapon (twin blades / glaive).
- **History:** the last 32 events as tokens — (player symbol ∈ 12, boss symbol ∈ 8, outcome ∈ {hit, whiff, blocked,
  parried}, timing bucket of the player's press relative to the *perceived* impact).
- **Clock:** time in fight, fight index in the session.

Never observed: the player's raw inputs, buffered commands, the simulated player's hidden profile, RNG state. The
policy must infer the player from behaviour, as a human opponent would.

---

## 4. Objective

### 4.1 Reward

Per decision step: `r = damage_dealt − damage_taken` (health units, both sides on the same scale), plus a terminal
`+1` for winning, `−1` for losing, `0` for a timeout. Deliberately nothing else: "reading" is not rewarded directly;
it emerges because exploiting a habit is the cheapest way to deal damage against a habitual player and useless
against a random one.

### 4.2 Why this makes it read players

Against a population where each player has *different* habits, a policy that ignores who it is facing scores like
the scripted boss. A policy that notices "this one always dodges left after a fast slash" and answers with a left
sweep scores more — but only against that player. With memory in the network, gradient descent finds exactly that
behaviour: gather evidence, then commit when the evidence is strong. The classic brain's "margin that shrinks with
certainty" is re-discovered rather than written.

### 4.3 Training "from nothing"

Random initialisation. No behaviour cloning from `FBossBrain`, no payoff table, no script as a teacher. The classic
brain is used only as a **benchmark** (§7) and as the Pathbreaker control arm in the game.

### 4.4 Discounting

γ = 0.995 at decision granularity (about a whole exchange string of look-ahead), GAE λ = 0.95.

### 4.5 The fairness rules as constraints

The current game's promises must hold for the RL boss, or it will find unfun optima (turtling, frame traps,
reacting to inputs it should not see):

| Rule | How it is enforced |
|---|---|
| Animation reading only | observations (§3.4): 6-frame delay, no inputs |
| Telegraphs and frame data untouched | the action space is the existing moves; the killer move's red mark always shows |
| No faster-than-scripted adaptive attacks | action mask: an attack chosen within N frames of a player recovery must not have fewer startup frames than the fastest attack a *scripted* boss could throw there (matches today's legibility rule) |
| Aggression floor | a constraint, not a reward: cost `c = max(0, target_swings_per_min − actual)` per window; Lagrangian PPO raises its multiplier until the constraint holds |
| No lock-down loops | unchanged `HWCore` rules (combo decay, stun caps); plus a mask on re-grabbing within 3 s |
| READ banner is testimony | shown only after a counter lands, from the auxiliary head (§5.3) |

---

## 5. The model

### 5.1 Architecture

- Encoder: MLP (256, 256) over the per-step vector; a small embedding for each history token, mean-pooled.
- Memory: **GRU, 256 units**, carried across steps within a session. (Alternative once the pipeline works: a
  4-layer, 128-wide Transformer over the last 512 events — better long memory, slower.)
- Heads: policy logits (23, masked), value, and the auxiliary prediction head.

### 5.2 Algorithm

**Recurrent PPO with action masking**, a Lagrangian multiplier for the aggression cost, and an entropy bonus that
decays over training. Write it CleanRL-style (one file, PyTorch) rather than combining library wrappers: the
masking + recurrence + constraint combination is not covered by one off-the-shelf class.

Starting hyperparameters: 4096 parallel environments, rollouts of 128 decisions, truncated backprop through 32
steps, 4 epochs, 8 minibatches, clip 0.2, learning rate 3e-4 → 3e-5 linear, entropy 0.01 → 0.001, value coefficient
0.5, max gradient norm 0.5, reward normalisation on.

### 5.3 The auxiliary "read" head

A softmax over the 12 player symbols predicting the player's *next* answer, trained with cross-entropy against what
the player actually did (weight 0.1 in the loss). It does not change what counts as success; it gives:

- the **READ banner** — after a counter lands, show the head's top symbol and its probability, exactly like today;
- the **F3 overlay** — the live prediction and its entropy (the equivalent of `ReadBits`);
- faster, more stable learning (a well-known effect of auxiliary predictive losses).

### 5.4 Budget

The simulation is cheap; network inference dominates. On the development workstation (RTX A5000, 18 cores) expect
on the order of 10⁵ decisions per second with batched GPU inference, so 10⁹ decisions — a typical meta-RL budget —
is a few hours to a day. Start at 10⁸ to validate the pipeline.

---

## 6. The opponents — the part that decides whether it works

The policy can only learn to read the kinds of players it has met. The population is the curriculum.

1. **The existing bots** (`HWSim`: Masher, Turtle, Habitual, Varied, DodgerLeft, RhythmParrier), skill 0–1.
2. **Procedural habit players** — the main source of variety. Each is generated from:
   - a response table: for each boss symbol (and optionally the player's previous answer), a distribution over the
     12 answers, drawn from a Dirichlet with a random concentration — from "always the same" to "fully random";
   - a parry timing profile (mean and spread of the press lead before the perceived impact);
   - feint bite probability, reaction delay (10–25 frames), execution noise, aggression;
   - **habit switches**: with some probability the table is replaced mid-session, so the policy must also learn to
     notice that a read has gone stale (the classic brain's forgetting).
3. **Exploiters (league play)** — every few hundred million steps, freeze the boss and train RL *player* agents
   against it to find its weaknesses; add the best to the population. This stops the boss from overfitting to bot
   quirks and closes loopholes (B0 found an infinite loop in the rules once; exploiters find the policy equivalents).
4. **Human-like players (later)** — the A2 telemetry CSVs and B4 playtest logs can fit behaviour-cloned players.
   This is data about the *opponents*, not the boss, so the boss is still trained from nothing.

Curriculum: begin with strong habits (low entropy) and low skill; widen the Dirichlet, raise skill and add switches as
the policy's win rate against each band passes 60%.

---

## 7. Evaluation — the same bar as today, plus RL-specific checks

Run with `RL/eval.py` against held-out players never seen in training:

| Check | Pass condition |
|---|---|
| B0 thesis | against habitual players, the RL boss's damage advantage beats the scripted boss's by the same margin the classic brain achieves |
| B0 aggression floor | swings per minute ≥ the script's in the same fights |
| B0 masher lethal | a masher loses |
| B0 net exchange | the RL boss never trades worse than the script |
| Adaptation curve | counter success per exchange rises within ~10 repeats of a habit and falls after a habit switch (the B2 test, as a curve) |
| Not a random-player bully | against high-entropy players it performs like the script, not better — it must be *reading*, not just stronger |
| Exploitability | the best exploiter's win rate stays under a set ceiling |
| Classic vs RL | head-to-head on identical seeded player sets; report both |
| Humans (B4) | blind playtest: Pathbreaker vs classic Hellwalker vs RL Hellwalker; "it learned me" ratings and win rates |

---

## 8. Putting it in the game

- **Export:** `RL/export_onnx.py` writes the policy + auxiliary head as ONNX with the GRU state as an explicit
  input/output.
- **Runtime:** Unreal's NNE (ONNX Runtime, CPU) loads it. A new `UHWRLBrain` implements the same decision call the
  duel subsystem makes today; `-HWBrain=rl|classic` chooses (classic by default until the RL boss passes §7).
- **Memory:** one GRU state per game session, carried from keeper to keeper and dropped on quit — the same promise
  the game makes now.
- **Determinism:** single-threaded inference, greedy (argmax) actions at play time, so seeded replays and the B1
  determinism tests still hold.
- **Cost:** a 256-unit GRU is well under 0.1 ms per decision; decisions are a few per second.
- **Fallback:** if the model file is missing, the classic brain runs (the same optional-asset pattern the packs use).

---

## 9. Proposed layout

```
RL/
  hwcore_py/          pybind11 bindings over HWCore + FRLEnvBatch (built with the Sim/build.bat flags)
  players.py          procedural habit players, Dirichlet tables, switches, exploiter loading
  env.py              Gymnasium VectorEnv wrapper, observation/mask layout, session handling
  ppo_rnn.py          recurrent PPO + masking + Lagrangian aggression constraint + auxiliary head
  train.py            config, curriculum, checkpoints, TensorBoard logging
  eval.py             §7 checks, B0 metrics, adaptation curves, classic-vs-RL
  exploiters.py       league: train player agents against frozen boss snapshots
  export_onnx.py      policy + aux head to ONNX for Unreal NNE
Source/Hellwalker/Public/HWCore/HWRLEnv.h, Private/HWCore/HWRLEnv.cpp    engine-free env
Source/Hellwalker/.../HWRLBrain.h/.cpp                                  NNE-backed boss brain
```

---

## 10. Milestones

| | Deliverable | Done when |
|---|---|---|
| RL-0 | `FRLEnv` + bindings + random agent | 10⁵+ decisions/s, seeded replays identical, masks never allow an illegal move |
| RL-1 | Feed-forward PPO vs one fixed habitual bot | learns that bot's counter (a sanity check, not a reader) |
| RL-2 | Recurrent PPO vs the procedural population | adaptation curve appears on held-out habit players |
| RL-3 | Constraints and fairness | all B0 checks pass; the aggression constraint holds |
| RL-4 | Exploiter league | exploitability under the ceiling after two rounds |
| RL-5 | ONNX + `UHWRLBrain` in Unreal | `-HWBrain=rl` plays full fights at 60 fps; determinism tests pass |
| RL-6 | Human playtest (B4) | classic vs RL compared by blind players |

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Bots are not humans (sim-to-human gap) | broad procedural population, noise, exploiters, later human-fitted players |
| Degenerate optimum (turtling, spam, frame traps) | masks, aggression constraint, unchanged frame data, exploiters |
| Strong but not *readable* (feels random or unfair) | fairness constraints; the auxiliary head and READ banner explain each punish; human testing decides |
| Overfits to bot tells (perfectly regular timing) | timing noise and reaction jitter in every player |
| Forgets to forget (clings to stale reads) | habit switches in training; the adaptation-curve check |
| Training instability | normalisation, clipping, small learning rate; checkpoint and evaluate often |
| Harder to debug than tallies | log per-decision predictions, masks and values; keep the classic brain as a reference |

---

## 12. Classic brain vs RL brain

| | Classic (shipped) | RL (this plan) |
|---|---|---|
| Learns during a fight | yes — tallies | yes — recurrent memory |
| Learns before the game ships | no (rules written by hand) | yes — millions of simulated fights |
| Needs a payoff table / script | yes | no |
| Explainable | fully (counts, margins, reasons) | partly (auxiliary head, logged values) |
| Can discover strategies no one wrote | no | yes |
| Risk of unfair or degenerate play | low (by construction) | real — handled by masks, constraints, evaluation |
| Cost to build | done | the milestones above |
