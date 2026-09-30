# Hellwalker — a boss that reads you

An Unreal Engine 5.8 **C++** action game in the spirit of *Phantom Blade Zero*'s Hellwalker mode:
the boss, **the Ninefold Warden**, watches how you fight, learns your habits, and counterattacks them
("if you always dodge left, it tracks left; if you parry on a rhythm, it feints").
Design spec: [PLAN.md](PLAN.md). Theme research: [Phantom Blade Zero Hellwalker Mode.md](Phantom%20Blade%20Zero%20Hellwalker%20Mode.md).

The game is an **open world** — an ash valley generated in C++ at startup — with three boss shrines. You
explore as the Game Animation Sample's motion-matched character (run, sprint, vault, mantle), ring bells to
rest, and challenge each shrine's keeper. The duel itself is the arena game this project began as, unchanged
and B0-validated, played on the shrine's plaza. Every keeper reads the same you: the playstyle model is shared
across the whole world, so the final shrine's Warden has watched you fight all the way there.

Everything we wrote is C++: the world, the sites, the flow, the saves, the HUD, the arena, lighting, Enhanced
Input and the fighters' animation (no Animation Blueprint). There are no binary assets of our own; the look
comes from Fab packs and the Game Animation Sample, by path, and every one of them is optional (without them
the world and fighters are greybox).

## Play

| | |
|---|---|
| **Editor** | open `HellwalkerRL.uproject`: it starts on `/Game/HellwalkerRL/Maps/L_Hellwalker` — press **Play** (the valley is built in C++ when play starts). `L_Arena` = the duel alone |
| `Tools\Build.bat` | build the editor target (close the editor first) |
| `Tools\Play.bat` | **the open world** in a window (title: new game / continue). `-HWWorldMode=hellwalker` skips the title |
| `Tools\MakeMaps.bat` | rewrite the two maps (they hold only a game-mode override; never hand-edited) |
| `Tools\Arena.bat` | the arena duel on its own (the B4 blind test; `-HWTier=`, `-HWBlind`, `-HWBoss=Wukong`) |
| `Tools\Demo.bat habitual 0.8 hellwalker` | watch a simulated player fight the Warden in the arena |
| `Tools\Test.bat` | every done-test, headless inside Unreal |
| `Tools\Thesis.bat` | B0: the headless thesis simulator (no Unreal) |

### Controls (F1 in game shows this panel)

Generated from the live bindings: `hw.Controls` writes every mapping to `Saved/HellwalkerRL/Controls.txt`.

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
| R · 1 / 2 | menu | arena: fight again (it remembers) · choose tier |

**Menus and debug** — title: 1 / 2 / 3 new walk (Pathbreaker / Hellwalker / 66 Days), Enter continue ·
F1 controls panel · F3 debug overlay (boss read, hit volumes) · Esc quit.

**Tiers** — one brain, one flag (PLAN B1):
* **Pathbreaker** — the scripted control arm: a fixed pattern you can learn.
* **Hellwalker** — the same script, but the model decides which action fills each slot, and the
  "Lucky Draw" logic reacts to what just happened (hit → press, whiff → back off, blocked → guard
  pressure, parried → reset or feint). After a model-driven counter lands, the **Read Meter** shows
  what it predicted you would do — testimony, not telegraph.

**Console** (`~`): `hw.Reset [seed]` · `hw.Tier pathbreaker|hellwalker` · `hw.InjectParry <frames>` (A3) ·
`hw.Autoplay <kind> [skill]` · `hw.Debug` · `hw.Blind` · `hw.ResetModel` · `hw.Shot` ·
`hw.Photo player|boss|off [yaw dist height]` · `hw.Blade <0|1|2> <pitch yaw roll> [x y z]`.

## The open world

| | |
|---|---|
| **Valley** | `HWWorldGen`: a 2.4 km heightfield (hills, ridges, a mountain wall) with flattened plazas and carved paths — a pure function of the seed. `AHWOpenWorld` meshes it (10x10 procedural chunks, 460k triangles, collision), lights it (sun, atmosphere, volumetric clouds, fog) and dresses it (`HWDressing`: firs, broken firs, cliff outcrops, boulders, stones, grass and fern clumps from the environment packs; each instance fitted from the mesh's own bounds; ~55k instances). Builds in ~1.5 s. |
| **Ground** | a code-built four-layer material (`HWTerrainMaterial`): the Lighthouse pack's rock, scrub, path-dirt and ash textures, world-projected and blended by weights the generator writes into the vertex colour (rock on the steeps, dirt on paths and plazas, ash on the heights). Without the pack, the vertex colour is the albedo. |
| **Sites** | 4 bells (checkpoints), 3 shrines, 6 ruins with the Game Animation Sample's traversable blocks (vault / mantle). With Paragon Monolith: the shrines are dark "Evil" fortresses (gate arch, wall slabs, barbican spires, spiked sconces, broken statues at the approach; the final one with two tall keeps); the ruins are jungle stone (decorated floor, mossy columns, a ring arch, rubble, stone over the traversal blocks); the Western Watch keeps the Lighthouse pack's lighthouse. The greybox stays underneath as the hidden colliders, so the duel floor and walls behave the same with or without the packs. |
| **Keepers** | the Ninefold Warden (Sevarog, the Warden script) · the Monkey Sage (Wukong, the Sage script) · the Warden, Returned — reborn in stone (the Stone Golem; the final shrine — sealed until the other two fall; always reads you). |
| **The Crossroads** | a hamlet around the central bell (`HWSettlement.cpp`, the Desert City kit): mud-brick houses facing the bell with the roads left open, a market of fabric stalls, fire pits, chimney smoke, great rocks around it (its plaza is flattened to 36 m). |
| **Fire and smoke** | Niagara Examples: fire on logs in the bells' braziers (lit when you ring them), the pits and the shrines' sconces (out when the seal breaks); smoke rising behind every shrine whose keeper lives; a teleport-in as Soul wakes and as both fighters enter a duel; the loser of a duel shatters into embers. |
| **Flow** | E at a shrine's gate: the explorer steps out, the duel runs on the plaza, and when it ends you step back into the world. Die: you wake again (at your bell, or — during the build phase — at the next keeper's gate). |
| **Modes** | Pathbreaker (scripted keepers, the final one still reads you) · Hellwalker · **66 Days** (66 lives; when the last day passes, the save is erased). Progress is saved at bells and shrines (`UHWSaveGame`); the model is not — the keepers forget you when you quit. |
| **Build phase** | new games and deaths put you just outside the next keeper's gate. `-HWSpawnAtBell` restores the bells. |

**The Sage passes B0 too.** `ThesisSim --script 1` runs the thesis simulator on the Sage's script:
thesis, aggression floor, masher, net exchange — all PASS at 16 sessions per cell (its floor controller aims
5 swings/min above the shadow script: `HW::ApplyScriptTuning`, applied identically in the sim and the game).

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

## Layout

```
Source/HellwalkerRL/Public/HWCore, Private/HWCore   engine-free C++ core — the rules, the model, the brain
    HWTypes / HWMoves        frame data (PLAN §6) and the move table
    HWFighter / HWDuel       frame-stepped combat: windows, outcomes, stun, sha-chi (§5.2, §5.4)
    HWPlaystyleModel         interleaved variable-order Markov model + timing sidecar (§2.2)
    HWPayoffTable            the payoff matrix, measured through FDuel micro-duels (§2.4)
    HWBossBrain              script slots, substitution, Lucky Draw, aggression floor, Read Meter (§1.2, §2.4)
    HWEncounter              symbol observer (§2.1), telemetry rows (A2)
    HWSim                    2-D arena + simulated players + runner (B0's instrument)
    HWCoreTests              the done-tests of PLAN §3 as plain functions
Source/HellwalkerRL/...        the Unreal layer: duel subsystem (frame cursor, sweeps), characters,
                             lock-on, combat / brain components, HUD, arena, config, console, tests
Sim/ThesisSim.cpp            B0 — compiles the same core with plain MSVC, no Unreal
```

The simulator and the game compile **the same core files**, so B0's verdict is a verdict on the
rules the game ships with.

## What B0 changed (deviations from PLAN.md — each found by the simulator, each reviewable)

B0 did its job: it caught design defects before any engine work depended on them.

1. **The payoff matrix is measured, not authored.** The hand-authored matrix contradicted the frame data
   (e.g. "heavy vs light = +2, armor absorbs it" — but a light lands on frame 8 and armor starts on 10).
   `HWPayoffTable` now scores every boss move × player response × press timing by running a short duel
   through `FDuel` and measuring the health exchange. The plan's authoring rule survives as a clamp:
   defensive moves cap at +1. The authored matrix is kept as a switchable fallback (`--authored`).
2. **The timing sidecar is anchored to the *perceived* impact.** Players time presses to what the wind-up
   shows, not to the boss's commitment; measuring from the commitment made honest slow moves look like
   baits. Samples are taken only when the impact is at least 18 frames out (faster ones measure reaction,
   not timing), and a "bite" sidecar records whether a player times to a feint's fake or waits for the real
   strike.
3. **Combo decay + boss hitstun 22.** Player light chains now combo; after 3 hits in one stun the stun decays so
   nobody can be locked forever (B0 found an infinite loop at T=0).
4. **Threat abort.** A player swing already visibly winding up (≥ 6 frames, human reaction time) that would
   interrupt every legal attack sends the adaptive boss to guard/evade — rule 1, animation reading, never input reading.
5. **Aggression floor, matched-player form.** Besides the plan's `RefSwingsPerMin` term, the controller compares
   against the script's swing rate *in the same fight's slots*, and its pressure favours shorter attacks.
6. **Heavy tracking sweeps** join the Warden's move set so heavy slots can punish a one-direction dodger.

`Tools\Thesis.bat` (16 sessions per cell): thesis PASS, aggression floor PASS, masher lethal PASS, and a
stricter-than-plan net-exchange check (the boss must not trade health worse than the script) PASS within a
5% sampling tolerance.

## Status against PLAN.md

See the session summary / `Saved/HellwalkerRL/Telemetry/*.csv` for A2 data. Milestones implemented:
A0 (skeleton, lock-on, greybox arena), A1 (combat offence, sweep hit detection, one outcome per swing,
ResetEncounter + console), A2 (numeric overlay + per-exchange CSV, runs >20 ms mean frame time
discarded), A3 (sha-chi, block, parry with the reward floor, ghoststep via root motion, boss guard +
evade), B0 (thesis simulator), B1 (control arm, determinism), B2 (playstyle model with negative
control), B3 (adapt + counterattack, legibility rules, aborts to defence), B4 tooling (runtime tier
toggle, blind labels, per-tier move-selection frequencies), C1 (twin blades / glaive), C2 (characters,
frame-locked animation, telegraph overlays, afterimages, trails and impact bursts — see above), C3 (reason
strings, argmax assertion). Not done: B4's human playtest (needs testers).
