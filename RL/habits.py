"""The reading test: does the keeper answer different habits differently, and more so as it learns them?

Four groups of pure-habit players (the same response to every boss swing class, 5 % noise):
    parrier   parries        -> a reader should turn to feints / delayed heavies / unparryables
    stepleft  dodges left    -> left-tracking sweeps
    stepright dodges right   -> right-tracking sweeps
    blocker   holds guard    -> guard pressure, grabs, the killer
For each group, over immortal fixed-length sessions, record every keeper attack with its index k in the session, and
report the share of the group's counters among the keeper's attacks in early (k 1-5) vs late (k 21-60) windows, plus
the hit rate. A reader starts alike against everyone (it cannot know yet) and diverges toward each group's counter;
a strong-but-blind keeper plays the same mix against all four.

    python RL/habits.py --ckpt RL/checkpoints/<run>/latest.pt [--sessions 256] [--json out.json]
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hwcore  # noqa: E402
import players as players_mod  # noqa: E402

# Response columns of a habit table: Parry, Block, StepL, StepR, StepB, StepF, Attack, None
GROUPS = {"parrier": 0, "blocker": 1, "stepleft": 2, "stepright": 3}

MOVE_NAMES = None


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


def group_specs(group: str, n: int, seed: int, switch_to: str = None, switch_after: int = 30) -> list:
	"""Pure-habit players; with switch_to, the habit changes to that group's after switch_after keeper swings."""
	rng = np.random.default_rng(seed)
	col = GROUPS[group]
	specs = []
	for i in range(n):
		a = pure_table(col)
		b = pure_table(GROUPS[switch_to]) if switch_to else a
		specs.append(hwcore.make_spec(kind=hwcore.KIND_HABIT, skill=float(rng.uniform(0.5, 0.9)), fights_in_session=2,
			habit=np.stack([a, b]), habit_switch_after=switch_after if switch_to else -1, habit_noise=0.05, adapts=False,
			tag=900 + col))
	return specs


def run_group(engine, group: str, sessions: int, seed: int, names: list, max_k: int = 60, fight_seconds: int = 60,
		switch_to: str = None, switch_after: int = 30) -> dict:
	import env as env_mod
	cfg = hwcore.env_config(immortal=True, max_fight_seconds=fight_seconds)
	specs = group_specs(group, 64, seed, switch_to, switch_after)
	n_env = int(min(256, sessions))
	e = env_mod.HellwalkerVecEnv(n_env, players_mod.FixedSet(specs, group), seed=seed, config=cfg, backend="dll")
	obs = e.reset()
	engine.reset(n_env)
	counters = counters_for(group, names)
	k = np.zeros(n_env, np.int64)
	pend = [None] * n_env
	recs = [[] for _ in range(n_env)]
	done = []
	steps = 0
	while len(done) < sessions and steps < 60000:
		a, top, p = engine.act(obs)
		obs, _, _, sd, fd, info = e.step(a, top, p)
		steps += 1
		for i in range(n_env):
			if info["swings"][i] > 0:
				if pend[i] is not None:
					recs[i].append(pend[i])
				k[i] += 1
				pend[i] = [int(k[i]), int(a[i]), bool(info["dmg_dealt"][i] > 0)]
			elif info["dmg_dealt"][i] > 0 and pend[i] is not None:
				pend[i][2] = True
			if fd[i] and pend[i] is not None:
				recs[i].append(pend[i])
				pend[i] = None
			if sd[i]:
				done.append(recs[i])
				recs[i] = []
				k[i] = 0
	e.close()

	def window(lo, hi):
		ks = [(act, hit) for r in done for kk, act, hit in r if lo <= kk <= hi]
		if not ks:
			return {"n": 0, "counter_share": float("nan"), "p_hit": float("nan")}
		acts = np.array([x[0] for x in ks])
		hits = np.array([x[1] for x in ks], float)
		return {"n": len(ks), "counter_share": float(np.isin(acts, list(counters)).mean()), "p_hit": float(hits.mean())}

	def mix(lo, hi):
		v = np.zeros(len(names))
		for r in done:
			for kk, act, _ in r:
				if lo <= kk <= hi:
					v[act] += 1
		return v / max(1.0, v.sum())

	def top(v, n=4):
		return [(names[i], round(float(v[i]), 3)) for i in np.argsort(-v)[:n] if v[i] > 0]

	out = {"sessions": len(done), "early": window(1, 5), "mid": window(6, 20), "late": window(21, max_k),
		"mix_early": mix(1, 5).tolist(), "mix_late": mix(21, max_k).tolist(), "late_mix": top(mix(21, max_k)),
		"early_mix": top(mix(1, 5))}
	if switch_to:
		# around the switch (k = switch_after + 1 is the first swing answered with the new habit)
		out["pre_switch"] = window(switch_after - 9, switch_after)
		out["post_switch"] = window(switch_after + 1, switch_after + 10)
		out["recovered"] = window(switch_after + 21, switch_after + 40)
		out["mix_pre"] = top(mix(switch_after - 9, switch_after))
		out["mix_post"] = top(mix(switch_after + 1, switch_after + 10))
		out["mix_recovered"] = top(mix(switch_after + 21, switch_after + 40))
	return out


def js_divergence(dists: list) -> float:
	"""Generalised Jensen-Shannon divergence (bits) of several distributions: 0 = identical, log2(n) = disjoint."""
	P = np.array(dists, float) + 1e-12
	P /= P.sum(1, keepdims=True)
	M = P.mean(0)
	H = lambda x: float(-(x * np.log2(x)).sum())
	return H(M) - float(np.mean([H(p) for p in P]))


def main(argv=None) -> int:
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--ckpt", help="torch checkpoint")
	ap.add_argument("--hwrl", help="exported policy (C++ forward pass) instead of --ckpt")
	ap.add_argument("--sessions", type=int, default=256)
	ap.add_argument("--seed", type=int, default=424242)
	ap.add_argument("--device", default="cuda")
	ap.add_argument("--json", default=None)
	args = ap.parse_args(argv)
	import eval as ev
	if args.ckpt:
		import model as M
		net, _ = M.load_checkpoint(args.ckpt)
		engine = ev.TorchEngine(net, args.device)
	else:
		engine = ev.CppEngine(hwcore.Policy(args.hwrl))
	names = hwcore.action_names()
	out = {}
	for g in GROUPS:
		r = run_group(engine, g, args.sessions, args.seed + GROUPS[g], names)
		out[g] = r
		e, l = r["early"], r["late"]
		print(f"{g:10s} hit {e['p_hit']:.3f} -> {l['p_hit']:.3f} | early mix {r['early_mix']} | late mix {r['late_mix']}", flush=True)
	js_early = js_divergence([out[g]["mix_early"] for g in GROUPS])
	js_late = js_divergence([out[g]["mix_late"] for g in GROUPS])
	print(f"move-mix divergence between the four habits (JS, bits; 0 = same play vs everyone, 2 = disjoint): "
		f"early {js_early:.3f} -> late {js_late:.3f}", flush=True)
	sw = run_group(engine, "parrier", args.sessions, args.seed + 77, names, switch_to="stepleft", switch_after=30)
	print(f"habit switch parry -> step left after 30 keeper swings: hit {sw['pre_switch']['p_hit']:.3f} (before) -> "
		f"{sw['post_switch']['p_hit']:.3f} (10 after) -> {sw['recovered']['p_hit']:.3f} (21-40 after)", flush=True)
	print(f"   mix before {sw['mix_pre']}", flush=True)
	print(f"   mix after  {sw['mix_post']}", flush=True)
	print(f"   mix later  {sw['mix_recovered']}", flush=True)
	out["switch_parry_to_stepleft"] = sw
	out["js_early"], out["js_late"] = js_early, js_late
	if args.json:
		with open(args.json, "w", encoding="utf-8") as f:
			json.dump(out, f, indent=1)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
