# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Hellwalker is an Unreal Engine 5.8 **C++** project (the owner wants C++, not Blueprint logic): a boss that reads the
player's habits and counters them, themed on Phantom Blade Zero's Hellwalker mode. `PLAN.md` is the spec (milestones
A0–C3, frame data §6); `README.md` has the controls, pack table, status against the plan and the deviations B0 forced
("What B0 changed") — read both before changing combat rules.

## Commands (Windows; engine at `D:\Shadow\Epic Games\UE_5.8`)

| | |
|---|---|
| `Tools\Build.bat` | build `HellwalkerEditor` Win64 Development. **Close the editor/game first** — a running editor (Live Coding) or game locks the module DLL and the build fails |
| `Tools\Test.bat` | every automation test, headless (`Automation RunTests Project.HellwalkerRL`); prints `Test Completed` lines, exit 0 = all passed |
| `Tools\Thesis.bat` | B0: builds `Sim\out\ThesisSim.exe` with plain MSVC (no Unreal), runs `--tests`, then `--sessions 16` (extra args pass through, e.g. `--script 1` = the Sage) |
| `Tools\Play.bat` | the open world in a window (default map). `Tools\Arena.bat` = the duel alone (`-HWBoss=Sevarog|Wukong|Golem`) |
| `Tools\MakeMaps.bat` | regenerate `Content/HellwalkerRL/Maps/L_Hellwalker` + `L_Arena` via `-run=HWMakeMaps` |

Single test (names: `Project.HellwalkerRL.Core.*`, `.World.*`, `.Anim.*`, plus a few top-level):

```
"D:\Shadow\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" HellwalkerRL.uproject -ExecCmds="Automation RunTests Project.HellwalkerRL.World.GroundLayers;Quit" -unattended -nullrhi -nosplash -nosound -nopause
```

Scripted play checks (how changes are verified — there is no other way to "see" the game):

```
UnrealEditor.exe HellwalkerRL.uproject -game -windowed -ResX=1600 -ResY=900 -HWWorldMode=hellwalker -HWNoSave "-HWExec=10:hw.Duel 0|14:hw.Kill boss" -HWShotAt=12 -HWShotEvery=4 -HWShots=3 -HWQuitAt=30 -log -unattended
```

- Output goes to `Saved/Logs/HellwalkerRL.log` (not stdout); screenshots to `Saved/Screenshots/WindowsEditor/`; surveys and
  reports to `Saved/HellwalkerRL/` (`MeshSurvey.txt` via `-HWMeshSurvey`, `AnimSurvey_<cast>.txt` via `-HWAnimSurvey`,
  `Controls.txt` via `hw.Controls`). World phase changes log as `World phase: ...`.
- Timed console commands (`"<seconds>:<cmd>|..."`): `hw.Duel <i>`, `hw.Kill boss|player`, `hw.Goto <site id>`,
  `hw.GotoBlock <n>`, `hw.Hold <IA_Name> <s> [x y]` (inject any `/Game/Input/IA_*` action as the player), `hw.Where`,
  `hw.Interact`, `hw.Help`, `hw.Controls`, `hw.Shot`. Declared in `HWConsole.cpp` / `HWAutomation.h`.
- Launch quirks: Windows PowerShell splits `-Foo=0.8` into two args — launch UE from Git Bash or `.bat`. In Git Bash,
  prefix `MSYS_NO_PATHCONV=1` when passing a `/Game/...` map path. First runs after new pack content spend minutes
  compiling shaders/meshes (use long timeouts).

## Architecture

**Two layers, one rulebook.** `Source/HellwalkerRL/{Public,Private}/HWCore/` is engine-free C++ (namespace `HW::`):
frame data and move table, `FFighter`/`FDuel` (frame-stepped combat, 60 fps), `FPlaystyleModel` (variable-order Markov
model shared across a session), payoff table, `FBossBrain` (scripts, substitution, aggression floor, Read Meter),
`FEncounter`, and `HWSim` (simulated players). `Sim/ThesisSim.cpp` compiles **the same files** with plain MSVC and
UE-strict warnings-as-errors (`Sim/build.bat`), so HWCore must not include Unreal headers and must keep compiling
there. Changing rules/tuning in HWCore means re-running `Tools\Thesis.bat` (B0 must still PASS) as well as the tests.

**The Unreal layer is presentation + input around that core.** `UHWDuelSubsystem` owns the encounter and steps it on a
fixed frame cursor; characters (`AHWCharacterBase` → player / boss) only depict the simulated state. Animation is
frame-locked: a native `UHWAnimInstance` (custom proxy, no Animation Blueprint) samples clips at times computed by
`HWWarpMoveTime` so contact lands on the move's impact frame. Each look is a *cast* in `HWAnimCasts.cpp`
(mesh, clip per role/move, trail FX) whose clip choices come from measured survey data (contact time, sweep side);
the `Anim.Casts` test enforces that sweeps cross the side their hitbox tracks. A cast may set `LookMeshPath` to draw a
different body over a hidden driver via the Game Animation Sample's runtime retargeting (the Stone Golem).

**Game modes and maps.** `AHWOpenWorldGameMode` (default; `L_Hellwalker`) and `AHWGameMode` (arena; `L_Arena`). The
maps contain only a game-mode override — everything (terrain, sites, lights, arena) is built in C++ at play.

**Open world.** `FHWWorldGen` is a pure function of the seed: heightfield, sites (bells, shrines, ruins, the Crossroads
hamlet's plaza), paths, `PathDistance`/`SiteDistance`, ground layer weights. `AHWOpenWorld` meshes it (procedural
chunks + a code-built 4-layer material that reads the vertex colour as layer weights), scatters pack meshes
(`HWDressing`, placed by `HWBuild::Fitted` from each mesh's bounds, never by assumed pivots), builds ruins with the
sample's `LevelBlock_Traversable` and the hamlet (`HWSettlement.cpp`). `AHWBell` / `AHWShrine` keep a greybox body as
the (hidden) colliders and draw pack architecture over it, so gameplay is identical with or without packs.
`AHWOpenWorldGameMode` runs the flow (title → exploring → duel → duel over → ending), saves (`UHWSaveGame`), and the
build-phase rule: new games and deaths spawn the player just outside the next shrine's gate (`-HWSpawnAtBell` reverts).

**The explorer** is the Game Animation Sample's `SandboxCharacter_Mover_Ragdoll` (`-HWExplorer=cmc|mover` to switch),
dressed as Soul by `DressExplorerAsSoul` (Fighter-pack Manny on `ABP_GenericRetarget`, component tag names the
retargeter). Mover pawns ignore `AActor::TeleportTo` — use `HWBuild::TeleportPawn`. `LevelBlock_Traversable` drops its
spawn scale — call `SetActorScale3D` after spawning (the sample's traversal reaches ~2.2 m; 2.5 m walls just jump).

**Input** is created entirely in C++ (`AHWPlayerController`): the duel context, and an explore context at priority 10
layered over the sample's own `IMC_Sandbox` (so a shared key goes to ours — don't take C, R, Space, Shift, Ctrl there).

**Content.** No hand-made binary assets: materials are built in code (`HWGlowMaterial.cpp`), and every Fab pack
(Paragon, Lighthouse, KiteDemo, Desert City, Thornblade `Sword`, Stone Golem, Niagara Examples, Slash Trail, Fighter,
the Game Animation Sample folders) is loaded by path through optional loaders (`HWBuild::OptionalMesh` /
`OptionalPackageMesh` / `OptionalSystem`) with a greybox fallback, and is gitignored — only `Content/HellwalkerRL/Maps`
is tracked. Niagara Examples' `NS_Fire` burns the mesh it is attached to (use `HWBuild::Fire`, which supplies the pack's
logs/torch fuel); mesh-sampling effects go through `HWBuild::MeshEffect`. Pack meshes placed as instances need
`HWBuild::EnsureInstancedUsage` or a game run draws the default material.

**Tests** live in `Private/HWCore/HWCoreTests.cpp` (the PLAN §3 done-tests as plain functions, also run by
`ThesisSim --tests`) and `Private/Tests/` (UE automation: core wrappers, world generation, animation casts).
