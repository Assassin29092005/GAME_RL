"""HellwalkerRL — train the keeper (RL.md §5, DESIGN.md §9).

    python RL/train.py --stage rl1                       feed-forward PPO vs one Habitual bot (skill 0.8), 1-fight
                                                          sessions: the sanity check (it learns THAT bot's counter)
    python RL/train.py --stage rl2                       recurrent PPO vs the curriculum population: the reader
    python RL/train.py --stage rl3 --init <rl2 ckpt>     rl2 continued, with evaluation gates logged as it trains
    python RL/train.py --resume RL/checkpoints/<run>/latest.pt    continue a run exactly where it stopped

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
	"rl1": {"recurrent": False, "players": "habitual", "fights": 1, "steps": 20_000_000, "envs": 1024, "gate_every": 0},
	"rl2": {"recurrent": True, "players": "population", "fights": None, "steps": 100_000_000, "envs": 4096, "gate_every": 0},
	"rl3": {"recurrent": True, "players": "population", "fights": None, "steps": 100_000_000, "envs": 4096, "gate_every": 50},
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
CSV_FIELDS = CSV_BASE + CSV_GROUPS + CSV_GATES


def parse_args(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--stage", choices=sorted(STAGES), default="rl2")
	ap.add_argument("--envs", type=int, default=None)
	ap.add_argument("--steps", type=float, default=None, help="total decisions (e.g. 1e8)")
	ap.add_argument("--run", default=None, help="run name (default <stage>-<timestamp>)")
	ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
	ap.add_argument("--seed", type=int, default=1)
	ap.add_argument("--resume", default=None, help="checkpoint to continue (model + optimizer + curriculum + counters)")
	ap.add_argument("--init", default=None, help="checkpoint to start from (weights only; e.g. rl3 from rl2)")
	ap.add_argument("--backend", choices=("auto", "dll", "mock"), default="auto")
	ap.add_argument("--threads", type=int, default=0, help="C++ env worker threads (0 = hardware concurrency)")
	ap.add_argument("--players", default=None, help=f"player set override ({', '.join(players_mod.PLAYER_SETS)})")
	ap.add_argument("--target-spm", type=float, default=66.0, help="aggression target (FRLConfig default 66)")
	ap.add_argument("--max-fight-seconds", type=int, default=180)
	# PPO
	ap.add_argument("--num-steps", type=int, default=128)
	ap.add_argument("--bptt", type=int, default=32)
	ap.add_argument("--epochs", type=int, default=4)
	ap.add_argument("--minibatches", type=int, default=8)
	ap.add_argument("--lr", type=float, default=3e-4)
	ap.add_argument("--lr-final", type=float, default=3e-5)
	ap.add_argument("--ent-coef", type=float, default=0.01)
	ap.add_argument("--ent-coef-final", type=float, default=0.001)
	ap.add_argument("--lambda-lr", type=float, default=0.01)
	ap.add_argument("--no-cost", action="store_true", help="disable the aggression constraint")
	ap.add_argument("--tf32", action="store_true", help="TF32 matmuls (faster; rollout/update ratio no longer exactly 1)")
	# network
	ap.add_argument("--small", action="store_true", help="a small network (smoke tests)")
	ap.add_argument("--hidden", type=int, default=256)
	ap.add_argument("--enc-hidden", type=int, default=256)
	ap.add_argument("--embed-dim", type=int, default=32)
	# bookkeeping
	ap.add_argument("--save-every", type=int, default=25, help="iterations between checkpoints + .hwrl exports")
	ap.add_argument("--log-every", type=int, default=1)
	ap.add_argument("--gate-every", type=int, default=None, help="iterations between evaluation gates (0 = off)")
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
	if k in ("pg_loss", "v_loss", "aux_loss", "loss", "entropy", "approx_kl", "clipfrac", "grad_norm", "aux_acc",
			"explained_variance", "ratio_dev_first_mb", "lr", "ent_coef", "return_std"):
		return "train/" + k
	if k in ("decisions_per_s", "rollout_decisions_per_s", "update_samples_per_s", "elapsed_s"):
		return "perf/" + k
	return "env/" + k


def build_row(it: Dict[str, float], env_stats: dict, players, elapsed: float) -> Dict[str, float]:
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


def save_all(ckpt_dir: Path, trainer: PPOTrainer, players, args, stage_cfg: dict, tag: str) -> Path:
	ckpt_dir.mkdir(parents=True, exist_ok=True)
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


# =====================================================================================================================
# Main
# =====================================================================================================================

def main(argv=None) -> int:
	args = parse_args(argv)
	resume_extra = None
	resume_model = None
	if args.resume:
		resume_model, resume_extra = load_checkpoint(args.resume)
		saved = resume_extra.get("args", {})
		# A resumed run keeps its identity (stage, run name, sizes) unless explicitly overridden.
		args.stage = saved.get("stage", args.stage)
		args.run = args.run or saved.get("run")
		if args.envs is None:
			args.envs = saved.get("envs")
		if args.steps is None:
			args.steps = saved.get("steps")
		if args.players is None:
			args.players = saved.get("players")
	stage = dict(STAGES[args.stage])
	if args.stage == "rl3" and not (args.init or args.resume):
		print("[train] rl3 continues rl2: pass --init <rl2 checkpoint> (or --resume an rl3 run)")
		return 2
	args.envs = int(args.envs or stage["envs"])
	args.steps = float(args.steps or stage["steps"])
	args.players = args.players or stage["players"]
	gate_every = stage["gate_every"] if args.gate_every is None else args.gate_every
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

	# Environment.
	cfg_env = hwcore.env_config(max_fight_seconds=args.max_fight_seconds, target_spm=args.target_spm,
		num_threads=args.threads)
	env = env_mod.make_env(args.envs, players, seed=args.seed, backend=args.backend, config=cfg_env,
		pin_memory=(device.type == "cuda"))

	# Model.
	if resume_model is not None:
		model = resume_model
	elif args.init:
		init_model, _ = load_checkpoint(args.init)
		model = init_model
		if model.recurrent != stage["recurrent"]:
			print(f"[train] note: --init network recurrent={model.recurrent}, stage wants {stage['recurrent']}; keeping the checkpoint's")
	else:
		sizes = dict(enc_hidden=64, embed_dim=16, hidden=64) if args.small else \
			dict(enc_hidden=args.enc_hidden, embed_dim=args.embed_dim, hidden=args.hidden)
		model = HellwalkerNet(obs_dim=hwcore.OBS_DIM, num_actions=hwcore.NUM_ACTIONS, aux_classes=hwcore.AUX_CLASSES,
			token_vocab=hwcore.TOKEN_VOCAB, history_tokens=hwcore.HISTORY_TOKENS, recurrent=stage["recurrent"], side=0,
			**sizes)

	ppo_cfg = PPOConfig(num_steps=args.num_steps, bptt=args.bptt, epochs=args.epochs, minibatches=args.minibatches,
		lr=args.lr, lr_final=args.lr_final, ent_coef=args.ent_coef, ent_coef_final=args.ent_coef_final,
		lambda_lr=args.lambda_lr, use_cost=not args.no_cost, tf32=args.tf32)
	per_iter = args.envs * args.num_steps
	total_iters = max(1, int(math.ceil(args.steps / per_iter)))
	trainer = PPOTrainer(model, env, ppo_cfg, device=device, total_iterations=total_iters, seed=args.seed)
	if resume_extra and "trainer" in resume_extra:
		trainer.load_state_dict(resume_extra["trainer"])
		trainer.total_iterations = total_iters

	# Run folder.
	run_dir.mkdir(parents=True, exist_ok=True)
	with open(run_dir / "config.json", "w") as fh:
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
			if trainer.iteration % max(1, args.log_every) == 0:
				row = build_row(it, env_stats, players, time.time() - t_start)
				row.update({k: v for k, v in it.items() if k.startswith("gate_")})
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
		if trainer.iteration > last_save_iter or iters_this_run == 0 or not (ckpt_dir / "latest.pt").exists():
			path = save_all(ckpt_dir, trainer, players, args, stage, f"{trainer.iteration:06d}")
			print(f"[train] final checkpoint: {path}")
		logger.close()
		env.close()
	return exit_code


if __name__ == "__main__":
	sys.exit(main())
