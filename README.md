# HellwalkerRL — a boss that learns you, trained with reinforcement learning

An Unreal Engine 5.8 **C++** action game in the spirit of *Phantom Blade Zero*'s Hellwalker mode. The keepers of
the valley's shrines watch how you fight and punish your habits — and in this project the adaptive keeper is a
**neural network trained from scratch with reinforcement learning** ([RL.md](RL.md)), not a hand-written
counting brain. It "learns you" inside its memory during a session, the way a human opponent would: it starts
with a general style, notices that *you* always dodge left after a heavy or always parry a fast slash, switches to
the counter, and switches back when you change.

This project was built as a new project using the Hellwalker game (the tally-and-cheat-sheet brain) as
reference only. Everything the player touches is unchanged from it — the open world, the three shrines, the
duel, the moves and frame data, the controls, the Fab-pack look — except **who decides the keeper's moves**:

| | |
|---|---|
| **Pathbreaker** (easy mode) | the scripted keeper: a fixed pattern you can learn (the control arm) |
| **Hellwalker** | the **RL keeper**: a recurrent policy network (`Content/HellwalkerRL/RL/hellwalker_rl.hwrl`), run by a small C++ forward pass inside the game. One network plays all **three keepers** — the Warden, the Sage, the Returned — each with its own style, at every **difficulty** (Easy · Normal · Hard · Hellwalker · Adaptive). Its memory of you is carried from shrine to shrine and dropped when you quit |
| **66 Days** | Hellwalker with 66 lives |

The old tally brain is not in the game; it lives only in the evaluation tools (`Sim/Classic`) as the benchmark
the RL keeper is measured against. Design: [RL.md](RL.md) (the plan), [RL/DESIGN.md](RL/DESIGN.md) (the
engineering contract), [PLAN.md](PLAN.md) (the combat spec). Theme research: [Phantom Blade Zero Hellwalker Mode.md](Phantom%20Blade%20Zero%20Hellwalker%20Mode.md).

## Play

| | |
|---|---|
| **The game (.exe)** | `Tools\Package.bat` builds `Build\Packaged\Windows\HellwalkerRL.exe` (Shipping; `Package.bat Development` keeps logs and the console). Copy the whole `Build\Packaged\Windows` folder anywhere and run the exe |
| **Editor** | open `HellwalkerRL.uproject` (starts on `/Game/HellwalkerRL/Maps/L_Hellwalker`) and press **Play** — the valley is built in C++ when play starts |
| `Tools\Play.bat` | the open world in a window (title: new game / continue). `-HWWorldMode=hellwalker` skips the title; `-HWPolicy=<file.hwrl>` plays another trained keeper |
| `Tools\Arena.bat` | the duel on its own (`-HWTier=`, `-HWBlind`, `-HWBoss=Sevarog\|Wukong\|Golem`, `-HWKeeper=0\|1\|2`) |
| `Tools\Demo.bat habitual 0.8 hellwalker` | watch a simulated player fight the RL keeper (F3 shows its reasoning) |
| `Tools\Build.bat` · `Tools\Test.bat` · `Tools\MakeMaps.bat` | build the editor target · every automation test, headless · regenerate the maps and the generated materials |
| `Tools\RLBuild.bat` · `Tools\RLTrain.bat` · `Tools\RLEval.bat` · `Tools\RLShip.bat` | the RL tools: build the training DLL and the simulator · train · evaluate (RL.md §7) · put a trained keeper into the game |
| `Tools\Thesis.bat` | B0 — the headless check the reference project had to pass, now run on the RL keeper (`--identity 0\|1\|2`, `--skill`) |
| `Tools\Parity.bat` | the Unreal-vs-simulator gap: the same autoplay players and keeper in the real game and in the training simulator |
| `Tools\CI.bat` | what GitHub Actions runs (`.github/workflows/ci.yml`): core tests, env benchmark, torch/C++/ONNX parity, trainer self-tests |

### Controls (F1 in game shows this panel)

Generated from the live bindings: `hw.Controls` writes every mapping to `Saved/HellwalkerRL/Controls.txt`. Every
fighting key and the menu keys can be **rebound** (Settings → Controls, keyboard and gamepad separately).

**Exploring** (the Game Animation Sample's Mover character, worn as Soul)

| Keyboard / mouse | Gamepad | |
|---|---|---|
| W A S D | left stick | move (camera-relative) |
| Mouse | right stick | look |
| Shift (hold) | LB (hold) | sprint |
| Left Ctrl | RB | walk / run toggle |
| Space | A | jump — facing a wall: **hurdle, vault, mantle or climb** (walls up to ~2.2 m; every ruin has one of each) |
| C | B | crouch — **while running: slide** |
| R | X | **ragdoll** — Space to get back up |
| Middle mouse | right stick press | strafe |
| RMB (hold) | LT (hold) | aim |
| E | Y | ring a bell (rest, checkpoint) · challenge a keeper at a shrine gate |

**Fighting** (a keeper's duel, or the arena)

| Keyboard / mouse | Gamepad | |
|---|---|---|
| W A S D | left stick | move (locked on: strafe around the keeper) |
| LMB | X | light attack (chain) |
| E | Y | heavy attack |
| RMB (hold) | LB (hold) | block |
| Q | RB | parry (just before the hit) |
| Space + direction | A + stick | ghoststep (dodge) |
| F | B | switch weapon: twin blades / greatblade |
| Tab / middle mouse | right stick press | lock-on |
| R · 1 / 2 | — | arena: fight again (it remembers) · choose tier |

**Menus** — Esc or P / Start: **pause** (resume · settings · "How the keeper learns you" · the keeper's notebook ·
restart / quit to title · quit). N / D-pad down: **the keeper's notebook**. Menus work with mouse, keyboard
(arrows, Enter, Esc) and gamepad (D-pad / stick, A, B). Title: 1 / 2 / 3 new walk (Pathbreaker / Hellwalker /
66 Days), Enter continue. F1 controls panel · F3 debug overlay (the keeper's choice, its read of you, its memory, hit
volumes).

**Settings** (saved per user): *Gameplay* — difficulty, mouse sensitivity, invert Y · *Controls* — rebinding (keyboard
and gamepad), reset to defaults · *Graphics* — overall quality, resolution, window mode, v-sync, frame-rate limit
(applied on Apply) · *Audio* — master / music / effects · *Accessibility* — HUD scale, how long the READ banner holds,
camera shake (also turns off the hit kick), show the tutorial again.

**Tiers**:
* **Pathbreaker** — the scripted control arm: a fixed pattern you can learn.
* **Hellwalker** — the RL keeper: a neural network with a memory of you, deciding every move at the moments the
  script could (see "The RL keeper" below). After one of its swings lands on the answer its read head predicted, the
  **READ** banner shows what it expected you to do — testimony, not telegraph.

**Console** (`~`): `hw.Reset [seed]` · `hw.Tier pathbreaker|hellwalker` · `hw.InjectParry <frames>` (A3) ·
`hw.Autoplay <kind> [skill]` · `hw.Debug` · `hw.Blind` · `hw.ResetModel` (the keepers forget you) · `hw.Menu <page>` ·
`hw.Shot` · `hw.Photo player|boss|off [yaw dist height]` · `hw.Blade <0|1|2> <pitch yaw roll> [x y z]`.

## The open world

| | |
|---|---|
| **Valley** | `HWWorldGen`: a 2.4 km heightfield (hills, ridges, a mountain wall) with flattened plazas and carved paths — a pure function of the seed. `AHWOpenWorld` meshes it (10x10 procedural chunks, 460k triangles, collision), lights it (sun, atmosphere, volumetric clouds, fog) and dresses it (`HWDressing`: firs, broken firs, cliff outcrops, boulders, stones, grass and fern clumps from the environment packs; each instance fitted from the mesh's own bounds; ~55k instances). Builds in ~1.5 s. |
| **Ground** | a code-built four-layer material (`HWTerrainMaterial`): the Lighthouse pack's rock, scrub, path-dirt and ash textures, world-projected and blended by weights the generator writes into the vertex colour (rock on the steeps, dirt on paths and plazas, ash on the heights). Without the pack, the vertex colour is the albedo. |
| **Sites** | 4 bells (checkpoints), 3 shrines, 6 ruins with the Game Animation Sample's traversable blocks (vault / mantle). With Paragon Monolith: the shrines are dark "Evil" fortresses (gate arch, wall slabs, barbican spires, spiked sconces, broken statues at the approach; the final one with two tall keeps); the ruins are jungle stone (decorated floor, mossy columns, a ring arch, rubble, stone over the traversal blocks); the Western Watch keeps the Lighthouse pack's lighthouse. The greybox stays underneath as the hidden colliders, so the duel floor and walls behave the same with or without the packs. |
| **Keepers** | the Ninefold Warden (Sevarog, the Warden script) · the Monkey Sage (Wukong, the Sage script) · the Warden, Returned — reborn in stone (the Stone Golem; the final shrine — sealed until the other two fall; always reads you). |
| **The Crossroads** | a hamlet around the central bell (`HWSettlement.cpp`, the Desert City kit): mud-brick houses facing the bell with the roads left open, a market of fabric stalls, fire pits, chimney smoke, great rocks around it (its plaza is flattened to 36 m). |
| **Fire and smoke** | Niagara Examples: fire on logs in the bells' braziers (lit when you ring them), the pits and the shrines' sconces (out when the seal breaks); smoke rising behind every shrine whose keeper lives; a teleport-in as Soul wakes and as both fighters enter a duel; the loser of a duel shatters into embers. |
| **Flow** | E at a shrine's gate: the explorer steps out, the duel runs on the plaza, and when it ends you step back into the world. Every keeper reads the same you: the RL keeper's memory is shared across the whole walk. Die: you wake again (at your bell, or — during the build phase — at the next keeper's gate). |
| **Modes** | Pathbreaker (scripted keepers, the final one still reads you) · Hellwalker · **66 Days** (66 lives; when the last day passes, the save is erased). Progress is saved at bells and shrines (`UHWSaveGame`); the keepers' memory of you is not — they forget you when you quit. |
| **Build phase** | new games and deaths put you just outside the next keeper's gate. `-HWSpawnAtBell` restores the bells. |

**The script.** Pathbreaker's two scripts (the Warden, the Sage) are the reference project's, played verbatim by `FScriptBrain`; the RL keeper has no script — the three Hellwalker keepers differ only in look, health and place.

Automation (`HWAutomation`, both modes): `-HWShotAt/-HWShotEvery/-HWShots`, `-HWQuitAt`,
`-HWExec="9:hw.Kill boss|17:hw.Duel 1"` (timed console commands); open world only: `-HWDuel=<i>`,
`-HWTour` (walks the paths, jumps at obstacles), `-HWGoto=<site>`, `-HWNoSave`, `-HWMeshSurvey`,
`-HWExplorer=ragdoll|mover|cmc` (which Game Animation Sample character explores; default ragdoll).
Console: `hw.Duel <i>`, `hw.Kill boss|player`, `hw.Goto <site id>`, `hw.GotoBlock <n>` (in front of a traversal
block: six per ruin — vault, mantle, platform, step, vault, climb), `hw.Hold <IA_Name> <s> [x y]` (press any of the
explorer's input actions, e.g. `hw.Hold IA_Crouch 0.1`), `hw.Where`, `hw.Interact` (as E), `hw.Controls`, `hw.Help`.

Scripted checks of the sample's moves (all verified): walk 374 cm/s, sprint 583, crouch-slide 780 (capsule drops),
crouch-walk 164; ragdoll and Space get-up; mantle onto 1.5 m, climb onto 2.0 and 2.2 m walls (2.5 m is past its
reach), vault 0.9 m — e.g. `"-HWExec=10:hw.GotoBlock 5|11:hw.Hold IA_Move 2.2 0 1|11.4:hw.Hold IA_Jump 0.1|13:hw.Where"`.

The whole walk, scripted (three duels won, the final gate opens, the ending):
`Play.bat -HWWorldMode=hellwalker -HWNoSave "-HWExec=8:hw.Duel 0|13:hw.Kill boss|28:hw.Duel 1|33:hw.Kill boss|48:hw.Duel 2|53:hw.Kill boss"`
— the log shows each `World phase:` step, ending in `Ending (shrines cleared 3/3)`.

## C2 — the characters (Fab packs, not in git)

| Pack (Fab) | In `Content/` | Used for |
|---|---|---|
| Fighter Animation Pack (9CG) | `Fighter_Animations` | **Soul**: UE5 Manny + the pack's UE5 set (`Sequence2`) — strings, heavies, 4-way dodges (ghoststep), block / parry / hit / guard-break, run and guarded-walk loops. No retarget. |
| Paragon: Sevarog | `ParagonSevarog` | **the Warden** (default) on its native skeleton — Swing 1-3 Fast/Medium/Slow, Ultimate (killer), Soul Siphon (grab), stun, hit reacts |
| Paragon: Wukong | `ParagonSunWukong` | the Warden, alternative look (`-HWBoss=Wukong`) — staff melee A-E, Q Slam (killer), flip, stun |
| Slash Trail FX (SoftTofu) | `SlashTrail_SoftTofu` | weapon trails over the active frames; a burst on every hit / block / parry |
| Free Animation Library | `FreeAnimationLibrary` | not needed yet (no clip beats the Fighter pack for any move) |
| Game Animation Sample | copied into `Content/` (`Blueprints`, `Characters`, `Input`, `Levels`, ...) | **the open world's explorer** — its Mover-based motion-matched character (`SandboxCharacter_Mover_Ragdoll`: run, sprint, crouch, slide, hurdle/vault/mantle/climb, ragdoll), worn as Soul: the Fighter pack's Manny retargeted live by its `ABP_GenericRetarget`; the ruins use its `LevelBlock_Traversable` |
| Stone Golem | `Stone_Golem` | **the Warden, Returned**: its body over a UE5 Manny driver that fights with the Fighter pack's strikes (the golem has no attacks of its own), retargeted live by the sample's `RTG_UEFN_to_UE4_Mannequin` (the golem is a UE4-mannequin skeleton). `Arena.bat -HWBoss=Golem` fights it in the arena |
| Niagara Examples | `NiagaraExamples` | fire (`NS_Fire` burns a mesh: it is given the pack's fireplace logs / torch fuel), chimney smoke, smoke plumes, teleport-ins, the defeat burst |
| Science Fiction Desert City | `Scifi_desert_city` | the Crossroads hamlet (houses, fabric stalls, crates, rocks) |
| Thornblade (`Sword`) | `Sword` | Soul's blades: drawn on the duel's hand-weapon frames (tip, trails and grip tuning unchanged) and sheathed on the explorer's back |
| Paragon: Agora and Monolith (+ KiteDemo) | `ParagonProps`, `KiteDemo` | the valley's rocks, cliffs, ferns, grass; ruin and shrine architecture |
| Abandoned Lighthouse | `TheLightHouseOfNoReturn` | firs, broken firs, grass, plants, cliffs, stones |

How it works (`HWAnimTypes.h`, `HWAnimCasts.cpp`, `HWAnimSet.cpp`, `HWAnimDriver.cpp`, `HWAnimInstance.cpp`):

* **The frame lock.** A clip's time is a pure function of the move's frame index (`HWWarpMoveTime`): the
  clip's *contact* pose lands exactly on the rules' impact frame, a delayed heavy reads as the ordinary
  heavy then holds at the top of its wind-up, a feint's fake swing visibly half-arrives on the fake frame and
  pulls back. Hitstop freezes animation for free. `Project.HellwalkerRL.Anim.FrameLock` checks all of it.
* **Marks are measured, not authored.** Loading a cast samples every attack clip on the game's 60 Hz grid:
  contact = the frame the weapon tip is fastest, apex = the pause before it, and the *side* the weapon
  crosses to. `-HWAnimSurvey` writes that for every clip in each pack (`Saved/HellwalkerRL/AnimSurvey_*.txt`);
  the casts were chosen from it, and `Project.HellwalkerRL.Anim.Casts` asserts that every sweep's clip crosses
  the way its hitbox tracks (legibility, PLAN §2.4a).
* **No Animation Blueprint.** `UHWAnimInstance` is a native anim instance whose proxy samples and blends the
  clips the driver chose (n-way locomotion base, then action layers, additive flinches, cross-fades).
* **Presentation only.** The mesh never collides and never moves the capsule (root motion is stripped;
  steps are pinned over the capsule). Same seeds, same fights: skeletal and `-HWGreybox` runs produce
  identical telemetry.
* **Colour language kept.** The greybox telegraphs (killer red, hyper-armor orange, guard blue, exposed
  yellow, parry flash) become an additive rim-glow overlay on the real mesh; the ghoststep trail becomes
  pose-copied afterimages. The glow material is built from code (`HWGlowMaterial.cpp`).

**The Game Animation Sample (PLAN C2a) — where it is used.** Not in the duel: the duel's locomotion is
lock-on strafing inside a frame-stepped fight, which the Fighter pack's loops cover frame-locked and
deterministic. In the open world, where you run, sprint, vault and mantle, the sample's motion-matched
character is the explorer, and it wears Soul's mesh through the sample's own runtime retargeting.

**Not usable on 5.8 yet:** RamsterZ Free Anims and Niagara Examples (Fab lists them up to 5.7).
Every pack listed above is in use.

## The RL keeper — how it learns you

The keeper was trained offline on a billion simulated decisions against a population of thousands of different
simulated players — each with its own habits (for every kind of keeper swing, a probability of parrying, blocking,
dodging left / right / back, swinging back, doing nothing), timing, reaction speed and nerve; some who **change habits
mid-session**, some who **learn** (they notice which answer gets them hit and shift away from it), and a league of
**exploiters** — player networks trained only to beat the current keeper (RL-4). No single strategy beats all of them,
so the only way to score well is to work out, during each session, *who* it is facing. The network has a memory (a
GRU), and that memory is where the reading happens. The weights were frozen when training ended; in the game the
keeper learns you inside its memory, from nothing but what it can see (RL.md §0, "meta-RL").

* **What it sees** (`HWRLObserver`, 107 numbers + 32 remembered exchanges): its own state; your distance,
  movement, guard, weapon, health and what your character was *visibly* doing **6 frames (0.1 s) ago** — never your
  button presses; the exchanges it has perceived this session (its move, your answer, the outcome, how early you
  pressed); and who it is and how hard it plays (the keeper and the difficulty are inputs of the same network).
* **What it may do**: the 22 existing keeper moves + wait, at the moments the scripted keeper could act (end of a
  move, a cancel window, after a stun), at most every 0.1 s. Hard masks keep it fair: no illegal move, no swinging
  at air, strings of at most 3 attacks, no re-grab within 3 s, the killer thrust at most once per 10 s, no chained
  attack faster than any scripted follow-up. Frame data and telegraphs are untouched.
* **What it wanted**: damage dealt minus damage taken (as fractions of each health bar), +1 for a win, −1 for a loss —
  with an aggression floor as a constraint (it may not win by turtling: a Lagrangian multiplier raises the price of
  every 30-second window spent under 66 swings/min), and a small per-keeper style bonus (below). "Reading" is never
  rewarded directly; it emerges because exploiting a habit is the cheapest way to win against a habitual player.
* **The READ banner** is testimony, not telegraph: after a keeper swing lands, the banner shows what its read head
  predicted you would do — only if it was confident (≥ 55 %) *and right* — with a sting and the keeper's laugh.
* **The keeper's notebook** (pause menu, or N): what the keepers have written down about you this session — what the
  read head expects you to do against each of its eight attacks and how sure it is, what you actually did against
  each kind of swing, how often it called you right (and when it was sure), the READs that landed, a bar per fight,
  and the habit it leans on. The end-of-duel screens carry a one-line summary.
* **F3 overlay**: the keeper's chosen move and the network's top three with probabilities, its read of your next
  answer and how certain it is, how it rates the position, how much of you it remembers, and which keeper at which
  skill is playing.

### Three keepers, one network

The identity is an input, so the three shrines' keepers are the same weights playing three characters; a small style
reward during training (DESIGN.md §7) gave each its own way of winning, and the evaluation checks each one is the most
like itself:

| keeper (shrine, look) | style | what the evaluation measures (dmg/min · guard breaks/min · feint bites/min · READs/min) |
|---|---|---|
| **the Warden** (Sevarog) | pressure: heavies into your guard | 1541 · **7.1** · 8.0 · 24.8 |
| **the Sage** (Wukong) | baits: feints and evasions | 1434 · 6.5 · **14.6** · 24.4 |
| **the Returned** (the stone golem, the Hell Gate) | the reader: fast, exact counters; 25 % more health | 1444 · 3.0 · 3.7 · **53.2** |

### Difficulty

The same network at every difficulty: the skill input slows its eyes (perception 0.1 → 0.27 s), its decisions
(every 0.1 → 0.2 s), shortens its strings (3 → 1) and doubles the grab / killer cooldowns; Easy and Normal also sample
from the policy instead of always taking its best move; Easy adds a breather — an attack that opens a string must come at
least 1.4 s after the keeper's previous attack began (`RL::EasySwingGap`, a mask; the weights are the same, and the ladder
below measures what it does). **Adaptive** moves the skill between fights to keep them
close (−0.10 after a crushing keeper win, −0.03 after a narrow one, +0.04 / +0.10 after yours; sampling below 0.3,
the breather below 0.15), so a losing streak walks it down to Easy and wins bring it back.

| difficulty | skill · sampling | keeper dmg/min | taken/min | swings/min | fight (s) |
|---|---|---|---|---|---|
| Easy | 0 · T 1.0 · breather | **537** | 275 | 38 | 42 |
| *Pathbreaker (the script)* | — | *719* | *391* | *64* | *31* |
| Normal | 0.4 · T 0.6 | 1194 | 166 | 65 | 19 |
| Hard | 0.75 · greedy | 1316 | 140 | 72 | 17 |
| Hellwalker | 1 · greedy | 1333 | 153 | 75 | 17 |

(Mortal fights against held-out habit players and the reference bots, `RL/eval.py` ladder; simulated players almost
never win, so damage is the measure. The human playtest will tell where the steps really sit.)

### Sound and feel

`UHWAudioSubsystem` plays the duel from the packs already in Content (no hand-made assets; a missing pack is silent):
swings by weight (fast, heavy, feint, the killer's warning), the keeper's effort, pain and death in its own voice
(Sevarog for the Warden, Wukong for the Sage, Sevarog lowered and slowed for the Returned), hits, a bright parry over
a ringing blade, a dull block, ghoststeps, guard breaks, and the READ sting under the keeper's laugh. The duel music
fades in for the fight and **tightens as the keeper pulls ahead** (its health lead over you, smoothed), and a low
breathing layer comes in when you are close to death. (The first version followed the RL critic's value; review showed
that value is the normalised *remaining* return, which falls as the keeper closes in on a kill — no win estimate.) Gamepad rumble on hits taken, parries and READs; the camera
kick on hits honours the accessibility toggle. Scripted checks read the per-duel cue counts from the log.

## Training and evaluation

```
Tools\RLBuild.bat                                      RL\native\out\hwrl.dll (env + C++ forward pass) + Sim\out\ThesisSim.exe
Tools\RLTrain.bat --stage rl1 --run sanity             RL-1: a memoryless policy vs one habitual bot (minutes)
Tools\RLTrain.bat --stage rl2 --run keepers            RL-2..4: the recurrent keepers vs the population + the exploiter league (1e9 decisions, ~2 h)
Tools\RLEval.bat --hwrl <policy.hwrl> --ckpt <ckpt.pt> --name <report>   RL.md §7 -> RL\reports\<report>.md
RL\.venv\Scripts\python.exe RL\exploit.py --boss <policy.hwrl> --out <exploiter.hwrl>   train a fresh exploiter against a frozen keeper
Tools\Thesis.bat [--identity 0|1|2] [--sessions 64]    B0 on the shipped keeper
Tools\Parity.bat                                       the Unreal-vs-simulator gap
Tools\RLShip.bat <policy.hwrl>                         ship it: Content\HellwalkerRL\RL\hellwalker_rl.hwrl
```

The environment is the game's own C++ (`FDuel`, the observer, the simulated players) stepped by a thread pool:
~600,000–820,000 decisions/s on 36 threads, bit-identical for any thread count. The trainer (PyTorch, CUDA) is recurrent
PPO with action masks, the Lagrangian aggression constraint and an auxiliary read head; the same network runs in the
game through `FRLPolicy` (parity with PyTorch: max error 4e-6; an ONNX copy is exported and verified too). Training
logs **the reading test every 50 iterations** (`[read]` lines and TensorBoard: how far the keeper's play diverges toward
each habit's counter, and its hit rate around a habit switch), so the evidence is watched as it grows.
Every few hundred iterations the **league** freezes the keeper, trains two fresh exploiters against it (`exploit.py`:
a small player network with the player's moves, 10-frame perception, rewarded for damage), and adds them to the
population.

## Results — the shipped keeper

`Content/HellwalkerRL/RL/hellwalker_rl.hwrl` = `RL/checkpoints/rl2_keepers/keeper_1p17e9.hwrl`: run `rl2_keepers`,
10⁹ decisions of recurrent PPO from random initialisation with four league rounds (eight exploiters; ~2 h on the RTX
A5000 + 36 CPU threads), then two fine-tunes — 1.5 × 10⁸ decisions with a floor under the aggression price and a fifth
league round, then 1.6 × 10⁷ at a fixed price (λ = 2, 40 % reference bots), which closed the last B0 gap —
1.17 × 10⁹ decisions in all. Report: `RL/reports/keepers_1p17e9.md`.

**B0 — the check the reference project had to pass, with the RL keeper as the adaptive arm** (`Tools\Thesis.bat`,
64 sessions per cell, 20 player profiles × skills, each keeper):

| check | the Warden | the Sage | the Returned |
|---|---|---|---|
| harder than the script against every skilled profile | **PASS** — 1.5–4.2× its damage/min | **PASS** — 1.6–3.3× | **PASS** — 1.5–4.1× |
| aggression floor: swings/min ≥ the script's in the same fights | **PASS** — closest: RhythmParrier 0.5, 61.8 vs 57.9 | **PASS** — 59.9 vs 55.7 | **PASS** — 58.1 vs 50.3 |
| a masher dies | **PASS** — in 8.9 s (the script: 15.6 s) | **PASS** — 9.0 s (18.0 s) | **PASS** — 17.4 s (15.6 s) |
| net exchange never worse than the script (5 % tolerance) | **PASS** — worst: RhythmParrier 0.9, 1.41 vs 1.42 | **PASS** | **PASS** |

At 16 sessions per cell the Warden's net exchange against RhythmParrier 0.7–0.9 sits right at the 5 % line and can
fail on sampling noise; 64 sessions settle it (it passes for all three keepers).

**The reading test** (`RL/habits.py`, the C++ attack log: 256 held-out sessions per group, clean hits only):

| players who always… | keeper's first 5 attacks | keeper's attacks 21–60 | its clean hit rate |
|---|---|---|---|
| parry | DelayedHeavy 54 %, FastSlash 36 % | FastSlash 59 %, DelayedHeavy 36 % | 0.51 → 0.53 |
| block | DelayedHeavy 55 %, HeavyCleave 22 % | HeavyCleave 60 %, DelayedHeavy 21 % | 0.27 → 0.40 |
| dodge left | DelayedHeavy 62 %, FastSlash 23 % | HeavyCleave 31 %, FastSlash 23 % | 0.23 → 0.47 |
| dodge right | DelayedHeavy 57 %, FastSlash 19 % | HeavyCleave 32 %, DelayedHeavy 24 % | 0.20 → 0.44 |

It opens with nearly the same play against everyone and diverges toward each habit's answer as the session goes on
(move-mix divergence between the four groups 0.08 → 0.48 bits). When a player switches habit after 30 keeper swings,
its hit rate falls 0.49 → 0.31 (its read has gone stale) and recovers to 0.41. Against held-out habit players its hit
rate rises with the session (attacks 1–5 → 21–60: +0.12 against strong habits). The in-training log shows the read was
sharpest mid-run (0.73 bits at ~5 × 10⁸ decisions) and traded some of it for pressure as the league's exploiters and the
aggression floor pushed; the fine-tune was stopped at the checkpoint that passes B0 while keeping the rise (later ones
flattened it to +0.04).

**Classic vs RL on identical seeded held-out players** (C++, 90 s immortal fights; damage dealt / taken per minute,
swings per minute, hit rate):

| players | Pathbreaker (script) | Hellwalker (classic tally brain) | Hellwalker (RL) |
|---|---|---|---|
| strong habits | 1019 / 498 / 66 / 0.60 | 1216 / 328 / 68 / 0.67 | **1756 / 101 / 83 / 0.74** |
| near-random | 991 / 577 / 67 / 0.58 | 1144 / 417 / 69 / 0.65 | **1735 / 155 / 80 / 0.72** |
| habit switchers | 945 / 668 / 67 / 0.55 | 1140 / 497 / 68 / 0.64 | **1866 / 144 / 77 / 0.72** |
| learning players | 841 / 488 / 66 / 0.51 | 999 / 352 / 68 / 0.58 | **1642 / 117 / 78 / 0.62** |
| the reference bots | 618 / 262 / 62 / 0.38 | 834 / 224 / 66 / 0.43 | **1212 / 127 / 75 / 0.46** |

**Exploitability (RL-4).** Against the reference bots at skill 0.9 the shipped keeper loses 10 % of fights;
a **fresh exploiter** trained against it for 4 × 10⁷ decisions (8 minutes) wins **93 %** (exchange 1.5 in health
fractions, `RL/exploit.py`) — by guard-and-poke: guard 33 % of its decisions, light attacks 17 %, circling left 14 %,
walking in 13 %. Every league round told the same story: the round's exploiters beat the frozen keeper 87–100 % of the
time (one 5 %), and the next keeper beat those but not the next fresh ones. The league made it harder to exploit
(a fresh exploiter against the 10⁹ keeper: 98 %, exchange 2.0) but did not converge; a longer league, or exploiters
kept in the population at a higher share, is the next step.

**Unreal vs the simulator** (`Tools\Parity.bat`, `RL/reports/parity.md`). The same autoplay players fought the same
keeper in the game's arena and in the training simulator (3 bots × RL / script, 18 Unreal fights per cell). The first
measurement found the keeper side matching but the *player* attacking 3-45× as often in Unreal; the bot's own counters
showed why — equal mean distance, but 8× as many frames within its reach — because the characters accelerated and braked
(~37 cm of slide per stop) while the simulator moves and stops instantly, so the keeper's learned spacing went soft. With
the duelists' movement made instant and the approach-stop distance shared, 6 of 72 metrics differ (26 before): the RL
keeper's cells agree within a few percent (damage, swings, hit rate, fight length; move mix ≤ 0.013 bits), and what is
left is mostly the scripted keeper against the rhythm parrier (likely capsule collision blocking steps the simulator
lets pass). Details: RL/DESIGN.md §12.

**Not yet met:** RL.md's "not a random-player bully" check — against near-random players the RL keeper is ~75 % more
dangerous than the script, not equal: it is stronger against everyone, and its reading shows in *what* it throws, not
in a smaller margin against random players. The adaptation curve's "dips after a switch" / "reads, not strength" checks
on the population-average curve fail (the pure-habit reading test above is the measure that separates them). The
human playtest (RL-6) is the real judge of difficulty and fairness.

## Layout

```
Source/HellwalkerRL/Public/HWCore, Private/HWCore   engine-free C++ core, compiled into the game AND the tools
    HWTypes / HWMoves / HWFighter / HWDuel     the rules: frame data, moves, frame-stepped combat (PLAN §5, §6)
    HWBrain                                     IBossBrain, decision records, the Read Meter, the boss scripts
    HWScriptBrain                               Pathbreaker: the script verbatim
    HWRLTypes / HWRLObserver / HWRLPolicy / HWRLBrain   the RL keeper: layouts, senses + masks, the network, the brain, the notebook
    HWEncounter / HWSim                         duel + brain + stats; 2-D arena and simulated players (habit, learning players)
    HWCoreTests / HWRLTests                     the done-tests, also run by ThesisSim
Source/HellwalkerRL/...        the Unreal layer: duel subsystem, session memory, characters, HUD + menus (HWMenu,
                               HWSettings), audio (HWAudio), open world, tests
Sim/ThesisSim.cpp, SimArms.*   B0: Pathbreaker vs an adaptive arm (the RL keeper, or the classic brain)
Sim/Classic/                   the reference project's tally brain (playstyle model, payoff table) — tools only
RL/native/                     the training environment (FRLEnvBatch, the exploiter env FRLPlayerEnv) + the hwrl.dll C ABI
RL/*.py                        the trainer: hwcore (ctypes), env, players, model, ppo_rnn, train, export, eval, habits,
                               exploit (RL-4), parity (Unreal vs simulator)
RL/tests/                      torch / C++ / ONNX parity
.github/workflows/ci.yml       CI without Unreal (Tools\CI.bat locally)
Content/HellwalkerRL/          the maps and generated materials (written by Tools\MakeMaps.bat), the shipped keeper
```

## What RL changed (deviations from RL.md — each found by building or training it, each reviewable)

1. **In-game inference is a plain C++ forward pass (`FRLPolicy`), not Unreal NNE / ONNX Runtime.** The network is
   small (0.5 M parameters, well under 0.1 ms per decision); this way the *same code* runs in B0's simulator and in the
   game, it is deterministic, and the game needs no plugin. An ONNX copy is still exported (and checked against
   PyTorch). Python talks to the C++ environment through a DLL + ctypes rather than pybind11 (nothing to install).
2. **The killer thrust got a 10 s cooldown mask.** RL-1, against one fixed bot, learned to drain the guard with heavy
   sweeps and then spam the unblockable killer on an empty guard (161 of 289 decisions). The grab already had a
   3 s cooldown in RL.md; the killer is the same kind of rule.
3. **The aggression floor is a per-window hinge (as RL.md §4.5 writes it), with a 15 s warm-up and a small budget.** A
   linear swing debt let a surplus against some players pay for a deficit against others; B0 caught the keeper
   swinging 45/min at a constant parrier. Even the hinge, priced by one global multiplier with a budget, lets a small
   matchup (the rhythm parrier) stay under the floor while the average is fine: the last step fixed the price
   (λ = 2) for a short fine-tune, which closed it within ~1.6 × 10⁷ decisions.
4. **The curriculum gate opens at once.** The keeper out-healths every simulated player (1100 vs 360 hp) and wins
   ~100 % of fights, so the ≥ 60 % win-rate gate unlocks every habit band within a few iterations; the population is
   broad from the start and the damage exchange, not the win, carries the signal.
5. **Reading is measured with pure-habit groups.** The population-average "hit rate vs k-th attack" curve rises for
   strong and weak habits alike (pressure builds over a fight), so it cannot separate reading from strength. The
   reading test (`RL/habits.py`) measures what reading *is*: the keeper's play diverging toward each habit's counter
   over a session, and its hit rate dipping and recovering around a habit switch.
6. **One network, three keepers, every difficulty (observation layout 3).** RL.md planned one keeper; the identity and
   the skill became inputs, so the shrines' keepers and the difficulty ladder needed no extra training runs.
7. **Easy is the network plus a breather.** At skill 0 the network still out-hits the script; rather than train a weaker
   policy, Easy masks attack openers until 1.4 s after the keeper's previous attack began. Unlike the cooldowns it is
   not observed and never set in training, so it is an input situation the network never saw: its swing-deficit input's
   target is capped at what the breather allows (so it is not told it is "behind" all fight), and the ladder measures
   the result rather than assuming it.
8. **The exploiters are small and see the player's view.** A 96-unit MLP + GRU-64 with the player's moves, 10-frame
   perception and a damage reward (with a timeout penalty — the first exploiters learned to run away). They find
   pressure strategies the hand-made population never had; the league puts them in the keeper's population.
9. **The duelists move like the simulator.** Instant acceleration and braking, and one approach-stop distance for both
   — found by the Unreal-vs-simulator measurement: with ordinary character acceleration the keeper's trained spacing
   went soft and the player got into reach 8× as often.
10. **Packaging.** A packaged game cannot compile shaders, so `Tools\MakeMaps.bat` also saves the code-built
   materials (`/Game/HellwalkerRL/Materials`) and bakes the instanced-mesh usage into the dressing's pack materials;
   editor-only code is guarded for the Shipping build; cooking bypasses the Zen store (its data lives on the full C:
   drive of the build machine). The keepers' voice lines are cooked one by one (an asset-manager rule), not the
   350 MB voice packs.

## Status against RL.md

| | Deliverable | Status |
|---|---|---|
| RL-0 | `FRLEnv` + bindings + random agent | **done** — 600,000–820,000 decisions/s (36 threads), ~130,000 on one; seeded replays identical for any thread count; masks never allow an illegal move |
| RL-1 | feed-forward PPO vs one fixed habitual bot | **done** — learns that bot's counters; it also found the killer-spam exploit, now masked |
| RL-2 | recurrent PPO vs the procedural population | **done** — 10⁹ decisions with the reading test logged during training; play diverges toward each habit's counter; hit rate dips and recovers around a habit switch |
| RL-3 | constraints and fairness | **done** — all four B0 checks pass for all three keepers (64 sessions); the aggression floor holds per matchup |
| RL-4 | exploiter league | **done, not converged** — five rounds, ten exploiters in the population, learning players too; a fresh exploiter still finds a winning strategy (above) |
| RL-5 | inference + the RL brain in Unreal | **done** — `FRLBrain` plays the Hellwalker tier (fallback: the script when the model file is missing); three keepers, five difficulties, the notebook, menus, sound; the Unreal-vs-simulator gap measured (above); the Shipping .exe |
| RL-6 | human playtest (B4) | **yours** — `Tools\Arena.bat -HWBlind` labels the tiers Variant A / B; F3 shows the keeper's reasoning; the notebook shows what it learned |

Tests: `Tools\Thesis.bat` (35 core tests: combat, the script, the RL keeper incl. skill, learning players, the
notebook and the Easy breather, the classic benchmark), `Tools\Test.bat` (47 Unreal automation tests: core and RL
wrappers, the shipped model, menus and settings, keepers and difficulty, the music's tension, world generation,
animation casts), `RL\.venv\Scripts\python.exe RL\tests\test_parity.py` (torch vs C++ vs ONNX),
`RL\native\out\envbench.exe` (throughput, determinism, masks, rollover, attack log), `Tools\CI.bat` (all of the
engine-free ones, as GitHub Actions runs them).
