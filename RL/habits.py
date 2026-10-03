"""The reading test: does the keeper answer different habits differently, and more so as it learns them?

Four groups of pure-habit players (the same response to every boss swing class, 5 % noise):
    parrier   parries        -> a reader should turn to feints / delayed heavies / unparryables
    stepleft  dodges left    -> left-tracking sweeps
    stepright dodges right   -> right-tracking sweeps
    blocker   holds guard    -> guard pressure, grabs, the killer
For each group, over immortal fixed-length sessions, every keeper attack is recorded with its index k in the session
(the C++ attack log: the game's own forward pass, greedy, all cores), and the report gives the share of the group's
counters among the keeper's attacks early (k 1-5) vs late (k 21-60), the move mix, and the CLEAN hit rate (a Hit
outcome; blocked chip damage is not a hit — review finding: the first version counted any damage). A reader starts
alike against everyone (it cannot know yet) and diverges toward each group's counter; a strong-but-blind keeper plays
the same mix against all four.

The habit switch: a parrier turns into a step-left player from keeper swing #switch_after on (that swing is the first
answered from the new table — review finding: the windows were one swing late). Windows: before = the 10 swings before
it, after = it and the 9 after, recovered = 20-39 swings after it.

    python RL/habits.py --hwrl <policy.hwrl> [--sessions 256] [--identity 0] [--skill 1] [--json out.json]
    python RL/habits.py --ckpt RL/checkpoints/<run>/latest.pt ...
API: reading_test(policy, ...) -> dict (train.py logs it during training).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from typing import Dict, Optional

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hwcore  # noqa: E402

# Response columns of a habit table: Parry, Block, StepL, StepR, StepB, StepF, Attack, None
GROUPS = {"parrier": 0, "blocker": 1, "stepleft": 2, "stepright": 3}
HIT = 1  # EHitOutcome::Hit


def counters_for(group: str, names: list) -> set:
	"""Action indices a reader would favour against this group (by move name)."""
	want = {
		"parrier": {"FeintEarly", "FeintMid", "FeintLate", "DelayedHeavy", "KillerThrust", "Grab"},
		"stepleft": {"SweepLeft", "SweepLeftLate", "HeavySweepLeft"},
		"stepright": {"SweepRight", "SweepRightLate", "HeavySweepRight"},
		"blocker": {"Grab", "KillerThrust", "HeavyCleave", "HeavySweepLeft", "HeavySweepRight", "DelayedHeavy"},
	}[group]
	return {i for i, n in enumerate(names) if n in want}


def pure_table(col: int) -> np.ndarray:
	table = np.full((4, 8), 0.02, np.float32)
	table[:, col] = 1.0
	return table


def group_specs(group: str, n: int, seed: int, switch_to: Optional[str] = None, switch_after: int = 30,
		identity: int = 0, keeper_skill: float = 1.0) -> list:
	"""Pure-habit players; with switch_to, table B (that group's habit) answers from keeper swing #switch_after on."""
	rng = np.random.default_rng(seed)
	col = GROUPS[group]
	specs = []
	for _ in range(n):
		a = pure_table(col)
		b = pure_table(GROUPS[switch_to]) if switch_to else a
		specs.append(hwcore.make_spec(kind=hwcore.KIND_HABIT, skill=float(rng.uniform(0.5, 0.9)), fights_in_session=2,
			habit=np.stack([a, b]), habit_switch_after=switch_after if switch_to else -1, habit_noise=0.05, adapts=False,
			tag=900 + col, keeper_identity=identity, keeper_skill=keeper_skill))
	return specs


def js_divergence(dists: list) -> float:
	"""Generalised Jensen-Shannon divergence (bits) of several distributions: 0 = identical, log2(n) = disjoint."""
	P = np.array(dists, float) + 1e-12
	P /= P.sum(1, keepdims=True)
	M = P.mean(0)
	H = lambda x: float(-(x * np.log2(x)).sum())  # noqa: E731
	return H(M) - float(np.mean([H(p) for p in P]))


def summarise(rec: np.ndarray, names: list, counters: set, switch_after: Optional[int] = None, max_k: int = 60) -> dict:
	"""Windows over attack records (fields k, action, outcome, dealt)."""
	def window(lo, hi):
		sel = rec[(rec["k"] >= lo) & (rec["k"] <= hi)]
		if len(sel) == 0:
			return {"n": 0, "counter_share": float("nan"), "p_hit": float("nan"), "p_dealt": float("nan")}
		return {"n": int(len(sel)), "counter_share": float(np.isin(sel["action"], list(counters)).mean()),
			"p_hit": float((sel["outcome"] == HIT).mean()), "p_dealt": float((sel["dealt"] != 0).mean())}

	def mix(lo, hi):
		sel = rec[(rec["k"] >= lo) & (rec["k"] <= hi)]
		v = np.bincount(sel["action"], minlength=len(names)).astype(float)
		return v / max(1.0, v.sum())

	def top(v, n=4):
		return [(names[i], round(float(v[i]), 3)) for i in np.argsort(-v)[:n] if v[i] > 0]

	out = {"sessions": int(len(np.unique(rec["session"]))) if len(rec) else 0, "early": window(1, 5), "mid": window(6, 20),
		"late": window(21, max_k), "mix_early": mix(1, 5).tolist(), "mix_late": mix(21, max_k).tolist(),
		"late_mix": top(mix(21, max_k)), "early_mix": top(mix(1, 5))}
	if switch_after is not None:
		sa = int(switch_after)
		out["pre_switch"] = window(sa - 10, sa - 1)
		out["post_switch"] = window(sa, sa + 9)
		out["recovered"] = window(sa + 20, sa + 39)
		out["mix_pre"] = top(mix(sa - 10, sa - 1))
		out["mix_post"] = top(mix(sa, sa + 9))
		out["mix_recovered"] = top(mix(sa + 20, sa + 39))
		# The table the player actually planned its answers with (recorded per swing by the bot itself, not inferred):
		# the first attack answered from table B should be swing #sa.
		ph = rec[rec["habit_phase"] == 1]
		out["first_phase_b_k"] = int(ph["k"].min()) if len(ph) else -1
	return out


def reading_test(policy: "hwcore.Policy", sessions: int = 256, seed: int = 424242, identity: int = 0,
		keeper_skill: float = 1.0, fight_seconds: int = 60, switch_after: int = 30, threads: int = 0) -> Dict:
	"""The whole test on the C++ forward pass: four habit groups + the parry -> step-left switch."""
	names = hwcore.action_names()
	out: Dict = {}
	for g, col in GROUPS.items():
		rec, _ = hwcore.attack_log(policy, group_specs(g, 64, seed + col, identity=identity, keeper_skill=keeper_skill),
			sessions, seed + col, immortal=True, fight_seconds=fight_seconds, num_threads=threads)
		out[g] = summarise(rec, names, counters_for(g, names))
	out["js_early"] = js_divergence([out[g]["mix_early"] for g in GROUPS])
	out["js_late"] = js_divergence([out[g]["mix_late"] for g in GROUPS])
	rec, _ = hwcore.attack_log(policy, group_specs("parrier", 64, seed + 77, switch_to="stepleft", switch_after=switch_after,
		identity=identity, keeper_skill=keeper_skill), sessions, seed + 77, immortal=True, fight_seconds=fight_seconds,
		num_threads=threads)
	out["switch_parry_to_stepleft"] = summarise(rec, names, counters_for("stepleft", names), switch_after=switch_after)
	return out


def flat_metrics(r: Dict, prefix: str = "read_") -> Dict[str, float]:
	"""The numbers train.py logs every gate."""
	sw = r["switch_parry_to_stepleft"]
	d = {f"{prefix}js_early": r["js_early"], f"{prefix}js_late": r["js_late"],
		f"{prefix}switch_pre": sw["pre_switch"]["p_hit"], f"{prefix}switch_post": sw["post_switch"]["p_hit"],
		f"{prefix}switch_recovered": sw["recovered"]["p_hit"]}
	for g in GROUPS:
		d[f"{prefix}{g}_hit_early"] = r[g]["early"]["p_hit"]
		d[f"{prefix}{g}_hit_late"] = r[g]["late"]["p_hit"]
		d[f"{prefix}{g}_counter_late"] = r[g]["late"]["counter_share"]
	return d


def policy_from_args(args) -> "hwcore.Policy":
	if args.hwrl:
		return hwcore.Policy(args.hwrl)
	import export
	import model as M
	net, extra = M.load_checkpoint(args.ckpt)
	path = os.path.join(tempfile.gettempdir() if os.environ.get("TMP") else ".", "habits_tmp.hwrl")
	export.export_hwrl(net, path, obs_layout_version=int(extra.get("obs_layout_version", hwcore.OBS_LAYOUT_VERSION)))
	return hwcore.Policy(path)


def main(argv=None) -> int:
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--ckpt", help="torch checkpoint (exported to a temporary .hwrl)")
	ap.add_argument("--hwrl", help="exported policy")
	ap.add_argument("--sessions", type=int, default=256)
	ap.add_argument("--seed", type=int, default=424242)
	ap.add_argument("--identity", type=int, default=0, help="0 Warden, 1 Sage, 2 Returned")
	ap.add_argument("--skill", type=float, default=1.0)
	ap.add_argument("--json", default=None)
	args = ap.parse_args(argv)
	if not (args.ckpt or args.hwrl):
		ap.error("--ckpt or --hwrl")
	pol = policy_from_args(args)
	r = reading_test(pol, args.sessions, args.seed, args.identity, args.skill)
	for g in GROUPS:
		e, l = r[g]["early"], r[g]["late"]
		print(f"{g:10s} clean hit {e['p_hit']:.3f} -> {l['p_hit']:.3f} (any damage {e['p_dealt']:.3f} -> {l['p_dealt']:.3f}) | "
			f"early mix {r[g]['early_mix']} | late mix {r[g]['late_mix']}", flush=True)
	print(f"move-mix divergence between the four habits (JS, bits; 0 = same play vs everyone, 2 = disjoint): "
		f"early {r['js_early']:.3f} -> late {r['js_late']:.3f}", flush=True)
	sw = r["switch_parry_to_stepleft"]
	print(f"habit switch parry -> step left at keeper swing #30 (first swing answered from table B: #{sw['first_phase_b_k']}): "
		f"hit {sw['pre_switch']['p_hit']:.3f} (10 before) -> {sw['post_switch']['p_hit']:.3f} (it + 9 after) -> "
		f"{sw['recovered']['p_hit']:.3f} (20-39 after)", flush=True)
	print(f"   mix before {sw['mix_pre']}", flush=True)
	print(f"   mix after  {sw['mix_post']}", flush=True)
	print(f"   mix later  {sw['mix_recovered']}", flush=True)
	if args.json:
		with open(args.json, "w", encoding="utf-8") as f:
			json.dump(r, f, indent=1)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
