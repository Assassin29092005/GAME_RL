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
| **Normal** (mode 1) | the scripted keepers: fixed patterns you can learn (the control arm; internally *Pathbreaker*) |
| **Adaptive AI** (mode 2) | the **RL keepers**: a recurrent policy network (`Content/HellwalkerRL/RL/hellwalker_rl.hwrl`), run by a small C++ forward pass inside the game. One network plays all **three keepers** — the Warden, the Sage, the Returned — each with its own style, at every **difficulty** (Easy · Normal · Hard · Hellwalker). Its memory of you is carried from shrine to shrine and dropped when you quit, and **how hard it plays grows with how well it actually predicts you** (its *insight*): a trick wins the first fights, and by the fourth or fifth it has read it (internally *Hellwalker*) |

The old tally brain is not in the game; it lives only in the evaluation tools (`Sim/Classic`) as the benchmark
the RL keeper is measured against. Design: [RL.md](RL.md) (the plan), [RL/DESIGN.md](RL/DESIGN.md) (the
engineering contract), [PLAN.md](PLAN.md) (the combat spec). Theme research: [Phantom Blade Zero Hellwalker Mode.md](Phantom%20Blade%20Zero%20Hellwalker%20Mode.md).

## Play

| | |
|---|---|
| **The game (.exe)** | `Tools\Package.bat` builds `Build\Packaged\Windows\HellwalkerRL.exe` (Shipping; `Package.bat Development` keeps logs and the console). Copy the whole `Build\Packaged\Windows` folder anywhere and run the exe |
| **Editor** | open `HellwalkerRL.uproject` (starts on `/Game/HellwalkerRL/Maps/L_Hellwalker`) and press **Play** — the valley is built in C++ when play starts |
| `Tools\Play.bat` | the open world in a window (title: new game / continue). `-HWWorldMode=normal\|adaptive` skips the title; `-HWDifficulty=`, `-HWParryAssist=RingSlow\|Ring\|Off`; `-HWPolicy=<file.hwrl>` plays another trained keeper |
| `Tools\Arena.bat` | the duel on its own (`-HWTier=`, `-HWBlind`, `-HWBoss=Sevarog\|Wukong\|Golem`, `-HWKeeper=0\|1\|2`) |
| `Tools\Demo.bat habitual 0.8 hellwalker` | watch a simulated player fight the RL keeper (F3 shows its reasoning) |
| `Tools\Build.bat` · `Tools\Test.bat` · `Tools\MakeMaps.bat` | build the editor target · every automation test, headless · regenerate the maps and the generated materials |
| `Tools\RLBuild.bat` · `Tools\RLTrain.bat` · `Tools\RLEval.bat` · `Tools\RLShip.bat` | the RL tools: build the training DLL and the simulator · train · evaluate (RL.md §7) · put a trained keeper into the game |
| `Tools\Thesis.bat` | B0 — the headless check the reference project had to pass, now run on the RL keeper (`--identity 0\|1\|2`, `--skill`, `--keeper-damage`). `Thesis.bat --arc [--arc-lo --arc-hi --keeper-damage]`: the Adaptive AI learning arc (insight) against one-trick, switching and near-random players |
| `Tools\Parity.bat` | the Unreal-vs-simulator gap: the same autoplay players and keeper in the real game and in the training simulator |
| `Tools\CI.bat` | what GitHub Actions runs (`.github/workflows/ci.yml`): core tests, env benchmark, torch/C++/ONNX parity, trainer self-tests |
| **The website** | `web/` — download page + every player's stats and what the keeper learned about them ([web/README.md](web/README.md): Firebase, Render, itch.io). Local preview: `RL\.venv\Scripts\python.exe web\dev\serve.py` |
| `Tools\MakeRelease.bat` | the download on GitHub Releases: the packaged game zipped into parts under GitHub's 2 GiB limit + `Join-and-Extract.bat` (players double-click it; Windows' own `tar` unpacks) + checksums, in `Build\Packaged\Release`; then `gh release create v<version> Build\Packaged\Release\* --notes-file <notes>` ([web/README.md](web/README.md) §3) |
| `Tools\ItchPush.bat <user> <game>` | upload the packaged game to itch.io with butler (an alternative to GitHub Releases) |

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
| M | D-pad left | **the valley map** (pauses): the whole valley, every keeper and bell, where you are; pick the keeper to **track** (↑/↓ + Enter, or click it) · wheel / LT RT zoom · drag / right stick pan · M, Esc or B closes |

While exploring, every keeper still standing has a compass mark and, when it is off screen, an arrow at the screen's
edge with its name and distance. The **tracked** keeper — the current objective (the nearest open keeper; the final gate
once it opens) unless you picked another on the map — pulses, keeps a mark on screen at any distance, and is named under
the objective ("Tracking: … m"). Fallen keepers drop off.

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
| R · 1 / 2 | — | arena: fight again (it remembers) · choose mode (Normal / Adaptive AI) |

**Menus** — Esc or P / Start: **pause** (resume · settings · "How the keeper learns you" · the keeper's notebook ·
restart / quit to title · quit). N / D-pad down: **the keeper's notebook**. Menus work with mouse, keyboard
(arrows, Enter, Esc) and gamepad (D-pad / stick, A, B). Title: 1 / 2 new walk (Normal / Adaptive AI), Enter
continue. F1 controls panel · F3 debug overlay (the keeper's choice, its read of you, its memory, hit
volumes).

**Settings** (saved per user): *Gameplay* — difficulty, parry assist, mouse sensitivity, invert Y · *Controls* — rebinding (keyboard
and gamepad), reset to defaults · *Graphics* — overall quality, resolution, window mode, v-sync, frame-rate limit
(applied on Apply) · *Audio* — master / music / effects · *Accessibility* — HUD scale, how long the READ banner holds,
camera shake (also turns off the hit kick), show the tutorial again.

**Modes**:
* **Normal** — the scripted control arm: fixed patterns you can learn, at every shrine.
* **Adaptive AI** — the RL keeper: a neural network with a memory of you, deciding every move at the moments the
  script could (see "The RL keeper" below). After one of its swings lands on the answer its read head predicted, the
  **READ** banner shows what it expected you to do — testimony, not telegraph.

**The parry assist** (both modes, Settings → *Parry assist*: ring + slow motion · ring only · off): a **red ring** round
the keeper's weapon hand is lit exactly while a parry pressed now would land (grey when you could not get one out in
time), and the duel slows while it is lit — 0.4x on Easy (the 0.2 s window lasts 0.5 s), 0.6x Normal, 0.75x Hard,
0.85x Hellwalker. Only the duel slows (its frame clock and the two fighters), never the rules: the simulator, the
training and the keeper's view of time are untouched. The ring times the impact the wind-up *shows*, so feints and the
delayed heavy still bait — a player who parries every ring is a habit the keeper learns and fakes. Unparryable attacks
(the killer thrust, the grab) get no ring; their telegraph is **violet**, so red only ever means "parry now".

**Console** (`~`): `hw.Reset [seed]` · `hw.Tier normal|adaptive` · `hw.InjectParry <frames>` (A3) ·
`hw.Autoplay <kind> [skill]` · `hw.Debug` · `hw.Blind` · `hw.ResetModel` (the keepers forget you) · `hw.Menu <page>` ·
`hw.Map [open|close|track <0|1|2|auto>|zoom <1-4>]` (open world) · `hw.Shot` · `hw.Photo player|boss|off [yaw dist height]` ·
`hw.Blade <0|1|2> <pitch yaw roll> [x y z]`.

## The open world

| | |
|---|---|
| **Valley** | `HWWorldGen`: a 2.4 km heightfield (hills, ridges, a mountain wall) with flattened plazas and carved paths — a pure function of the seed. `AHWOpenWorld` meshes it (10x10 procedural chunks, 460k triangles, collision), lights it (sun, atmosphere, volumetric clouds, fog) and dresses it (`HWDressing`: firs, broken firs, cliff outcrops, boulders, stones, grass and fern clumps from the environment packs; each instance fitted from the mesh's own bounds; ~55k instances). Builds in ~1.5 s. |
| **Ground** | a code-built four-layer material (`HWTerrainMaterial`): the Lighthouse pack's rock, scrub, path-dirt and ash textures, world-projected and blended by weights the generator writes into the vertex colour (rock on the steeps, dirt on paths and plazas, ash on the heights). Without the pack, the vertex colour is the albedo. |
| **Sites** | 4 bells (checkpoints), 3 shrines, 6 ruins with the Game Animation Sample's traversable blocks (vault / mantle). With Paragon Monolith: the shrines are dark "Evil" fortresses (gate arch, wall slabs, barbican spires, spiked sconces, broken statues at the approach; the final one with two tall keeps); the ruins are jungle stone (decorated floor, mossy columns, a ring arch, rubble, stone over the traversal blocks); the Western Watch keeps the Lighthouse pack's lighthouse. The greybox stays underneath as the hidden colliders, so the duel floor and walls behave the same with or without the packs. |
| **Keepers** | the Ninefold Warden (Sevarog, the Warden script) · the Monkey Sage (Wukong, the Sage script) · the Warden, Returned — reborn in stone (the Stone Golem; the final shrine — sealed until the other two fall; reads you in Adaptive AI). |
| **The Crossroads** | a hamlet around the central bell (`HWSettlement.cpp`, the Desert City kit): mud-brick houses facing the bell with the roads left open, a market of fabric stalls, fire pits, chimney smoke, great rocks around it (its plaza is flattened to 36 m). |
| **Fire and smoke** | Niagara Examples: fire on logs in the bells' braziers (lit when you ring them), the pits and the shrines' sconces (out when the seal breaks); smoke rising behind every shrine whose keeper lives; a teleport-in as Soul wakes and as both fighters enter a duel; the loser of a duel shatters into embers. |
| **Map** | M: the valley from above — a 1024² picture rendered once per world from `FHWWorldGen` (ground colour, paths and plazas, hill shading, 20 m contours) on a pool thread (~1 s, never on the game thread), drawn with Canvas; the keepers, bells and you over it. The pure parts (projection, the zoomed view, edge-indicator placement, the tracking rule) are `HWMap` (tests `Project.HellwalkerRL.Map.*`). |
| **Flow** | E at a shrine's gate: the explorer steps out, the duel runs on the plaza, and when it ends you step back into the world. Every keeper reads the same you: the RL keeper's memory is shared across the whole walk. Die: you wake again (at your bell, or — during the build phase — at the next keeper's gate). |
| **Modes** | Normal (every keeper scripted) · Adaptive AI (every keeper the RL keeper). (66 Days was removed; an old 66-Days save continues as Adaptive AI.) Progress is saved at bells and shrines (`UHWSaveGame`); the keepers' memory of you is not — they forget you when you quit. |
| **Build phase** | new games and deaths put you just outside the next keeper's gate. `-HWSpawnAtBell` restores the bells. |

**The script.** Normal mode's two scripts (the Warden, the Sage) are the reference project's, played verbatim by `FScriptBrain`; the RL keeper has no script — the three Adaptive AI keepers differ only in look, health and place.

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
| **the Warden** (Sevarog) | pressure: heavies into your guard | 1508 · **7.5** · 6.6 · 25.3 |
| **the Sage** (Wukong) | baits: feints and evasions | 1410 · 6.4 · **12.9** · 24.7 |
| **the Returned** (the stone golem, the Hell Gate) | the reader: fast, exact counters; 25 % more health | 1425 · 3.3 · 2.6 · **51.9** |

### Difficulty

**Combat, after play (2026-10-03):** the parry window is **12 frames** (0.2 s) before impact, up from 8, and a clean parry
now buys a breath — the keeper may guard, step or move as soon as its stagger ends but **starts no attack for another
0.75 s** (~1.1 s from the parry). Every brain plays by these rules (they are the shared core's), and the keeper was
retrained under them (Results below).

The same network at every difficulty: the skill input slows its eyes (perception 0.1 → 0.27 s), its decisions
(every 0.1 → 0.2 s), shortens its strings (3 → 1) and doubles the grab / killer cooldowns.

**After play (2026-10-04): softer, and a learning arc.** "I lose every fight, so the boss learning my trick never shows."
Three changes, none of which touches the rules the keeper was trained on:

* **Every difficulty hits softer** (`UHWSettingsSubsystem::PresetFor`, both modes): the keeper's attacks deal 60 % of their
  damage on Easy, 75 % Normal, 85 % Hard, 90 % Hellwalker (`FDuel::KeeperDamageScale`, health only — never the guard
  drain; 1 in training, the simulator and the parity runs). Normal is now the default difficulty.
* **The parry assist** (above), on at every difficulty, Hellwalker included.
* **Insight — the Adaptive AI arc** (`HW::FRLInsight`, replacing the old Adaptive difficulty): the keeper's strength now
  follows how well it actually predicts *you*. After each fight, insight moves (an exponential moving average) toward that
  fight's read-head accuracy on its attack calls, normalised between a near-random player (40 %) and a solved one (70 %);
  the next fight's skill walks the difficulty's range with it (Easy 0 → 0.4, Normal 0 → 0.7, Hard 0.15 → 0.85, Hellwalker
  0.3 → 1), it samples loosely while it does not know you (temperature 2.5, gone at insight 0.7) and waits 3.5 s between
  attack strings at first (gone at 0.6) — it watches before it presses. Repeat one trick and its calls come true, so
  insight climbs over four or five fights; change tricks and its calls go wrong, so insight decays (explicit, gradual
  forgetting on top of the memory's own). Insight is session state like the memory: kept keeper to keeper, dropped on
  quit or reset.

The arc, measured (`Thesis.bat --arc`: 64 sessions of 6 lethal fights per player against the shipped keeper, the Normal
preset, the simulator's habit players executing one answer to every swing; win % per fight):

| player (simulated) | fights 1 → 6, player win % | insight before each fight |
|---|---|---|
| always blocks, skill 0.85 | 98 · 92 · 70 · **28** · **3** · 0 | 0 · .14 · .27 · .43 · .59 · .71 |
| always steps left, skill 0.85 | 97 · 94 · 80 · **48** · **19** · 3 | 0 · .08 · .19 · .34 · .50 · .64 |
| always parries, skill 0.60 | 92 · 69 · 45 · **27** · **17** · 22 | 0 · .19 · .36 · .51 · .63 · .72 |
| always parries, skill 0.85 | 100 · 100 · 98 · 100 · 98 · 100 | 0 · .24 · .46 · .64 · .74 · .80 |
| always attacks, skill 0.85 | 6 · 0 · 0 · 0 · 0 · 0 | 0 · .31 · .51 · .63 · .71 · .78 |
| near-random, skill 0.85 | 95 · 98 · 98 · 100 · 100 · 97 | 0 · .03 · .05 · .05 · .06 · .08 |

A clean trick wins the first two or three fights and is countered by the fourth or fifth; a player who stays
unpredictable keeps its insight low. Two honest limits: a near-perfect parry bot (skill 0.85 reads half the feints) is
never countered — the parry-and-punish weakness the league found, made larger by the easier parry rules (a human who
parries on the ring is baited by its feints and delayed heavy, which the bot is not); and switching to another *predictable*
trick does not reset the arc — it reads the new one within a fight. Hard and Hellwalker counter faster (Hellwalker: a
blocking player wins fight 1 about half the time).

(The old ladder, measured before this change at full damage and fixed skill: Easy 524 keeper damage/min, the script
701, Normal 1121, Hard 1257, Hellwalker 1322 — `RL/eval.py` ladder.)

### Sound and feel

`UHWAudioSubsystem` plays the duel from the packs already in Content (no hand-made assets; a missing pack is silent):
swings by weight (fast, heavy, feint, the killer's warning), the keeper's effort, pain and death in its own voice
(Sevarog for the Warden, Wukong for the Sage, Sevarog lowered and slowed for the Returned), hits, a bright parry over
a ringing blade, a dull block, ghoststeps, guard breaks, and the READ sting under the keeper's laugh. The duel music
fades in for the fight and **tightens as the keeper pulls ahead** (its health lead over you, smoothed), and a low
breathing layer comes in when you are close to death. (The first version followed the RL critic's value; review showed
that value is the normalised *remaining* return, which falls as the keeper closes in on a kill — no win estimate.) Gamepad rumble on hits taken, parries and READs; the camera
kick on hits honours the accessibility toggle. Scripted checks read the per-duel cue counts from the log.

## The website and the research data

`web/` is the game's website (static, hosted on Render): the download (itch.io), a leaderboard, and a page for every
player with their skills — parry success, win rate, hit accuracy, dodges, damage — and **the keeper's notebook**: what
the RL keeper expects them to do against each of its attacks next to what they actually did. It is for the research
paper's results section, so there is a short survey ("did it feel like it was reading you?", fairness, difficulty, fun)
and an export to CSV.

There is **no login**. Each copy of the game signs in to Firebase anonymously (invisible: a random id and a made-up
nickname such as *Ashen Wanderer 4821*) and, after every duel, `UHWTelemetrySubsystem` sends one record (Firestore REST:
the fight + server-side increments of the player's totals). Offline, fights wait in a save slot and go up later.
"Open my stats page" (pause or title menu) opens the player's page linked to that copy of the game — only that browser
can rename, answer the survey or **reset** ("forget everything it learned about me": the player's fights are deleted
and the totals zeroed). Fights played by the autoplay bot or the tools are never uploaded. The game shows one line
saying anonymous stats are sent for research. Nothing is sent until `Config/DefaultGame.ini` `[HWTelemetry]` holds a
Firebase project — setup, step by step, in [web/README.md](web/README.md); the data contract is
[web/CONTRACT.md](web/CONTRACT.md); the security rules are `web/firebase/firestore.rules`.

Each fight record (contract v2) also carries the parry assist the player had, the keeper's damage scale, the parry window
and the keeper's insight when the fight began, so assisted and unassisted parries can be told apart.

Verified end to end against a local mock of the Firebase REST API (`web/dev/mock_firebase.py`): 112 contract checks
(`web/dev/e2e_test.py`, incl. the attacks the rules must refuse), and the real game uploading to it — two scripted
fights arrived with the player's totals exactly their sum, a bot fight was refused, a fight played while the server
was down waited and arrived at the next launch, and the site showed the player linked by the game's token.

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

`Content/HellwalkerRL/RL/hellwalker_rl.hwrl` = `RL/checkpoints/rl2_newrules2/keeper_1p45e9.hwrl`, 1.45 × 10⁹ decisions
of recurrent PPO from random initialisation: the 10⁹ run `rl2_keepers` (four league rounds; ~2 h on the RTX A5000 + 36
CPU threads), two fine-tunes that closed the last B0 gap (1.17 × 10⁹), then — after the combat changes from play — a
fine-tune under the new rules (`rl2_newrules`, 1.5 × 10⁸) and one more with two league rounds (`rl2_newrules2`,
1.7 × 10⁸): the new rules opened a hole the league had to close (below). Report: `RL/reports/keepers_1p45e9.md`.

**B0 — the check the reference project had to pass, with the RL keeper as the adaptive arm** (`Tools\Thesis.bat`,
64 sessions per cell, 20 player profiles × skills, each keeper):

| check | the Warden | the Sage | the Returned |
|---|---|---|---|
| harder than the script against every skilled profile | **PASS** — 1.6–6.5× its damage/min | **PASS** — 2.0–4.3× | **PASS** — 1.6–6.6× |
| aggression floor: swings/min ≥ the script's in the same fights | **PASS** — closest: DodgerLeft 0.9, 74.3 vs 65.1 | **PASS** — 74.0 vs 56.5 | **PASS** — 77.1 vs 65.1 |
| a masher dies | **PASS** — in 8.6 s (the script: 15.6 s) | **PASS** — 8.8 s (18.0 s) | **PASS** — 16.3 s (15.6 s) |
| net exchange never worse than the script (5 % tolerance) | **PASS** — worst: RhythmParrier 0.5, 3.91 vs 3.56 | **PASS** | **PASS** |

**The reading test** (`RL/habits.py`, the C++ attack log: 256 held-out sessions per group, clean hits only):

| players who always… | keeper's first 5 attacks | keeper's attacks 21–60 | its clean hit rate |
|---|---|---|---|
| parry | FastSlash 57 %, DelayedHeavy 35 % | FastSlash 58 %, DelayedHeavy 29 % | 0.47 → 0.43 |
| block | DelayedHeavy 46 %, FastSlash 40 % | HeavyCleave 54 %, DelayedHeavy 27 % | 0.32 → 0.43 |
| dodge left | DelayedHeavy 39 %, FastSlash 26 % | **HeavySweepLeft 53 %**, HeavyCleave 31 % | 0.27 → 0.42 |
| dodge right | FastSlash 39 %, DelayedHeavy 39 % | FastSlash 37 %, DelayedHeavy 36 % | 0.32 → 0.49 |

It opens much the same against everyone and diverges toward each habit's answer as the session goes on (move-mix
divergence between the four groups **0.15 → 0.63 bits**, the highest of any keeper here); against left-dodgers it finds
the left sweep that tracks the dodge. When a player switches habit after 30 keeper swings its hit rate falls
0.38 → 0.27 (its read has gone stale) and recovers to 0.40. Against held-out habit players its hit rate rises with the
session (attacks 1–5 → 21–60: 0.63 → 0.70, +0.07 against strong habits). The parrier is the hard case now: the wider
parry window makes a pure parrier hard to hit at all (0.47 → 0.43).

**Classic vs RL on identical seeded held-out players** (C++, 90 s immortal fights; damage dealt / taken per minute,
swings per minute, hit rate):

| players | Pathbreaker (script) | Hellwalker (classic tally brain) | Hellwalker (RL) |
|---|---|---|---|
| strong habits | 998 / 492 / 66 / 0.59 | 1169 / 335 / 67 / 0.66 | **1716 / 99 / 81 / 0.73** |
| near-random | 962 / 594 / 66 / 0.56 | 1127 / 411 / 68 / 0.65 | **1661 / 152 / 81 / 0.75** |
| habit switchers | 929 / 680 / 66 / 0.55 | 1112 / 492 / 66 / 0.64 | **1843 / 148 / 78 / 0.73** |
| learning players | 841 / 478 / 65 / 0.51 | 969 / 359 / 66 / 0.57 | **1534 / 145 / 75 / 0.61** |
| the reference bots | 579 / 349 / 59 / 0.38 | 774 / 214 / 62 / 0.44 | **1197 / 227 / 74 / 0.45** |

**What the combat changes did, and why it was retrained.** The 1.17 × 10⁹ keeper under the new rules, untouched, was
~10 % easier and still read players, but failed B0's aggression floor for all three keepers against the rhythm parrier
(more parries, each now costing it a pause it never trained with). A fine-tune under the new rules fixed B0 (1.28 × 10⁹)
— but a fresh exploiter then beat it **100 %** with an exchange of **12.5**: parry, punish with heavies through the
keeper's pause, repeat. The keeper may guard or step during the pause; it had simply never learned to. Two more league
rounds put that strategy into its population (round exploiters' exchange 33 → 15.6); the shipped keeper takes far
more care: a fresh exploiter still wins, but its exchange fell to **3.5** (it now takes 387 damage/min where it took 94).

**Exploitability (RL-4).** A fresh exploiter (`RL/exploit.py`, 4 × 10⁷ decisions, ~6 minutes) still beats every keeper
we trained — 98 % against the 10⁹ keeper (exchange 2.0, close pressure), 93 % against the 1.17 × 10⁹ one (1.5,
guard-and-poke), 99 % against the shipped one (3.5, parry-and-punish). Each league round closes the hole it is shown
and leaves the next; the league has not converged. Note that the exploiter's 360 hp against the keeper's 1100 already
counts in the exchange: these are skilled, single-minded strategies, which is what a determined human may find too.

**Unreal vs the simulator** (`Tools\Parity.bat`, `RL/reports/parity.md`). The same autoplay players fought the same
keeper in the game's arena and in the training simulator (3 bots × RL / script, 18 Unreal fights per cell). The first
measurement found the keeper side matching but the *player* attacking 3-45× as often in Unreal; the bot's own counters
showed why — equal mean distance, but 8× as many frames within its reach — because the characters accelerated and braked
(~37 cm of slide per stop) while the simulator moves and stops instantly, so the keeper's learned spacing went soft. With
the duelists' movement made instant and the approach-stop distance shared, 6 of 72 metrics differ (26 before): the RL
keeper's cells agree within a few percent (damage, swings, hit rate, fight length; move mix ≤ 0.013 bits), and what is
left is mostly the scripted keeper against the rhythm parrier (likely capsule collision blocking steps the simulator
lets pass). Details: RL/DESIGN.md §12. (Measured with the 1.17 × 10⁹ keeper before the combat changes; the movement fix is
independent of them.)

**Not yet met:** RL.md's "not a random-player bully" check — against near-random players the RL keeper is ~73 % more
dangerous than the script, not equal: it is stronger against everyone, and its reading shows in *what* it throws, not
in a smaller margin against random players. The population-average curve's "dips after a switch" / "reads, not
strength" checks fail (its rise passes; the pure-habit reading test above is the measure that separates them). The
human playtest (RL-6) is the real judge of difficulty and fairness.

## Layout

```
Source/HellwalkerRL/Public/HWCore, Private/HWCore   engine-free C++ core, compiled into the game AND the tools
    HWTypes / HWMoves / HWFighter / HWDuel     the rules: frame data, moves, frame-stepped combat (PLAN §5, §6)
    HWBrain                                     IBossBrain, decision records, the Read Meter, the boss scripts
    HWScriptBrain                               Pathbreaker (the Normal mode): the script verbatim
    HWRLTypes / HWRLObserver / HWRLPolicy / HWRLBrain   the RL keeper: layouts, senses + masks, the network, the brain, the notebook
    HWEncounter / HWSim                         duel + brain + stats; 2-D arena and simulated players (habit, learning players)
    HWCoreTests / HWRLTests                     the done-tests, also run by ThesisSim
Source/HellwalkerRL/...        the Unreal layer: duel subsystem, session memory + insight, characters, HUD + menus (HWMenu,
                               HWSettings), the parry assist (HWParryAssist), audio (HWAudio), the valley map (HWMap),
                               research telemetry (HWTelemetry), open world, tests
Sim/ThesisSim.cpp, SimArms.*   B0: Pathbreaker vs an adaptive arm (the RL keeper, or the classic brain); --arc: the insight arc
Sim/Classic/                   the reference project's tally brain (playstyle model, payoff table) — tools only
RL/native/                     the training environment (FRLEnvBatch, the exploiter env FRLPlayerEnv) + the hwrl.dll C ABI
RL/*.py                        the trainer: hwcore (ctypes), env, players, model, ppo_rnn, train, export, eval, habits,
                               exploit (RL-4), parity (Unreal vs simulator)
RL/tests/                      torch / C++ / ONNX parity
.github/workflows/ci.yml       CI without Unreal (Tools\CI.bat locally)
web/                           the website (static) + Firebase rules + the REST mock, e2e test and CSV export (web/README.md)
render.yaml                    the Render Blueprint for the website
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
10. **Combat changes from play, and retraining for them.** The parry window widened (8 → 12 frames) and a parried keeper
   pauses before its next attack (PLAN §6). Changing the rules changes the environment the keeper was trained in, so it
   was fine-tuned under them — and the new rules' parry-and-punish loop needed the league to close it (Results).
11. **Packaging.** A packaged game cannot compile shaders, so `Tools\MakeMaps.bat` also saves the code-built
   materials (`/Game/HellwalkerRL/Materials`) and bakes the instanced-mesh usage into the dressing's pack materials;
   editor-only code is guarded for the Shipping build; cooking bypasses the Zen store (its data lives on the full C:
   drive of the build machine). The keepers' voice lines are cooked one by one (an asset-manager rule), not the
   350 MB voice packs.
12. **The keeper's strength follows its insight into you; the game is softer; a parry assist.** From play: the owner lost
   every fight, so the arc the project exists to show — a trick works, then the keeper reads it — never appeared. Rather
   than retrain, three game-side changes leave the trained environment untouched: the difficulty scales the keeper's
   health damage (`FDuel::KeeperDamageScale`, 1 in training); a presentation-only parry assist (a red ring and duel-only slow
   motion, keyed to the wind-up so baits still bait); and `FRLInsight`, which sets each fight's skill, sampling temperature
   and breather from how well the read head called the player's answers to its attacks in the fights so far (calibrated
   with `Thesis.bat --arc`; replaces the outcome-driven Adaptive difficulty). The skill input stays within its trained
   range, but varies between fights of one session (training fixed it per session), and the 3.5 s breather at insight 0
   is an unobserved mask like Easy's — measured by the arc, not assumed. Fight records carry the assist, the damage scale
   and the insight (web/CONTRACT.md v2), so the paper can split parry and win rates by them.

## Status against RL.md

| | Deliverable | Status |
|---|---|---|
| RL-0 | `FRLEnv` + bindings + random agent | **done** — 600,000–820,000 decisions/s (36 threads), ~130,000 on one; seeded replays identical for any thread count; masks never allow an illegal move |
| RL-1 | feed-forward PPO vs one fixed habitual bot | **done** — learns that bot's counters; it also found the killer-spam exploit, now masked |
| RL-2 | recurrent PPO vs the procedural population | **done** — 10⁹ decisions with the reading test logged during training; play diverges toward each habit's counter; hit rate dips and recovers around a habit switch |
| RL-3 | constraints and fairness | **done** — all four B0 checks pass for all three keepers (64 sessions); the aggression floor holds per matchup |
| RL-4 | exploiter league | **done, not converged** — seven rounds, fourteen exploiters in the population, learning players too; each round closes the hole it is shown (the parry-and-punish exchange 12.5 → 3.5), but a fresh exploiter still finds a winning strategy (above) |
| RL-5 | inference + the RL brain in Unreal | **done** — `FRLBrain` plays the Adaptive AI mode (fallback: the script when the model file is missing); three keepers, four difficulties, the insight arc, the parry assist, the notebook, menus, sound, the valley map with keeper tracking; the Unreal-vs-simulator gap measured (above); anonymous research telemetry + the website; the Shipping .exe |
| RL-6 | human playtest (B4) | **yours** — the website collects it: every player's fights, what the keeper learned, and the survey; `Tools\Arena.bat -HWBlind` labels the tiers Variant A / B |

Tests: `Tools\Thesis.bat` (36 core tests: combat incl. the parry window and the post-parry pause, the script, the RL
keeper incl. skill, learning players, the notebook and the Easy breather, the classic benchmark), `Tools\Test.bat` (68
Unreal automation tests: core and RL wrappers, the shipped model, menus and settings, the two modes, keepers, difficulty and
insight, the parry assist against a real duel, the music's tension, world generation, the valley map and keeper tracking,
the research telemetry, animation casts), `Tools\Thesis.bat --arc` (the insight arc),
`web/dev/e2e_test.py --spawn` (112 checks of the website's data contract and security rules against the mock), `RL\.venv\Scripts\python.exe RL\tests\test_parity.py` (torch vs C++ vs ONNX),
`RL\native\out\envbench.exe` (throughput, determinism, masks, rollover, attack log), `Tools\CI.bat` (all of the
engine-free ones, as GitHub Actions runs them).
