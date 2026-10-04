# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

HellwalkerRL is an Unreal Engine 5.8 **C++** project (the owner wants C++, not Blueprint logic): a boss — "the keeper" —
that reads the player's habits and counters them, themed on Phantom Blade Zero's Hellwalker mode, whose adaptive brain
is a **reinforcement-learning policy trained from scratch** (meta-RL, RL.md). It also has a research website that
collects anonymous player data for a paper. It is a new project that used the Hellwalker project
(`D:\Shadow\GAME_NEW_ARAVINDA`) as REFERENCE ONLY — never modify that folder, and never open `D:\Shadow\GAME_CORE 5.8` at
all. The owner may have the reference project's editor open: never close it (see `Tools\Build.bat` below).

Read before changing things: `RL.md` (the RL plan and milestones), `RL/DESIGN.md` (the engineering contract: observation,
masks, tokens, rewards, network format, trainer; §11-13 record the review fixes and model history), `PLAN.md` (the combat
spec; frame data §6), `web/CONTRACT.md` (the telemetry data contract), `README.md` (results, status, deviations).

## Commands (Windows; engine at `D:\Shadow\Epic Games\UE_5.8`)

| | |
|---|---|
| `Tools\Build.bat` | build `HellwalkerRLEditor` Win64 Development. **Close this project's editor/game first** (Live Coding / a running game locks the DLL). Args pass to UBT: `-NoHotReloadFromIDE` builds while another project's editor on the same engine (the reference project) has Live Coding on |
| `Tools\Test.bat` | every automation test, headless (`Project.HellwalkerRL`); prints `Test Completed` lines to `Saved/Logs/HellwalkerRL.log`, exit 0 = all passed |
| `Tools\Thesis.bat` | B0: builds `Sim\out\ThesisSim.exe` (plain MSVC, no Unreal), runs `--tests` (all core tests), then the B0 sweep with the shipped RL keeper (or, without one, the classic reference brain). Args pass through (`--brain classic`, `--sessions 64`, `--identity 0\|1\|2`, `--skill`, `--script 1`, `--keeper-damage`). Judge a model at 64 sessions per identity: at 16 the Warden's net-exchange cell vs RhythmParrier is at the 5% line (noise). `Thesis.bat --arc [--arc-lo --arc-hi --keeper-damage --arc-skills --arc-tune k=v,...]` runs the Adaptive AI insight arc instead (6-fight sessions vs one-trick / switching / near-random players; how `FRLInsight` was calibrated, `RL/DESIGN.md` §14) |
| `Tools\RLBuild.bat` | builds `RL\native\out\hwrl.dll` (training env + C++ forward pass for Python, ctypes) and ThesisSim |
| `Tools\RLTrain.bat --stage rl1\|rl2\|rl3 --run <name>` | train (`RL\train.py`; GPU PyTorch in `RL\.venv`). `--league` adds the RL-4 exploiter rounds; `--resume <ckpt>` restores every setting (use a new `--run` so it writes its own checkpoint folder) |
| `Tools\RLEval.bat [--hwrl <hwrl>]` | RL.md §7 checks + the keepers' styles + the difficulty ladder + the reading test → `RL\reports\` |
| `RL\.venv\Scripts\python.exe RL\exploit.py --boss <keeper.hwrl> --out <x.hwrl>` | train one exploiter (a player network) against a frozen keeper (~6 min) — the exploitability check |
| `RL\.venv\Scripts\python.exe RL\habits.py --hwrl <keeper.hwrl>` | the reading test (C++ attack log, seconds) |
| `Tools\RLShip.bat <hwrl>` | put a trained policy where the game loads it: `Content\HellwalkerRL\RL\hellwalker_rl.hwrl` |
| `Tools\Parity.bat` | the Unreal-vs-simulator gap (`RL\parity.py`): autoplay sessions in the arena (`-HWParity=<jsonl>`, one launch = one session, `-benchmark` fixed steps) vs `hwrl_eval_sessions` on the same bots/keeper → `RL\reports\parity.md`. Builds are locked while it runs |
| `Tools\CI.bat` | what GitHub Actions runs (`.github/workflows/ci.yml`): core tests, env benchmark, torch/C++/ONNX parity, trainer self-tests — no Unreal |
| `Tools\Play.bat` / `Tools\Arena.bat` / `Tools\Demo.bat` | the open world / the duel alone (`-HWBoss=Sevarog\|Wukong\|Golem`, `-HWKeeper=0\|1\|2`, `-HWPolicy=<file.hwrl>`) / watch a simulated player fight |
| `Tools\MakeMaps.bat` | regenerate `Content/HellwalkerRL/Maps/L_Hellwalker` + `L_Arena` via `-run=HWMakeMaps` |
| `Tools\Package.bat` / `Tools\MakeRelease.bat` / `Tools\ItchPush.bat <user> <game>` | the standalone Shipping game into `Build\Packaged` / the GitHub Releases download (parts < 2 GiB + `Join-and-Extract.bat` + checksums in `Build\Packaged\Release`, uploaded with `gh release`; the site's `downloadUrl` points at the latest release) / upload to itch.io with butler. Shipping ignores a map on the command line (always opens `L_Hellwalker`) |

Python: always `RL\.venv\Scripts\python.exe` (torch 2.11 + CUDA 12.8, numpy 2, onnx, onnxruntime, tensorboard). **The C:
drive is nearly full**: set `TMP`/`TEMP` to `D:\Shadow\GAME_NEW_RL\RL\.pip-tmp`, never write large files to C:.

Single UE test: `"D:\Shadow\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" HellwalkerRL.uproject -ExecCmds="Automation RunTests Project.HellwalkerRL.RLModel;Quit" -unattended -nullrhi -nosplash -nosound -nopause`
(groups: `.Core.*`, `.RL.*`, `.RLModel`, `.Menu.*`, `.Settings.*`, `.Keepers.*`, `.Assist.*`, `.Map.*`, `.Telemetry.*`, `.World.*`,
`.Anim.*`, `.RngParity`, `.ConfigRoundTrip`). The engine-free core tests are wrapped there too (e.g.
`Project.HellwalkerRL.Core.A3.ParryWindow`, `Project.HellwalkerRL.RL.RL.Breather`) — the way to run one alone;
`ThesisSim --tests` runs them all. Other tests: `RL\tests\test_parity.py` (torch vs C++ vs ONNX), `web\dev\e2e_test.py
--spawn` (the website contract and security rules against a private mock; without `--spawn` it reuses whatever mock is
already on port 8099, possibly stale code).

Scripted play checks (the only way to "see" the game):
`UnrealEditor.exe HellwalkerRL.uproject -game -windowed -ResX=1600 -ResY=900 -HWWorldMode=adaptive -HWNoSave "-HWExec=10:hw.Duel 0|14:hw.Kill boss" -HWShotAt=12 -HWShotEvery=4 -HWShots=3 -HWQuitAt=30 -log -unattended`
— output in `Saved/Logs/HellwalkerRL.log`; screenshots in `Saved/Screenshots/WindowsEditor/`; reports in `Saved/HellwalkerRL/`.
Open world: timed `-HWExec=<s>:<cmd>|...`; the arena (`/Game/HellwalkerRL/Maps/L_Arena`) takes `-HWExec=<cmd> -HWExecAt=<s>`
plus `-HWTier=`, `-HWAutoplay=<kind> -HWAutoplaySkill=`, `-HWAutoStart -HWEncounters=<n>`. Console: `hw.Duel <i>`,
`hw.Kill boss|player`, `hw.Map [open|close|track <i>]`, `hw.Menu <page>`, `hw.Autoplay`, `hw.Goto`, `hw.Hold <IA_Name> <s>`,
`hw.Where`, `hw.Interact`, `hw.Help`, `hw.Controls`, `hw.Shot`, `hw.ResetModel` (forget the player). Launch UE from Git
Bash or `.bat` (PowerShell splits `-Foo=0.8`); in Git Bash prefix `MSYS_NO_PATHCONV=1` for `/Game/...` map paths.

## Architecture

**Two layers, one rulebook.** `Source/HellwalkerRL/{Public,Private}/HWCore/` is engine-free C++ (namespace `HW::`):
frame data and moves, `FFighter`/`FDuel` (frame-stepped combat, 60 fps), `FEncounter` (duel + an `IBossBrain` + stats),
the brains, and `HWSim` (2-D arena + simulated players incl. procedural and learning habit players). `Sim/ThesisSim.cpp`
and `RL/native` compile **the same files** with plain MSVC and UE-strict warnings-as-errors, so HWCore must not include
Unreal headers. Combat tuning is `FCombatTuning` (`HWTypes.h`, PLAN §6: e.g. the 12-frame parry window and the keeper's
post-parry attack lockout, enforced in `FDuel::CanCommit` so every brain obeys it). **Changing rules changes the RL
environment**: re-run `Tools\Thesis.bat` and the tests, and expect to fine-tune the keeper (README deviation 10).

**Brains** (`HWBrain.h` `IBossBrain`): `FScriptBrain` = Pathbreaker = the **Normal** mode, the script verbatim (the control
arm); `FRLBrain` = Hellwalker = the **Adaptive AI** mode, the RL keeper: `FRLObserver` (what it may see — the player's state 6 frames late at full skill,
up to 16 at skill 0, never inputs; decision points; fairness masks; history tokens; READ banner) + `FRLPolicy` (the
`.hwrl` weights and a plain C++ forward pass: MLP + token embeddings + GRU + policy/value/read heads) + `FRLSession` (its
memory of you: recurrent state + perceived exchanges, per game session, carried keeper to keeper, dropped on quit) +
`FRLNotebook` (what it learned about you, for the notebook screen and telemetry; never an input). Observation layout 3:
one network plays all three keepers (`Configure(Skill, Identity, MinSwingGap)`: 0 Warden, 1 Sage, 2 Returned) at every
difficulty (`RL::SkillParams`; sampling via `SetTemperature`; a breather swing gap, an unobserved mask never set in
training). In the game each fight's skill / temperature / gap come from `FRLInsight` (`HWRLBrain.h`): how well the read head
called the player's answers to its attacks in the session's fights so far, walked through the difficulty's skill range
(`UHWSettingsSubsystem::PresetFor`) — the learning arc (a trick wins early, is countered by fight 4-5); never an input. `IsBossPlayable()` gates every use of a model. The observer is shared verbatim
by the training environment, so the keeper acts identically in training and in the game. `Sim/Classic/` keeps the
reference project's tally brain (`FClassicBrain`) **for the tools only** — the benchmark.

**Training** (`RL/`): `RL/native` = `FRLEnvBatch` (thousands of fights on a thread pool, paused at every decision; each
session also fixes the keeper's skill / identity) + `FRLPlayerEnvBatch` (RL-4: the exploiter's env, the keeper frozen) +
`HWRLPlayer` (the exploiter's senses) + the `hwrl.dll` C ABI (v2; newer calls are optional exports so older DLLs still
load); Python = `hwcore.py` (ctypes), `env.py`, `players.py` (population + curriculum: reference bots, habit players —
35% of them learn against the keeper — and registered exploiters), `model.py` (the same network in PyTorch), `ppo_rnn.py`
(recurrent PPO, masks, Lagrangian aggression constraint, aux read head), `train.py` (stages, the reading test every 50
iterations, the league), `exploit.py`, `export.py` (`.hwrl` + ONNX), `eval.py`, `habits.py`, `parity.py`. Layout changes
in `HWRLTypes.h` bump `ObsLayoutVersion` and require retraining. A training run holds `RL\native\out\hwrl.dll`: give it a
copy via `HWRL_DLL`, or build elsewhere with `HWRL_OUT` (Tools\CI.bat does). A resumed run's league skips round indices
that already have exploiters, so to add rounds pass `--league-at` with extra entries. Shipping a model = scan the run's
checkpoints (B0 at 64 sessions per identity, `RLEval`, a fresh `exploit.py`) and `RLShip` the one that passes — later
checkpoints are not automatically better (see `RL/DESIGN.md` §13 for how the shipped `keeper_1p45e9` was chosen).

**The Unreal layer is presentation + input around that core.** `UHWDuelSubsystem` owns the encounter and the two brains
(the tier picks one; Hellwalker without a model falls back to the script) and steps it on a fixed frame cursor; it
passes geometry with `bMirrorY` (Unreal is left-handed, the simulator is not). `UHWSessionSubsystem` loads the policy
(`Content/HellwalkerRL/RL/hellwalker_rl.hwrl` or `-HWPolicy=`) and holds the session memory, notebook and insight.
The difficulty also scales the keeper's health damage (`HW::FDuel::KeeperDamageScale`, 1 in training) and sets the
**parry assist**: `HWParryAssist::Evaluate` (pure, exact to the frame, tested against a real `FDuel`) lights a red ring at
the boss's hand sockets while a parry pressed now would land — keyed to the impact the wind-up shows, so feints still
bait — and the duel slows only itself (frame cursor x scale + `CustomTimeDilation` on the two fighters, never global time
dilation). Autoplay / parity / `-benchmark` get no assist and damage 1; parity also forces skill 1. Characters only depict the simulated state; animation is frame-locked (`UHWAnimInstance`, `HWWarpMoveTime`,
casts in `HWAnimCasts.cpp`). **Duel movement must match the simulator** (instant acceleration/braking —
`AHWCharacterBase::DuelMoveAccel` — and the shared `FSimArena::ApproachStopDistance`): ordinary character physics made
the keeper's trained spacing go soft (`Tools\Parity.bat`, `RL/DESIGN.md` §12). The HUD is Canvas-drawn (`AHWHUD`):
menus come from a pure `FHWMenu` model (`HWMenu`, `HWPlayerControllerMenu.cpp`), settings and rebinding from
`UHWSettingsSubsystem` (difficulty presets, parry assist), the M-key valley map and keeper tracking
from `HWMap` (pure, tested helpers), sound from `UHWAudioSubsystem` (cues loaded by path).

**Game modes and maps.** `AHWOpenWorldGameMode` (default; `L_Hellwalker`) and `AHWGameMode` (arena; `L_Arena`). The
maps contain only a game-mode override — everything is built in C++ at play. The open world (`FHWWorldGen`,
`AHWOpenWorld`, `HWDressing`, `HWSettlement`, bells, shrines, the explorer = the Game Animation Sample's Mover character
dressed as Soul) is unchanged from the reference; see README. Play modes: Normal (scripted keepers at every shrine) and
Adaptive AI (`-HWWorldMode=normal|adaptive`); 66 Days and the Adaptive difficulty were removed (enum values kept for old
saves, which sanitise).

**Website and research telemetry.** `web/` is a static, no-build site (plain ES modules) deployed on Render
(`render.yaml`); data lives in Firebase, reached by **plain REST** from both the game and the site (no SDK), so the
stdlib mock `web/dev/mock_firebase.py` stands in for Firebase in tests. `web/CONTRACT.md` is the contract and
`web/firebase/firestore.rules` the security (fight schema v2 = v1 + assist / slowmoScale / keeperDamageScale /
parryWindowFrames / insight; v1 stays accepted; a rules change must be published by the owner in the Firebase console;
**real Firestore evaluates at most 1,000 rule expressions per request and the mock does not model it** — per-field rules
that pass every mock check were refused for every player, so rules check shapes, ownership, server time and a few growth
caps only, and must be verified against the real project after publishing) — change the contract first, then the game, the rules, the mock (it mirrors
the rules' semantics) and the site together. The game side is `UHWTelemetrySubsystem` (anonymous sign-in, one Firestore
commit per duel, offline queue): silent until `Config/DefaultGame.ini [HWTelemetry]` has ApiKey/ProjectId. Research
integrity: autoplay, parity, tool-touched (`hw.Kill`, `hw.InjectParry`, `hw.Hold`) and scripted (`-unattended`,
`-HWExec`) fights are never uploaded; tests use `-HWTelemetryEndpoint=http://127.0.0.1:8099 -HWTelemetryApiKey=x
-HWTelemetryProjectId=y -HWTelemetryAllowAutoplay` (a mock endpoint, its own save slot).

**Content.** No hand-made binary assets: materials are built in code, and every Fab pack is loaded by path through
optional loaders (`HWBuild::Optional*`) with a greybox fallback; packs are local copies and gitignored, so anything
loaded only by path must be listed for cooking in `Config/DefaultGame.ini` (`DirectoriesToAlwaysCook`, or the
asset-manager rule used for the keepers' voice lines). Tracked content: `Content/HellwalkerRL/Maps` (LFS) and the shipped
policy `Content/HellwalkerRL/RL/hellwalker_rl.hwrl`.

**Tests**: `Private/HWCore/HWCoreTests.cpp` (combat + control arm), `HWRLTests.cpp` (the RL keeper), both also run by
`ThesisSim --tests` with `Sim/Classic/HWClassicTests.cpp`; `Private/Tests/` (UE automation: core/RL wrappers, the shipped
model, menus and settings, keepers and difficulty, map, telemetry, world generation, animation casts); `RL/tests/`
(Python: torch/C++ parity, trainer smoke tests); `web/dev/e2e_test.py` (the website contract and rules).
