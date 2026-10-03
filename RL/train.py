"""HellwalkerRL — train the keeper (RL.md §5, DESIGN.md §9).

    python RL/train.py --stage rl1                       feed-forward PPO vs one Habitual bot (skill 0.8), 1-fight
                                                          sessions: the sanity check (it learns THAT bot's counter)
    python RL/train.py --stage rl2                       recurrent PPO vs the curriculum population: the reader
    python RL/train.py --stage rl3 --init <rl2 ckpt>     rl2 continued, with evaluation gates logged as it trains
    python RL/train.py --resume RL/checkpoints/<run>/latest.pt    continue a run exactly where it stopped (every saved
                                                          setting is restored unless given again; a new --steps
                                                          continues the lr / entropy decay from where it was)
    python RL/train.py --stage rl2 --league ...          RL-4: at --league-at fractions of the run, freeze the keeper,
                                                          train --league-exploiters exploiters against it (exploit.py)
                                                          and add them to the population

Common: --envs N --steps TOTAL_DECISIONS --run NAME --device cuda|cpu --seed S [--backend auto|dll|mock]
Smoke test without the DLL: --backend mock --envs 64 --num-steps 64 --max-iterations 2 --small

Writes RL/runs/<run>/ (metrics.csv, TensorBoard events if tensorboard imports, config.json) and
RL/checkpoints/<run>/ (ckpt_<iter>.pt + latest.pt, and the exported policy_<iter>.hwrl + latest.hwrl via export.py).
Ctrl+C stops at the end of the current iteration and writes a final checkpoint (a second Ctrl+C aborts).
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import signal
import sys
import time
from pathlib import Path
from typing import Dict, Optional

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
	sys.path.insert(0, str(HERE))

import numpy as np  # noqa: E402
import torch  # noqa: E402

import env as env_mod  # noqa: E402
import hwcore  # noqa: E402
import players as players_mod  # noqa: E402
from model import HellwalkerNet, load_checkpoint, save_checkpoint  # noqa: E402
from ppo_rnn import PPOConfig, PPOTrainer  # noqa: E402

RUNS_DIR = HERE / "runs"
CKPT_DIR = HERE / "checkpoints"

# Per-stage defaults (DESIGN.md §9 "Stages"). Anything given on the command line overrides them.
STAGES: Dict[str, dict] = {
	"rl1": {"recurrent": False, "players": "habitual", "fights": 1, "steps": 20_000_000, "envs": 1024, "gate_every": 0, "read_every": 0},
	"rl2": {"recurrent": True, "players": "population", "fights": None, "steps": 1_000_000_000, "envs": 4096, "gate_every": 100,
		"read_every": 50},
	"rl3": {"recurrent": True, "players": "population", "fights": None, "steps": 100_000_000, "envs": 4096, "gate_every": 50,
		"read_every": 50},
}

# Every setting a resumed run restores from its checkpoint unless it is given again on the command line (review
# finding: --resume used to fall back to the CLI defaults silently). name -> default for a fresh run.
RESUMABLE = {
	"target_spm": 66.0, "max_fight_seconds": 180, "seed": 1, "num_steps": 128, "bptt": 32, "epochs": 4, "minibatches": 8,
	"lr": 3e-4, "lr_final": 3e-5, "ent_coef": 0.01, "ent_coef_final": 0.001, "lambda_lr": 0.01, "no_cost": False,
	"tf32": False, "style_scale": 1.0, "league": False, "league_at": "0.3,0.5,0.7,0.85", "league_exploiters": 2,
	"league_decisions": 4e7, "league_envs": 2048, "read_every": None, "gate_every": None, "keeper_full_skill_p": 0.5,
	"exploit_fraction": 0.12, "ref_fraction": 0.2,
}

CSV_BASE = ["iteration", "decisions", "elapsed_s", "decisions_per_s", "rollout_decisions_per_s", "update_samples_per_s",
	"fights", "reward_per_fight", "keeper_win_rate", "keeper_loss_rate", "timeout_rate", "fight_seconds",
	"swings_per_min", "dmg_per_min", "taken_per_min", "reward_per_decision", "cost_per_decision", "lambda",
	"entropy", "pg_loss", "v_loss", "aux_loss", "aux_acc", "approx_kl", "clipfrac", "grad_norm", "explained_variance",
	"ratio_dev_first_mb", "lr", "ent_coef", "return_std", "reads_per_min", "label_rate", "sessions",
	"curriculum_unlocked", "curriculum_newest_win_rate", "curriculum_newest_window"]
CSV_GROUPS = [f"win_{g}" for g in players_mod.GROUPS_TRAIN + ("fixed",)] + \
	[f"reward_{g}" for g in players_mod.GROUPS_TRAIN + ("fixed",)] + \
	[f"spm_{g}" for g in players_mod.GROUPS_TRAIN + ("fixed",)]
CSV_GATES = ["gate_dmg_ratio_low", "gate_dmg_ratio_high", "gate_spm_ratio_low", "gate_hit_rl_low", "gate_hit_script_low"]
# The reading test, logged during training (habits.flat_metrics) — RL.md's evidence, watched as it develops.
CSV_READ = ["read_js_early", "read_js_late", "read_switch_pre", "read_switch_post", "read_switch_recovered"] + \
	[f"read_{g}_{m}" for g in ("parrier", "blocker", "stepleft", "stepright") for m in ("hit_early", "hit_late", "counter_late")]
# The three keepers' personalities (env.py pop_stats "keepers").
KEEPER_STATS = ("swings_per_min", "reads_per_min", "style_reward_per_min", "feint_share", "heavy_share", "evade_share",
	"feint_bites_per_min", "evasions_per_min", "guard_breaks_per_min", "pressure_blocks_per_min")
CSV_KEEPERS = [f"keeper_{k}_{m}" for k in ("Warden", "Sage", "Returned") for m in KEEPER_STATS]
CSV_LEAGUE = ["league_round", "league_exploiters", "league_player_win_rate", "league_exchange", "league_baseline_exchange",
	"skipped_updates"]
CSV_FIELDS = CSV_BASE + CSV_GROUPS + CSV_GATES + CSV_READ + CSV_KEEPERS + CSV_LEAGUE


def parse_args(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--stage", choices=sorted(STAGES), default=None, help="rl1 | rl2 | rl3 (default rl2; a resumed run keeps its own)")
	ap.add_argument("--envs", type=int, default=None)
	ap.add_argument("--steps", type=float, default=None, help="total decisions (e.g. 1e8)")
	ap.add_argument("--run", default=None, help="run name (default <stage>-<timestamp>)")
	ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
	ap.add_argument("--seed", type=int, default=None)
	ap.add_argument("--resume", default=None, help="checkpoint to continue (model + optimizer + curriculum + counters)")
	ap.add_argument("--init", default=None, help="checkpoint to start from (weights only; e.g. rl3 from rl2)")
	ap.add_argument("--backend", choices=("auto", "dll", "mock"), default="auto")
	ap.add_argument("--threads", type=int, default=0, help="C++ env worker threads (0 = hardware concurrency)")
	ap.add_argument("--players", default=None, help=f"player set override ({', '.join(players_mod.PLAYER_SETS)})")
	ap.add_argument("--target-spm", type=float, default=None, help="aggression target at skill 1 (FRLConfig default 66)")
	ap.add_argument("--max-fight-seconds", type=int, default=None)
	ap.add_argument("--style-scale", type=float, default=None, help="per-identity style rewards x this (0 = off; default 1)")
	ap.add_argument("--keeper-full-skill-p", type=float, default=None, help="sessions with the keeper at skill 1 (default 0.5)")
	# PPO (defaults in RESUMABLE)
	ap.add_argument("--num-steps", type=int, default=None)
	ap.add_argument("--bptt", type=int, default=None)
	ap.add_argument("--epochs", type=int, default=None)
	ap.add_argument("--minibatches", type=int, default=None)
	ap.add_argument("--lr", type=float, default=None)
	ap.add_argument("--lr-final", type=float, default=None)
	ap.add_argument("--ent-coef", type=float, default=None)
	ap.add_argument("--ent-coef-final", type=float, default=None)
	ap.add_argument("--lambda-lr", type=float, default=None)
	ap.add_argument("--lambda-floor", type=float, default=0.0,
		help="raise the aggression multiplier to at least this at the start (a fine-tune that must restore the floor)")
	ap.add_argument("--no-cost", action="store_true", default=None, help="disable the aggression constraint")
	ap.add_argument("--tf32", action="store_true", default=None, help="TF32 matmuls (faster; rollout/update ratio no longer exactly 1)")
	# RL-4 league
	ap.add_argument("--league", action="store_true", default=None, help="train exploiters at --league-at and add them")
	ap.add_argument("--league-at", default=None, help="fractions of the run where a league round runs (default 0.3,0.5,0.7,0.85)")
	ap.add_argument("--league-exploiters", type=int, default=None, help="exploiters per round (default 2)")
	ap.add_argument("--league-decisions", type=float, default=None, help="player decisions per exploiter (default 4e7)")
	ap.add_argument("--league-envs", type=int, default=None)
	ap.add_argument("--exploit-fraction", type=float, default=None, help="population share of exploiters once present (default 0.12)")
	ap.add_argument("--ref-fraction", type=float, default=None, help="population share of the reference bots (default 0.2; B0 grades against them)")
	ap.add_argument("--read-every", type=int, default=None, help="iterations between in-training reading tests (0 = off)")
	# network
	ap.add_argument("--small", action="store_true", help="a small network (smoke tests)")
	ap.add_argument("--hidden", type=int, default=256)
	ap.add_argument("--enc-hidden", type=int, default=256)
	ap.add_argument("--embed-dim", type=int, default=32)
	# bookkeeping
	ap.add_argument("--save-every", type=int, default=25, help="iterations between checkpoints + .hwrl exports")
	ap.add_argument("--log-every", type=int, default=1)
	ap.add_argument("--gate-every", type=int, default=None, help="iterations between B0-lite gates vs the script (0 = off)")
	ap.add_argument("--max-iterations", type=int, default=None, help="stop after this many iterations (smoke tests)")
	ap.add_argument("--no-tensorboard", action="store_true")
	ap.add_argument("--quiet", action="store_true")
	return ap.parse_args(argv)


# =====================================================================================================================
# Logging
# =====================================================================================================================

class Logger:
	def __init__(self, run_dir: Path, use_tb: bool, resume: bool):
		self.run_dir = run_dir
		run_dir.mkdir(parents=True, exist_ok=True)
		self.csv_path = run_dir / "metrics.csv"
		new = not (resume and self.csv_path.exists())
		self.fh = open(self.csv_path, "w" if new else "a", newline="")
		self.csv = csv.DictWriter(self.fh, fieldnames=CSV_FIELDS, extrasaction="ignore", restval="")
		if new:
			self.csv.writeheader()
		self.tb = None
		if use_tb:
			try:
				from torch.utils.tensorboard import SummaryWriter
				self.tb = SummaryWriter(str(run_dir))
			except Exception as e:  # tensorboard missing or broken: CSV only
				print(f"[train] TensorBoard unavailable ({type(e).__name__}: {e}); logging to CSV only")

	def log(self, row: Dict[str, float], step: int) -> None:
		self.csv.writerow({k: (f"{v:.6g}" if isinstance(v, float) else v) for k, v in row.items()})
		self.fh.flush()
		if self.tb is not None:
			for k, v in row.items():
				if isinstance(v, (int, float)) and not (isinstance(v, float) and math.isnan(v)):
					self.tb.add_scalar(_tb_name(k), v, step)

	def close(self) -> None:
		self.fh.close()
		if self.tb is not None:
			self.tb.close()


def _tb_name(k: str) -> str:
	for prefix, group in (("win_", "win_rate/"), ("reward_", "reward_per_fight/"), ("spm_", "swings_per_min/"),
			("gate_", "gate/"), ("curriculum_", "curriculum/")):
		if k.startswith(prefix) and k not in ("reward_per_fight", "reward_per_decision"):
			return group + k[len(prefix):]
	if k.startswith("read_"):
		return "reading/" + k[5:]
	if k.startswith("keeper_"):
		return "keepers/" + k[7:]
	if k.startswith("league_"):
		return "league/" + k[7:]
	if k in ("pg_loss", "v_loss", "aux_loss", "loss", "entropy", "approx_kl", "clipfrac", "grad_norm", "aux_acc",
			"explained_variance", "ratio_dev_first_mb", "lr", "ent_coef", "return_std"):
		return "train/" + k
	if k in ("decisions_per_s", "rollout_decisions_per_s", "update_samples_per_s", "elapsed_s"):
		return "perf/" + k
	return "env/" + k


def build_row(it: Dict[str, float], env_stats: dict, players, elapsed: float) -> Dict[str, float]:
	row = _build_row(it, env_stats, players, elapsed)
	for k, d in env_stats.get("keepers", {}).items():
		for m in KEEPER_STATS:
			row[f"keeper_{k}_{m}"] = d.get(m, float("nan"))
	row["skipped_updates"] = it.get("skipped_updates", 0)
	return row


def _build_row(it: Dict[str, float], env_stats: dict, players, elapsed: float) -> Dict[str, float]:
	groups = env_stats["groups"]
	allg = groups.get("all", {})
	nan = float("nan")
	row = {k: it.get(k, nan) for k in ("iteration", "decisions", "decisions_per_s", "rollout_decisions_per_s",
		"update_samples_per_s", "reward_per_decision", "cost_per_decision", "lambda", "entropy", "pg_loss", "v_loss",
		"aux_loss", "aux_acc", "approx_kl", "clipfrac", "grad_norm", "explained_variance", "ratio_dev_first_mb", "lr",
		"ent_coef", "return_std")}
	row["elapsed_s"] = elapsed
	row["fights"] = allg.get("fights", 0.0)
	row["reward_per_fight"] = allg.get("reward_per_fight", nan)
	row["keeper_win_rate"] = allg.get("win_rate", nan)
	row["keeper_loss_rate"] = allg.get("loss_rate", nan)
	row["timeout_rate"] = allg.get("timeout_rate", nan)
	row["fight_seconds"] = allg.get("fight_seconds", nan)
	row["dmg_per_min"] = allg.get("dmg_per_min", nan)
	row["taken_per_min"] = allg.get("taken_per_min", nan)
	row["swings_per_min"] = env_stats["swings_per_min"]
	row["reads_per_min"] = env_stats["reads_per_min"]
	row["label_rate"] = env_stats["label_rate"]
	row["sessions"] = env_stats["sessions"]
	for g in players_mod.GROUPS_TRAIN + ("fixed",):
		s = groups.get(g, {})
		row[f"win_{g}"] = s.get("win_rate", nan) if s.get("fights", 0) else nan
		row[f"reward_{g}"] = s.get("reward_per_fight", nan) if s.get("fights", 0) else nan
		row[f"spm_{g}"] = s.get("swings_per_min", nan) if s.get("fights", 0) else nan
	desc = players.describe()
	row["curriculum_unlocked"] = desc.get("unlocked", nan)
	row["curriculum_newest_win_rate"] = desc.get("newest_win_rate", nan)
	row["curriculum_newest_window"] = desc.get("newest_window", nan)
	return row


# =====================================================================================================================
# Checkpoints, export, gates
# =====================================================================================================================

def export_policy(model, path: Path) -> Optional[str]:
	"""Write a .hwrl via export.py (another part of the trainer); skipped with a note if it is missing or fails."""
	try:
		from export import export_hwrl
	except Exception as e:
		print(f"[train] export.py unavailable ({type(e).__name__}: {e}); no .hwrl written")
		return None
	try:
		net = model
		if next(model.parameters()).device.type != "cpu":
			net = HellwalkerNet(**model.config)
			net.load_state_dict({k: v.detach().cpu() for k, v in model.state_dict().items()})
		export_hwrl(net, str(path), obs_layout_version=hwcore.OBS_LAYOUT_VERSION)
		return str(path)
	except Exception as e:
		print(f"[train] .hwrl export failed ({type(e).__name__}: {e})")
		return None


def model_is_finite(trainer: PPOTrainer) -> bool:
	if not all(bool(torch.isfinite(p).all()) for p in trainer.model.parameters()):
		return False
	return math.isfinite(trainer.normalizer.std()) and math.isfinite(trainer.lam)


def save_all(ckpt_dir: Path, trainer: PPOTrainer, players, args, stage_cfg: dict, tag: str) -> Path:
	ckpt_dir.mkdir(parents=True, exist_ok=True)
	if not model_is_finite(trainer):
		# Review finding: a NaN model used to overwrite latest.pt / latest.hwrl (and a resume would continue from NaN).
		path = ckpt_dir / f"ckpt_nan_{tag}.pt"
		save_checkpoint(str(path), trainer.model, {"stage": args.stage, "run": args.run, "iteration": trainer.iteration, "nonfinite": True})
		print(f"[train] the model is not finite: wrote only {path.name}; latest.* untouched")
		return path
	extra = {
		"stage": args.stage, "run": args.run, "args": {k: v for k, v in vars(args).items()}, "stage_cfg": stage_cfg,
		"trainer": trainer.state_dict(), "players": players.state_dict(), "obs_layout_version": hwcore.OBS_LAYOUT_VERSION,
		"iteration": trainer.iteration, "decisions": trainer.decisions, "saved_at": time.time(),
	}
	path = ckpt_dir / f"ckpt_{tag}.pt"
	save_checkpoint(str(path), trainer.model, extra)
	save_checkpoint(str(ckpt_dir / "latest.pt"), trainer.model, extra)
	hw = export_policy(trainer.model, ckpt_dir / f"policy_{tag}.hwrl")
	if hw is not None:
		export_policy(trainer.model, ckpt_dir / "latest.hwrl")
	return path


def run_gate(trainer: PPOTrainer, ckpt_dir: Path, seed: int) -> Dict[str, float]:
	"""rl3's evaluation gate: the exported policy vs the script in C++ on a few held-out players (strong habits: reading
	must beat the script; near random: it must not bully). Quick and deterministic; skipped without the DLL."""
	if not hwcore.available():
		return {}
	path = export_policy(trainer.model, ckpt_dir / "gate.hwrl")
	if path is None:
		return {}
	sets = players_mod.eval_sets(seed=seed + 7, n=4, fights=2)
	out: Dict[str, float] = {}
	with hwcore.Policy(path) as pol:
		res = {}
		for name in ("habit_low", "habit_high"):
			for arm in (hwcore.ARM_SCRIPT, hwcore.ARM_RL):
				runs = [hwcore.eval_sessions(arm, s, 2, 1000 + i, policy=pol if arm == hwcore.ARM_RL else None,
					immortal=True, fight_seconds=60) for i, s in enumerate(sets[name])]
				res[(name, arm)] = hwcore.sum_eval_stats(runs)
	for name, key in (("habit_low", "low"), ("habit_high", "high")):
		s, r = res[(name, hwcore.ARM_SCRIPT)], res[(name, hwcore.ARM_RL)]
		out[f"gate_dmg_ratio_{key}"] = r["dmg_per_min"] / max(s["dmg_per_min"], 1e-6)
	s, r = res[("habit_low", hwcore.ARM_SCRIPT)], res[("habit_low", hwcore.ARM_RL)]
	out["gate_spm_ratio_low"] = r["swings_per_min"] / max(s["swings_per_min"], 1e-6)
	out["gate_hit_rl_low"] = r["hit_rate"]
	out["gate_hit_script_low"] = s["hit_rate"]
	return out


def run_reading(trainer: PPOTrainer, ckpt_dir: Path, sessions: int = 128) -> Dict[str, float]:
	"""The reading test on the current weights (C++, a few seconds): logged so RL.md's evidence is watched as it grows."""
	if not hwcore.available():
		return {}
	path = export_policy(trainer.model, ckpt_dir / "read_gate.hwrl")
	if path is None:
		return {}
	import habits
	with hwcore.Policy(path) as pol:
		return habits.flat_metrics(habits.reading_test(pol, sessions=sessions))


def league_round(trainer: PPOTrainer, env, players, ckpt_dir: Path, args, round_index: int, kept: list) -> Dict[str, float]:
	"""RL-4: freeze the keeper, train exploiters against it, register them in the env batch, add them to the population."""
	import exploit
	boss = export_policy(trainer.model, ckpt_dir / f"league_r{round_index}_keeper.hwrl")
	if boss is None:
		return {}
	base = exploit.baseline_exchange(boss)
	print(f"[league] round {round_index}: the frozen keeper vs the reference bots (skill 0.9): exchange {base['exchange']:.3f}", flush=True)
	wins, exch = [], []
	for k in range(int(args.league_exploiters)):
		out = ckpt_dir / f"exploiter_r{round_index}_{k}.hwrl"
		r = exploit.train_exploiter(boss, str(out), args.league_decisions, args.league_envs, seed=1000 * round_index + 17 * k + args.seed,
			device=str(trainer.device), threads=args.threads)
		pol = hwcore.Policy(str(out))
		pid = env.batch.add_player_policy(pol)
		kept.append(pol)
		# Later rounds (stronger keepers' holes) weigh more; a stronger exploiter weighs more.
		players.add_exploiter(pid, str(out), round_index, weight=(1.0 + round_index) * (1.0 + min(r["exchange"], 2.0)))
		wins.append(r["player_win_rate"])
		exch.append(r["exchange"])
		print(f"[league] exploiter {out.name}: player wins {r['player_win_rate']:.2f}, exchange {r['exchange']:.3f} "
			f"(baseline {base['exchange']:.3f}) -> policy {pid}", flush=True)
	return {"league_round": round_index, "league_exploiters": len(players.exploiters),
		"league_player_win_rate": float(np.mean(wins)) if wins else float("nan"),
		"league_exchange": float(np.mean(exch)) if exch else float("nan"), "league_baseline_exchange": base["exchange"]}


# =====================================================================================================================
# Main
# =====================================================================================================================

def main(argv=None) -> int:
	args = parse_args(argv)
	resume_extra = None
	resume_model = None
	saved: dict = {}
	if args.resume:
		resume_model, resume_extra = load_checkpoint(args.resume)
		saved = resume_extra.get("args", {})
		# A resumed run keeps its identity (stage, run name, sizes) and every setting unless explicitly overridden.
		args.stage = args.stage or saved.get("stage")
		args.run = args.run or saved.get("run")
		if args.envs is None:
			args.envs = saved.get("envs")
		if args.steps is None:
			args.steps = saved.get("steps")
		if args.players is None:
			args.players = saved.get("players")
	args.stage = args.stage or "rl2"
	for k, default in RESUMABLE.items():
		if getattr(args, k, None) is None:
			setattr(args, k, saved.get(k, default) if k in saved else default)
	stage = dict(STAGES[args.stage])
	if args.stage == "rl3" and not (args.init or args.resume):
		print("[train] rl3 continues rl2: pass --init <rl2 checkpoint> (or --resume an rl3 run)")
		return 2
	args.envs = int(args.envs or stage["envs"])
	args.steps = float(args.steps or stage["steps"])
	args.players = args.players or stage["players"]
	gate_every = stage["gate_every"] if args.gate_every is None else args.gate_every
	read_every = stage.get("read_every", 0) if args.read_every is None else args.read_every
	args.run = args.run or f"{args.stage}-{time.strftime('%Y%m%d-%H%M%S')}"
	run_dir, ckpt_dir = RUNS_DIR / args.run, CKPT_DIR / args.run

	torch.manual_seed(args.seed)
	np.random.seed(args.seed & 0xFFFFFFFF)
	device = torch.device(args.device)
	if device.type == "cuda" and not torch.cuda.is_available():
		print("[train] CUDA not available: using the CPU")
		device = torch.device("cpu")

	# Players.
	fights = stage["fights"]
	if args.players == "habitual":
		players = players_mod.make_player_source("habitual", args.seed, fights=fights or 1)
	else:
		players = players_mod.make_player_source(args.players, args.seed)
	if resume_extra and "players" in resume_extra:
		try:
			players.load_state_dict(resume_extra["players"])
		except Exception as e:
			print(f"[train] could not restore the player source state ({e}); starting it fresh")
	if isinstance(players, players_mod.Population):
		players.exploit_fraction = float(args.exploit_fraction)
		players.ref_fraction = float(args.ref_fraction)
		players.vary_keeper = bool(stage["recurrent"])
		players.full_skill_p = float(args.keeper_full_skill_p)
	else:
		# rl1 (one fixed bot): the keeper at full strength as the Warden — the sanity check stays the sanity check.
		pass

	# Environment.
	cfg_env = hwcore.env_config(max_fight_seconds=args.max_fight_seconds, target_spm=args.target_spm,
		num_threads=args.threads, style_scale=args.style_scale)
	env = env_mod.make_env(args.envs, players, seed=args.seed, backend=args.backend, config=cfg_env,
		pin_memory=(device.type == "cuda"))

	# Model.
	init_extra = None
	if resume_model is not None:
		model = resume_model
	elif args.init:
		init_model, init_extra = load_checkpoint(args.init)
		model = init_model
		if model.recurrent != stage["recurrent"]:
			print(f"[train] note: --init network recurrent={model.recurrent}, stage wants {stage['recurrent']}; keeping the checkpoint's")
	else:
		sizes = dict(enc_hidden=64, embed_dim=16, hidden=64) if args.small else \
			dict(enc_hidden=args.enc_hidden, embed_dim=args.embed_dim, hidden=args.hidden)
		model = HellwalkerNet(obs_dim=hwcore.OBS_DIM, num_actions=hwcore.NUM_ACTIONS, aux_classes=hwcore.AUX_CLASSES,
			token_vocab=hwcore.TOKEN_VOCAB, history_tokens=hwcore.HISTORY_TOKENS, recurrent=stage["recurrent"], side=0,
			**sizes)

	# Fields without a CLI flag (cost_budget, gamma, ...) come from the resumed run's own config.
	base_cfg = dict(resume_extra["trainer"].get("ppo_config", {})) if resume_extra and "trainer" in resume_extra else {}
	base_cfg = {k: v for k, v in base_cfg.items() if k in PPOConfig.__dataclass_fields__}
	base_cfg.update(num_steps=args.num_steps, bptt=args.bptt, epochs=args.epochs, minibatches=args.minibatches,
		lr=args.lr, lr_final=args.lr_final, ent_coef=args.ent_coef, ent_coef_final=args.ent_coef_final,
		lambda_lr=args.lambda_lr, use_cost=not args.no_cost, tf32=args.tf32)
	ppo_cfg = PPOConfig(**base_cfg)
	per_iter = args.envs * args.num_steps
	total_iters = max(1, int(math.ceil(args.steps / per_iter)))
	trainer = PPOTrainer(model, env, ppo_cfg, device=device, total_iterations=total_iters, seed=args.seed)
	if resume_extra and "trainer" in resume_extra:
		trainer.load_state_dict(resume_extra["trainer"])
		if total_iters != trainer.total_iterations:
			print(f"[train] the run's length changes on resume: {trainer.total_iterations} -> {total_iters} iterations; the lr / "
				f"entropy decay continues from where it was")
		trainer.set_total_iterations(total_iters)
		if trainer.iteration >= total_iters:
			print(f"[train] warning: the run is already at iteration {trainer.iteration} of {total_iters}")
	if args.lambda_floor and trainer.lam < args.lambda_floor:
		print(f"[train] aggression multiplier {trainer.lam:.3f} -> {args.lambda_floor:.3f} (--lambda-floor)")
		trainer.lam = float(args.lambda_floor)
	elif args.init and init_extra and "trainer" in init_extra:
		# rl3 continues rl2: keep its Lagrange multiplier and its return scale (review finding: they were dropped, so
		# the 'constraints hold' stage started with no aggression penalty and a mis-scaled value target).
		ts = init_extra["trainer"]
		trainer.lam = float(ts.get("lambda", trainer.lam))
		if "normalizer" in ts:
			trainer.normalizer.rms.load_state_dict(ts["normalizer"]["rms"])
		print(f"[train] --init: lambda {trainer.lam:.3f}, return std {trainer.normalizer.std():.3f} from {args.init}")

	# RL-4: exploiters registered by earlier league rounds (resume) go back into this batch, in their original order.
	league_kept: list = []
	if isinstance(players, players_mod.Population) and players.exploiters and env.backend == "dll":
		for e in players.exploiters:
			pol = hwcore.Policy(e["path"])
			e["id"] = env.batch.add_player_policy(pol)
			league_kept.append(pol)
		print(f"[train] re-registered {len(players.exploiters)} exploiters from the checkpoint")
	league_at = sorted({float(x) for x in str(args.league_at).split(",") if x.strip()}) if args.league else []
	league_done = {int(e["round"]) for e in getattr(players, "exploiters", [])}

	# Run folder.
	run_dir.mkdir(parents=True, exist_ok=True)
	cfg_name = "config.json" if not args.resume else f"config_resume_{trainer.iteration:06d}.json"
	with open(run_dir / cfg_name, "w") as fh:
		json.dump({"args": vars(args), "stage": stage, "ppo": ppo_cfg.__dict__, "model": model.config,
			"backend": env.backend, "device": str(device), "total_iterations": total_iters}, fh, indent=2, default=str)
	logger = Logger(run_dir, use_tb=not args.no_tensorboard, resume=bool(args.resume))
	n_params = sum(p.numel() for p in model.parameters())
	print(f"[train] {args.stage} run={args.run} backend={env.backend} device={device} envs={args.envs} "
		f"T={args.num_steps} iters={total_iters} ({args.steps:.3g} decisions) players={args.players} "
		f"recurrent={model.recurrent} params={n_params}")

	# Ctrl+C: finish the iteration, save, exit. A second Ctrl+C aborts.
	stop = {"flag": False}

	def on_sigint(signum, frame):
		if stop["flag"]:
			raise KeyboardInterrupt
		stop["flag"] = True
		print("\n[train] stopping after this iteration (Ctrl+C again to abort)")

	old_handler = signal.signal(signal.SIGINT, on_sigint)
	t_start = time.time()
	iters_this_run = 0
	last_save_iter = trainer.iteration
	exit_code = 0
	try:
		while trainer.iteration < total_iters and not stop["flag"]:
			if args.max_iterations is not None and iters_this_run >= args.max_iterations:
				break
			it = trainer.iteration_step()
			iters_this_run += 1
			if not all(math.isfinite(it[k]) for k in ("loss", "pg_loss", "v_loss", "entropy")):
				print(f"[train] non-finite loss at iteration {trainer.iteration}: {it}")
				exit_code = 3
				break
			env_stats = env.pop_stats()
			if gate_every and trainer.iteration % gate_every == 0:
				try:
					it.update(run_gate(trainer, ckpt_dir, args.seed))
				except Exception as e:
					print(f"[train] gate failed: {type(e).__name__}: {e}")
			if read_every and trainer.iteration % read_every == 0:
				try:
					rd = run_reading(trainer, ckpt_dir)
					it.update(rd)
					if rd and not args.quiet:
						print(f"[read] JS {rd['read_js_early']:.3f} -> {rd['read_js_late']:.3f} | switch {rd['read_switch_pre']:.2f} -> "
							f"{rd['read_switch_post']:.2f} -> {rd['read_switch_recovered']:.2f} | late counters: parry "
							f"{rd['read_parrier_counter_late']:.2f} block {rd['read_blocker_counter_late']:.2f} left "
							f"{rd['read_stepleft_counter_late']:.2f} right {rd['read_stepright_counter_late']:.2f}", flush=True)
				except Exception as e:
					print(f"[train] reading test failed: {type(e).__name__}: {e}")
			for r_i, frac in enumerate(league_at):
				if r_i not in league_done and trainer.iteration >= int(frac * total_iters):
					league_done.add(r_i)
					try:
						it.update(league_round(trainer, env, players, ckpt_dir, args, r_i, league_kept))
						save_all(ckpt_dir, trainer, players, args, stage, f"{trainer.iteration:06d}_league{r_i}")
					except Exception as e:
						print(f"[league] round {r_i} failed: {type(e).__name__}: {e}")
			if trainer.iteration % max(1, args.log_every) == 0:
				row = build_row(it, env_stats, players, time.time() - t_start)
				row.update({k: v for k, v in it.items() if k.startswith(("gate_", "read_", "league_"))})
				logger.log(row, trainer.decisions)
				if not args.quiet:
					print(f"[{trainer.iteration:5d}/{total_iters}] dec {trainer.decisions:.3g} | {it['decisions_per_s']:,.0f} dec/s "
						f"(roll {it['rollout_decisions_per_s']:,.0f}) | R/fight {row['reward_per_fight']:+.3f} win {row['keeper_win_rate']:.2f} "
						f"| spm {row['swings_per_min']:.1f} cost {row['cost_per_decision']:+.3f} lam {row['lambda']:.3f} "
						f"| ent {it['entropy']:.3f} pg {it['pg_loss']:+.4f} v {it['v_loss']:.4f} aux {it['aux_loss']:.3f} "
						f"acc {it['aux_acc']:.2f} kl {it['approx_kl']:.4f} | bands {row['curriculum_unlocked']}", flush=True)
			if args.save_every and trainer.iteration % args.save_every == 0:
				save_all(ckpt_dir, trainer, players, args, stage, f"{trainer.iteration:06d}")
				last_save_iter = trainer.iteration
	except KeyboardInterrupt:
		print("\n[train] aborted")
		exit_code = 130
	finally:
		signal.signal(signal.SIGINT, old_handler)
		if exit_code == 3:
			print("[train] stopped on a non-finite loss: latest.pt / latest.hwrl keep the last good checkpoint")
			save_all(ckpt_dir, trainer, players, args, stage, f"{trainer.iteration:06d}")   # writes only ckpt_nan_* if poisoned
		elif trainer.iteration > last_save_iter or iters_this_run == 0 or not (ckpt_dir / "latest.pt").exists():
			path = save_all(ckpt_dir, trainer, players, args, stage, f"{trainer.iteration:06d}")
			print(f"[train] final checkpoint: {path}")
		logger.close()
		env.close()
	return exit_code


if __name__ == "__main__":
	sys.exit(main())
