# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

HellwalkerRL is an Unreal Engine 5.8 **C++** project (the owner wants C++, not Blueprint logic): a boss — "the keeper" —
that reads the player's habits and counters them, themed on Phantom Blade Zero's Hellwalker mode, whose adaptive brain
is a **reinforcement-learning policy trained from scratch** (meta-RL, RL.md). It is a new project that used the
Hellwalker project (`D:\Shadow\GAME_NEW_ARAVINDA`) as REFERENCE ONLY — never modify that folder, and never open
`D:\Shadow\GAME_CORE 5.8` at all.

Read before changing things: `RL.md` (the RL plan and milestones), `RL/DESIGN.md` (the engineering contract: observation,
masks, tokens, rewards, network format, trainer), `PLAN.md` (the combat spec; frame data §6), `README.md`.

## Commands (Windows; engine at `D:\Shadow\Epic Games\UE_5.8`)

| | |
|---|---|
| `Tools\Build.bat` | build `HellwalkerRLEditor` Win64 Development. **Close the editor/game first** (Live Coding / a running game locks the DLL) |
| `Tools\Test.bat` | every automation test, headless (`Project.HellwalkerRL`); prints `Test Completed` lines, exit 0 = all passed |
| `Tools\Thesis.bat` | B0: builds `Sim\out\ThesisSim.exe` (plain MSVC, no Unreal), runs `--tests`, then the B0 sweep with the RL keeper (the shipped model) or, without one, the classic reference brain. Args pass through (`--brain classic`, `--sessions 4`, `--script 1`) |
| `Tools\RLBuild.bat` | builds `RL\native\out\hwrl.dll` (training env + C++ forward pass for Python, ctypes) and ThesisSim |
| `Tools\RLTrain.bat --stage rl1\|rl2\|rl3 --run <name>` | train (RL\train.py; GPU PyTorch in `RL\.venv`) |
| `Tools\RLEval.bat [--policy <hwrl>]` | RL.md §7 checks → `RL\reports\` |
| `Tools\RLShip.bat <hwrl>` | put a trained policy where the game loads it: `Content\HellwalkerRL\RL\hellwalker_rl.hwrl` |
| `Tools\Play.bat` / `Tools\Arena.bat` | the open world / the duel alone (`-HWBoss=Sevarog\|Wukong\|Golem`, `-HWPolicy=<file.hwrl>`) |
| `Tools\MakeMaps.bat` | regenerate `Content/HellwalkerRL/Maps/L_Hellwalker` + `L_Arena` via `-run=HWMakeMaps` |
| `Tools\Package.bat` | the standalone game (.exe) into `Build\Packaged` |

Python: always `RL\.venv\Scripts\python.exe` (torch 2.11 + CUDA 12.8, numpy 2, onnx, onnxruntime, tensorboard). **The C:
drive is nearly full**: set `TMP`/`TEMP` to `D:\Shadow\GAME_NEW_RL\RL\.pip-tmp`, never write large files to C:.

Single UE test: `"D:\Shadow\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" HellwalkerRL.uproject -ExecCmds="Automation RunTests Project.HellwalkerRL.RLModel;Quit" -unattended -nullrhi -nosplash -nosound -nopause`
(groups: `Project.HellwalkerRL.Core.*`, `.RL.*`, `.RLModel`, `.World.*`, `.Anim.*`, `.RngParity`, `.ConfigRoundTrip`).

Scripted play checks (the only way to "see" the game):
`UnrealEditor.exe HellwalkerRL.uproject -game -windowed -ResX=1600 -ResY=900 -HWWorldMode=hellwalker -HWNoSave "-HWExec=10:hw.Duel 0|14:hw.Kill boss" -HWShotAt=12 -HWShotEvery=4 -HWShots=3 -HWQuitAt=30 -log -unattended`
— output in `Saved/Logs/HellwalkerRL.log`; screenshots in `Saved/Screenshots/WindowsEditor/`; reports in `Saved/HellwalkerRL/`.
Timed console commands: `hw.Duel <i>`, `hw.Kill boss|player`, `hw.Goto`, `hw.GotoBlock`, `hw.Hold <IA_Name> <s>`,
`hw.Where`, `hw.Interact`, `hw.Help`, `hw.Controls`, `hw.Shot`, `hw.ResetModel` (forget the player). Launch UE from Git
Bash or `.bat` (PowerShell splits `-Foo=0.8`); in Git Bash prefix `MSYS_NO_PATHCONV=1` for `/Game/...` map paths.

## Architecture

**Two layers, one rulebook.** `Source/HellwalkerRL/{Public,Private}/HWCore/` is engine-free C++ (namespace `HW::`):
frame data and moves, `FFighter`/`FDuel` (frame-stepped combat, 60 fps), `FEncounter` (duel + an `IBossBrain` + stats),
the brains, and `HWSim` (2-D arena + simulated players incl. procedural habit players). `Sim/ThesisSim.cpp` and
`RL/native` compile **the same files** with plain MSVC and UE-strict warnings-as-errors, so HWCore must not include
Unreal headers. Changing rules/tuning in HWCore means re-running `Tools\Thesis.bat` and the tests.

**Brains** (`HWBrain.h` `IBossBrain`): `FScriptBrain` = Pathbreaker, the script verbatim (the control arm / easy mode);
`FRLBrain` = Hellwalker, the RL keeper: `FRLObserver` (what it may see — the player's state 6 frames late, never inputs;
decision points; fairness masks; history tokens; READ banner) + `FRLPolicy` (the `.hwrl` weights and a plain C++
forward pass: MLP + token embeddings + GRU + policy/value/read heads) + `FRLSession` (its memory of you: recurrent state +
perceived exchanges, per game session, carried keeper to keeper, dropped on quit). The observer is shared verbatim by
the training environment, so the keeper acts identically in training and in the game. `Sim/Classic/` keeps the
reference project's tally brain (`FClassicBrain`, playstyle model, payoff table) **for the tools only** — the benchmark.

**Training** (`RL/`): `RL/native` = `FRLEnvBatch` (thousands of fights on a thread pool, paused at every decision) +
the `hwrl.dll` C ABI; Python = `hwcore.py` (ctypes), `env.py`, `players.py` (population + curriculum), `model.py`
(the same network in PyTorch), `ppo_rnn.py` (recurrent PPO, masks, Lagrangian aggression constraint, aux read head),
`train.py`, `export.py` (`.hwrl` + ONNX), `eval.py` (RL.md §7). Layout changes in `HWRLTypes.h` bump
`ObsLayoutVersion` and require retraining.

**The Unreal layer is presentation + input around that core.** `UHWDuelSubsystem` owns the encounter and the two brains
(the tier picks one; Hellwalker without a model falls back to the script) and steps it on a fixed frame cursor; it
passes geometry with `bMirrorY` (Unreal is left-handed, the simulator is not). `UHWSessionSubsystem` loads the policy
(`Content/HellwalkerRL/RL/hellwalker_rl.hwrl` or `-HWPolicy=`) and holds the session memory. Characters only depict
the simulated state; animation is frame-locked (`UHWAnimInstance`, `HWWarpMoveTime`, casts in `HWAnimCasts.cpp`).

**Game modes and maps.** `AHWOpenWorldGameMode` (default; `L_Hellwalker`) and `AHWGameMode` (arena; `L_Arena`). The
maps contain only a game-mode override — everything is built in C++ at play. The open world (`FHWWorldGen`,
`AHWOpenWorld`, `HWDressing`, `HWSettlement`, bells, shrines, the explorer = the Game Animation Sample's Mover character
dressed as Soul) is unchanged from the reference; see README.

**Content.** No hand-made binary assets: materials are built in code, and every Fab pack is loaded by path through
optional loaders (`HWBuild::Optional*`) with a greybox fallback; packs are local copies and gitignored. Tracked content:
`Content/HellwalkerRL/Maps` (LFS) and the shipped policy `Content/HellwalkerRL/RL/hellwalker_rl.hwrl`.

**Tests**: `Private/HWCore/HWCoreTests.cpp` (combat + control arm), `HWRLTests.cpp` (the RL keeper), both also run by
`ThesisSim --tests` with `Sim/Classic/HWClassicTests.cpp`; `Private/Tests/` (UE automation: core/RL wrappers, the shipped
model, world generation, animation casts); `RL/tests/` (Python: torch/C++ parity, trainer smoke tests).
