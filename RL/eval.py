"""HellwalkerRL — evaluate a trained keeper against the RL.md §7 bar, on players it never trained against.

    python RL/eval.py --ckpt RL/checkpoints/<run>/latest.pt --name <report>            everything
    python RL/eval.py --hwrl RL/checkpoints/<run>/latest.hwrl --only b0 --sessions 16   one piece
    --only b0|curve|arms (comma-separated for several)

Pieces:
  b0     Sim/out/ThesisSim.exe --brain rl --policy <hwrl> --sessions N: the B0 thesis sweep with the RL keeper as the
         adaptive arm; its VERDICT (thesis / aggression floor / masher lethal / net exchange) is parsed.
  curve  the adaptation curve through the env (the torch policy from --ckpt, greedy; or the C++ policy from --hwrl):
         P(keeper attack hits) vs the k-th keeper attack of the session, for held-out LOW-alpha (strong habits) and
         HIGH-alpha (near random) players — a reader's curve rises for the first and stays flat for the second — and
         the same aligned on a scheduled habit switch (it should dip after the switch, then recover).
  arms   hwcore.eval_sessions for arm 0 (Pathbreaker script), 1 (classic Hellwalker) and 2 (RL) on IDENTICAL seeded
         held-out players: not-a-bully (vs near-random players the RL keeper deals about what the script deals, not
         more), classic vs RL head-to-head, aggression floor, net exchange, masher lethal.

Writes RL/reports/<name>.md (readable) and RL/reports/<name>.json (every number).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
import time
import zlib
from pathlib import Path
from typing import Dict, List, Optional

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
	sys.path.insert(0, str(HERE))

import numpy as np  # noqa: E402

import hwcore  # noqa: E402
import players as players_mod  # noqa: E402

ROOT = HERE.parent
THESIS_EXE = ROOT / "Sim" / "out" / "ThesisSim.exe"
REPORTS_DIR = HERE / "reports"


# =====================================================================================================================
# (1) B0 via ThesisSim
# =====================================================================================================================

VERDICT_KEYS = (("skilled players", "thesis"), ("aggression floor", "aggression_floor"), ("masher", "masher_lethal"),
	("net exchange", "net_exchange"))


def parse_thesis_output(text: str) -> dict:
	"""The VERDICT block (+ the sweep rows and the lethal lines) of ThesisSim's output."""
	out: dict = {"checks": {}, "rows": [], "lethal": []}
	m = re.search(r"adaptive arm:\s*(.+)", text)
	out["adaptive_arm"] = m.group(1).strip() if m else None
	m = re.search(r"reference \(Pathbreaker\) swings/min = ([\d.]+)", text)
	out["reference_spm"] = float(m.group(1)) if m else None
	verdict = text.split("VERDICT", 1)[1] if "VERDICT" in text else ""
	for line in verdict.splitlines():
		for needle, key in VERDICT_KEYS:
			if line.strip().startswith(needle):
				out["checks"][key] = "PASS" if line.rstrip().endswith("PASS") else "FAIL"
	out["b0"] = "PASS" if "B0 PASSES" in verdict else ("FAIL" if "B0 FAILS" in verdict else None)
	row_re = re.compile(r"^(\w+)\s+([\d.]+) \|\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+\|\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+\|"
		r"\s*([-\d.]+)\s+([-\d.]+)\s+\|\s*([-\d.]+)%\s+([-\d.]+)\s+\|\s*([-\d.]+)\s+([-\d.]+)\s+\|\s*(.*)$")
	for line in text.splitlines():
		r = row_re.match(line.strip())
		if r:
			g = r.groups()
			out["rows"].append({"profile": g[0], "skill": float(g[1]), "script_dmg": [float(x) for x in g[2:5]],
				"adaptive_dmg": [float(x) for x in g[5:8]], "spm_script": float(g[8]), "spm_adaptive": float(g[9]),
				"xch_script": float(g[12]), "xch_adaptive": float(g[13]), "verdict": g[14].strip()})
	for line in text.splitlines():
		r = re.match(r"\s*(\w+)\s+skill ([\d.]+) vs (\S+)\s*: player died (\d+)/(\d+).*boss died (\d+)/(\d+)", line)
		if r:
			out["lethal"].append({"profile": r.group(1), "skill": float(r.group(2)), "arm": r.group(3),
				"player_died": int(r.group(4)), "of": int(r.group(5)), "boss_died": int(r.group(6))})
	return out


def run_b0(hwrl: str, sessions: int = 4, script: int = 0, exe: Path = THESIS_EXE, timeout: int = 7200,
		identity: int = -1, skill: float = 1.0) -> dict:
	if not exe.exists():
		return {"skipped": f"{exe} not found (build it with Sim\\build.bat)"}
	cmd = [str(exe), "--brain", "rl", "--policy", str(hwrl), "--sessions", str(sessions), "--script", str(script),
		"--skill", str(skill)] + (["--identity", str(identity)] if identity >= 0 else [])
	t0 = time.time()
	try:
		p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
	except subprocess.TimeoutExpired:
		return {"skipped": f"ThesisSim timed out after {timeout} s", "cmd": cmd}
	res = parse_thesis_output(p.stdout)
	res.update({"cmd": cmd, "exit_code": p.returncode, "seconds": time.time() - t0})
	if p.returncode == 3:
		res["error"] = "ThesisSim could not build the RL arm (policy failed to load?)"
	res["stdout_tail"] = p.stdout[-4000:]
	if p.stderr:
		res["stderr_tail"] = p.stderr[-2000:]
	return res


# =====================================================================================================================
# (2) The adaptation curve through the env
# =====================================================================================================================

class TorchEngine:
	"""Greedy decisions with the torch policy (the env's observation dict in, actions + read-head top out)."""

	def __init__(self, model, device: str = "cpu"):
		import torch
		self.torch = torch
		self.device = torch.device(device)
		self.model = model.to(self.device).eval()
		self.h = None

	def reset(self, n: int) -> None:
		self.h = self.model.initial_state(n, self.device)

	def act(self, obs: dict):
		torch = self.torch
		with torch.no_grad():
			o = torch.from_numpy(np.ascontiguousarray(obs["obs"])).to(self.device)
			t = torch.from_numpy(np.ascontiguousarray(obs["tokens"])).to(self.device)
			m = torch.from_numpy(np.ascontiguousarray(obs["mask"])).to(self.device)
			s = torch.from_numpy(np.ascontiguousarray(obs["starts"])).to(self.device)
			self.h = self.h.masked_fill(s.unsqueeze(-1), 0.0)
			logits, _, h_new = self.model.step(o, t, m, self.h)
			a = logits.argmax(-1)
			top_p, top = torch.softmax(self.model.aux_logits(h_new, a), -1).max(-1)
			self.h = h_new
		return a.cpu().numpy().astype(np.int32), top.cpu().numpy().astype(np.int8), top_p.cpu().numpy().astype(np.float32)


class CppEngine:
	"""Greedy decisions with the game's C++ forward pass (hwcore.Policy) — what ThesisSim / the game run."""

	def __init__(self, policy: "hwcore.Policy"):
		self.pol = policy
		self.h = None

	def reset(self, n: int) -> None:
		self.h = np.zeros((n, self.pol.hidden), np.float32)

	def act(self, obs: dict):
		self.h[obs["starts"]] = 0.0
		out = self.pol.forward(obs["obs"], obs["tokens"], obs["mask"].view(np.uint8), self.h)
		a = out["argmax"]
		probs = self.pol.aux(out["h_out"], a)
		self.h = out["h_out"]
		return a.astype(np.int32), probs.argmax(-1).astype(np.int8), probs.max(-1).astype(np.float32)


def run_curve_set(engine, specs: List, n_sessions: int, seed: int, backend: str, cfg, max_k: int = 80,
		max_steps: int = 40000, max_envs: int = 256) -> dict:
	"""Play complete sessions against `specs` (cycled) and record every keeper attack of each session: its index k in
	the session (fights of a session continue the count — the memory carries), whether it hit, and the habit table the
	player was answering with. An attack "hit" when a CLEAN hit (EHitOutcome::Hit — blocked chip damage is not one;
	review finding) landed in the step it was committed in or in a later step before its next attack (same fight)."""
	import env as env_mod
	n_env = int(max(1, min(len(specs) * 4, n_sessions, max_envs)))
	source = players_mod.FixedSet(specs, "curve")
	e = env_mod.HellwalkerVecEnv(n_env, source, seed=seed, config=cfg, backend=backend)
	obs = e.reset()
	engine.reset(n_env)
	k = np.zeros(n_env, np.int64)
	pending = np.zeros(n_env, np.int64)      # k of the attack awaiting its outcome (0 = none)
	pend_hit = np.zeros(n_env, bool)
	pend_phase = np.zeros(n_env, np.int64)
	sess_records: List[list] = [[] for _ in range(n_env)]
	sessions: List[list] = []
	reads = 0
	decisions = 0

	def finalize(i: int) -> None:
		if pending[i] > 0:
			sess_records[i].append((int(pending[i]), bool(pend_hit[i]), int(pend_phase[i])))
			pending[i] = 0

	steps = 0
	while len(sessions) < n_sessions and steps < max_steps:
		a, top, p = engine.act(obs)
		obs, _, _, sd, fd, info = e.step(a, top, p)
		steps += 1
		decisions += n_env
		reads += int(info["read_counter"].sum())
		sw = info["swings"] > 0
		dealt = info["hits"] > 0
		for i in np.flatnonzero(sw | dealt | fd):
			if sw[i]:
				finalize(i)
				k[i] += 1
				pending[i] = k[i]
				pend_hit[i] = dealt[i]
				pend_phase[i] = info["habit_phase"][i]
			elif dealt[i] and pending[i] > 0:
				pend_hit[i] = True
			if fd[i]:
				finalize(i)   # a new fight: the pending attack cannot land any more
			if sd[i]:
				sessions.append(sess_records[i])
				sess_records[i] = []
				k[i] = 0
	e.close()

	hits = np.zeros(max_k + 1)
	cnt = np.zeros(max_k + 1)
	offs: Dict[int, List[int]] = {}
	for rec in sessions:
		sw_k = next((kk for kk, _, ph in rec if ph == 1), None)
		for kk, h, _ in rec:
			if kk <= max_k:
				hits[kk] += h
				cnt[kk] += 1
			if sw_k is not None:
				d = offs.setdefault(kk - sw_k, [0, 0])
				d[0] += h
				d[1] += 1
	p_by_k = [float(hits[i] / cnt[i]) if cnt[i] else None for i in range(max_k + 1)]

	def window(lo: int, hi: int):
		n = cnt[lo:hi + 1].sum()
		return (float(hits[lo:hi + 1].sum() / n) if n else float("nan")), int(n)

	bins = []
	for lo in range(1, max_k + 1, 5):
		p_, n_ = window(lo, min(lo + 4, max_k))
		bins.append({"k": f"{lo}-{min(lo + 4, max_k)}", "p_hit": p_, "n": n_})
	early, n_early = window(1, 5)
	late, n_late = window(21, 60)

	def offwin(lo: int, hi: int):
		h = sum(offs[o][0] for o in offs if lo <= o <= hi)
		n = sum(offs[o][1] for o in offs if lo <= o <= hi)
		return (h / n if n else float("nan")), n

	switch = None
	if offs:
		switch = {"bins": [], "sessions_with_switch": sum(1 for r in sessions if any(ph == 1 for _, _, ph in r))}
		for lo in range(-20, 40, 5):
			p_, n_ = offwin(lo, lo + 4)
			switch["bins"].append({"offset": f"{lo}..{lo + 4}", "p_hit": p_, "n": n_})
		switch["before"] = offwin(-10, -1)[0]
		switch["after"] = offwin(0, 9)[0]
		switch["recovered"] = offwin(20, 39)[0]
	return {"sessions": len(sessions), "decisions": decisions, "attacks": int(cnt.sum()),
		"attacks_per_session": float(sum(len(r) for r in sessions) / max(len(sessions), 1)),
		"p_hit_by_k": p_by_k, "n_by_k": cnt.astype(int).tolist(), "bins": bins,
		"early_p_hit": early, "early_n": n_early, "late_p_hit": late, "late_n": n_late,
		"rise": late - early if n_early and n_late else float("nan"), "switch": switch,
		"reads_per_1k_decisions": 1000.0 * reads / max(decisions, 1)}


def run_curve(engine, seed: int, backend: str, n_sessions: int = 128, fights: int = 2, switch_after: int = 30,
		n_players: int = 64, target_spm: float = 66.0, identity: int = 0, keeper_skill: float = 1.0) -> dict:
	# Immortal fights of a fixed length (as B0 measures rates): a mortal fight ends when the player dies, so the late
	# part of the curve would only count the players who survived longest (survivorship bias).
	cfg = hwcore.env_config(target_spm=target_spm, immortal=True, max_fight_seconds=60)
	kw = dict(fights=fights, identity=identity, keeper_skill=keeper_skill)
	sets = {
		"low_alpha": players_mod.heldout_habit_players(n_players, 0, seed, switch=False, variant=11, **kw),
		"high_alpha": players_mod.heldout_habit_players(n_players, 3, seed, switch=False, variant=12, **kw),
		"switch_low_alpha": players_mod.heldout_habit_players(n_players, 0, seed, switch=True, switch_after=switch_after,
			variant=13, **kw),
		"switch_high_alpha": players_mod.heldout_habit_players(n_players, 3, seed, switch=True, switch_after=switch_after,
			variant=14, **kw),
	}
	out = {"config": {"n_sessions": n_sessions, "fights": fights, "switch_after": switch_after, "players": n_players,
		"backend": backend}}
	for name, specs in sets.items():
		t0 = time.time()
		out[name] = run_curve_set(engine, specs, n_sessions, seed + zlib.crc32(name.encode()) % 1000, backend, cfg)
		out[name]["seconds"] = time.time() - t0
		print(f"[eval] curve {name}: {out[name]['sessions']} sessions, {out[name]['attacks']} attacks, "
			f"P(hit) k1-5 {out[name]['early_p_hit']:.3f} -> k21-60 {out[name]['late_p_hit']:.3f}", flush=True)
	lo, hi = out["low_alpha"], out["high_alpha"]
	sw = out["switch_low_alpha"].get("switch") or {}
	out["checks"] = {
		# A reader's hit rate climbs against strong habits and much less against near-random players.
		"rises_on_habits": bool(lo["rise"] > 0.05),
		"reads_not_strength": bool(lo["rise"] > hi["rise"] + 0.03),
		# After a scheduled habit switch the old read goes stale: the hit rate dips, then recovers.
		"dips_after_switch": bool(sw.get("before", float("nan")) - sw.get("after", float("nan")) > 0.03),
		"recovers_after_switch": bool(sw.get("recovered", float("nan")) > sw.get("after", float("nan")) + 0.03),
	}
	return out


# =====================================================================================================================
# (3) Arms on identical seeded players (C++): not-a-bully, classic vs RL
# =====================================================================================================================

def run_arms(policy: Optional["hwcore.Policy"], seed: int, n_players: int = 16, sessions: int = 2, fights: int = 3,
		fight_seconds: int = 90, script_index: int = 0, bully_tolerance: float = 0.15, identity: int = 0,
		keeper_skill: float = 1.0) -> dict:
	if not hwcore.available():
		return {"skipped": "hwrl.dll not available (build it with RL\\native\\build.bat)"}
	sets = players_mod.eval_sets(seed=seed, n=n_players, fights=fights, identity=identity, keeper_skill=keeper_skill)
	arms = [hwcore.ARM_SCRIPT, hwcore.ARM_CLASSIC] + ([hwcore.ARM_RL] if policy is not None else [])
	out: dict = {"config": {"players": n_players, "sessions": sessions, "fights": fights, "fight_seconds": fight_seconds,
		"script_index": script_index, "seed": seed, "identity": identity, "keeper_skill": keeper_skill}, "sets": {}}
	for name, specs in sets.items():
		lethal = name == "masher"
		res = {}
		for arm in arms:
			runs = []
			for i, s in enumerate(specs):
				runs.append(hwcore.eval_sessions(arm, s, sessions, seed * 7919 + 101 * i + 1, policy=policy if arm == hwcore.ARM_RL else None,
					immortal=not lethal, fight_seconds=180 if lethal else fight_seconds, script_index=script_index))
			res[hwcore.ARM_NAMES[arm]] = hwcore.sum_eval_stats(runs)
		out["sets"][name] = res
		print(f"[eval] arms {name}: " + ", ".join(f"{k.split()[0]} {v['dmg_per_min']:.1f} dmg/min {v['swings_per_min']:.0f} spm"
			for k, v in res.items()), flush=True)
	if policy is None:
		out["checks"] = {}
		return out
	S, C, R = (hwcore.ARM_NAMES[a] for a in (hwcore.ARM_SCRIPT, hwcore.ARM_CLASSIC, hwcore.ARM_RL))

	def ratio(set_name, a, b, key="dmg_per_min"):
		x, y = out["sets"][set_name][a][key], out["sets"][set_name][b][key]
		return x / y if y > 1e-9 else float("nan")

	def exchange(set_name, arm):
		d = out["sets"][set_name][arm]
		return d["dmg_per_min"] / max(d["boss_dmg_per_min"], 1e-3)

	checks = {
		"rl_vs_script_dmg_high_alpha": ratio("habit_high", R, S),
		"rl_vs_script_dmg_low_alpha": ratio("habit_low", R, S),
		"classic_vs_script_dmg_low_alpha": ratio("habit_low", C, S),
		"rl_vs_classic_dmg_low_alpha": ratio("habit_low", R, C),
		"rl_vs_script_dmg_reference": ratio("reference", R, S),
	}
	checks["not_a_bully"] = bool(checks["rl_vs_script_dmg_high_alpha"] <= 1.0 + bully_tolerance)
	# The B0 thesis bar on held-out habit players: RL's advantage over the script at least the classic brain's.
	checks["thesis_like"] = bool(checks["rl_vs_script_dmg_low_alpha"] >= checks["classic_vs_script_dmg_low_alpha"])
	checks["aggression_floor"] = bool(all(out["sets"][n][R]["swings_per_min"] >= out["sets"][n][S]["swings_per_min"]
		for n in ("habit_low", "habit_high", "reference")))
	checks["net_exchange"] = bool(all(exchange(n, R) >= 0.95 * exchange(n, S) for n in ("habit_low", "habit_high", "reference")))
	checks["masher_lethal"] = bool(out["sets"]["masher"][R]["keeper_win_rate"] >= 1.0)
	# Players who adapt to the keeper (learning habit players): its edge over the script should survive them.
	checks["rl_vs_script_dmg_learners"] = ratio("learners", R, S)
	out["checks"] = checks
	return out


# =====================================================================================================================
# (4) The keepers (personalities) and the difficulty ladder
# =====================================================================================================================

MOVE_GROUPS = {"feints": (9, 10, 11), "heavies": (5, 6, 7, 8), "sweeps": (1, 2, 3, 4, 7, 8), "grab_killer": (12, 13),
	"evades": (16, 17, 18), "guard": (14, 15)}


def run_keepers(policy: "hwcore.Policy", seed: int, n_players: int = 16, sessions: int = 2, fights: int = 3,
		fight_seconds: int = 90) -> dict:
	"""The same network as each keeper (identity input) on the same held-out players: its style (move shares, style
	events per minute) and strength. The personalities should differ visibly: the Warden breaks guards, the Sage baits
	and evades, the Returned reads (READ counters)."""
	out = {}
	for ident, name in enumerate(hwcore.KEEPER_NAMES):
		sets = players_mod.eval_sets(seed=seed, n=n_players, fights=fights, identity=ident)
		runs = []
		for set_name in ("habit_low", "habit_mid", "reference"):
			for i, sp in enumerate(sets[set_name]):
				runs.append(hwcore.eval_sessions(hwcore.ARM_RL, sp, sessions, seed * 7919 + 101 * i + 7 + 31 * len(runs), policy=policy,
					immortal=True, fight_seconds=fight_seconds))
		d = hwcore.sum_eval_stats(runs)
		moves = np.array(d["boss_moves"], np.float64)
		attacks = max(moves[:hwcore.NUM_ATTACK_ACTIONS].sum(), 1.0)
		shares = {g: float(moves[list(idx)].sum() / (attacks if g not in ("evades", "guard") else max(moves.sum(), 1.0)))
			for g, idx in MOVE_GROUPS.items()}
		out[name] = {"dmg_per_min": d["dmg_per_min"], "taken_per_min": d["boss_dmg_per_min"], "swings_per_min": d["swings_per_min"],
			"hit_rate": d["hit_rate"], "reads_per_min": d["read_counters_per_min"], "mean_distance": d["mean_distance"],
			**{f"share_{g}": v for g, v in shares.items()},
			**{f"{e}_per_min": d[f"{e}_per_min"] for e in hwcore.STYLE_EVENTS}}
		print(f"[eval] keeper {name}: {d['dmg_per_min']:.0f} dmg/min, feints {shares['feints']:.2f}, heavies {shares['heavies']:.2f}, "
			f"evades {shares['evades']:.2f}, guard breaks/min {d['guard_breaks_per_min']:.2f}, bites/min {d['feint_bites_per_min']:.2f}, "
			f"reads/min {d['read_counters_per_min']:.2f}", flush=True)
	w, sg, r = out["Warden"], out["Sage"], out["Returned"]
	out["checks"] = {
		"warden_breaks_most_guards": bool(w["guard_breaks_per_min"] >= max(sg["guard_breaks_per_min"], r["guard_breaks_per_min"])),
		"sage_baits_most": bool(sg["feint_bites_per_min"] >= max(w["feint_bites_per_min"], r["feint_bites_per_min"])),
		"returned_reads_most": bool(r["reads_per_min"] >= max(w["reads_per_min"], sg["reads_per_min"])),
	}
	return out


def run_ladder(policy: "hwcore.Policy", seed: int, n_players: int = 16, sessions: int = 2, fights: int = 3,
		skills=(0.0, 0.4, 0.75, 1.0), temperatures=(1.0, 0.6, 0.0, 0.0), gaps=None) -> dict:
	"""The difficulty ladder (the game's Easy / Normal / Hard / Hellwalker presets: skill + sampling temperature, and
	Easy's breather, RL::EasySwingGap: frames from one attack's commit to the next opener) in MORTAL fights against held-out players and the
	reference bots: the keeper's win rate and damage should fall with the skill — and Easy should sit below the
	script on the same players."""
	if gaps is None:
		gaps = (hwcore.easy_swing_gap(),) + (0,) * (len(skills) - 1)
	if not (len(skills) == len(temperatures) == len(gaps)):
		raise ValueError("run_ladder: skills, temperatures and gaps need one entry per rung")
	# An hwrl.dll that predates the breather cannot run it: the rung runs without it and the check is skipped.
	breather_ok = hwcore.has_eval_gap()
	rows = []
	for sk, temp, want_gap in list(zip(skills, temperatures, gaps)) + [(None, None, 0)]:
		gap = int(want_gap) if breather_ok else 0
		runs = []
		for set_name in ("habit_mid", "habit_high", "reference"):
			specs = players_mod.eval_sets(seed=seed, n=n_players, fights=fights, keeper_skill=sk if sk is not None else 1.0)[set_name]
			for i, sp in enumerate(specs):
				arm = hwcore.ARM_SCRIPT if sk is None else hwcore.ARM_RL
				runs.append(hwcore.eval_sessions(arm, sp, sessions, seed * 7919 + 101 * i + 3 + 17 * len(runs),
					policy=policy if sk is not None else None, immortal=False, fight_seconds=180, temperature=temp or 0.0,
					min_swing_gap=gap))
		d = hwcore.sum_eval_stats(runs)
		rows.append({"skill": sk, "temperature": temp, "swing_gap": gap, "swing_gap_wanted": int(want_gap),
			"arm": "RL" if sk is not None else "script",
			"keeper_win_rate": d["keeper_win_rate"], "player_win_rate": d["keeper_loss_rate"], "dmg_per_min": d["dmg_per_min"],
			"taken_per_min": d["boss_dmg_per_min"], "swings_per_min": d["swings_per_min"], "fight_seconds": d["seconds"] / max(d["fights"], 1)})
		print(f"[eval] ladder {'script' if sk is None else f'skill {sk:.2f} T {temp:.1f} gap {gap}'}: keeper wins {d['keeper_win_rate']:.2f}, "
			f"player wins {d['keeper_loss_rate']:.2f}, {d['dmg_per_min']:.0f} dmg/min, taken {d['boss_dmg_per_min']:.0f}/min", flush=True)
	rl = [r for r in rows if r["arm"] == "RL"]
	checks = {"monotone_damage": bool(all(a["dmg_per_min"] <= b["dmg_per_min"] * 1.05 for a, b in zip(rl, rl[1:])))}
	if breather_ok or not any(g > 0 for g in gaps):
		checks["easy_below_script"] = bool(rl[0]["dmg_per_min"] <= rows[-1]["dmg_per_min"])
	else:
		print("[eval] ladder: this hwrl.dll predates the Easy breather (hwrl_eval_sessions_gap): Easy ran without it; "
			"easy_below_script skipped — rebuild with Tools\\RLBuild.bat", flush=True)
	return {"rows": rows, "checks": checks}


# =====================================================================================================================
# Report
# =====================================================================================================================

def _f(x, fmt="{:.3f}"):
	if x is None or (isinstance(x, float) and math.isnan(x)):
		return "—"
	return fmt.format(x)


def write_report(name: str, result: dict, out_dir: Path = REPORTS_DIR) -> Path:
	out_dir.mkdir(parents=True, exist_ok=True)
	with open(out_dir / f"{name}.json", "w") as fh:
		json.dump(result, fh, indent=2, default=lambda o: o.item() if hasattr(o, "item") else str(o))
	L = [f"# HellwalkerRL evaluation — {name}", "", f"- policy: `{result.get('hwrl') or '—'}`",
		f"- checkpoint: `{result.get('ckpt') or '—'}`", f"- date: {result.get('date')}", ""]
	b0 = result.get("b0")
	if b0 is not None:
		L += ["## B0 thesis (ThesisSim, RL keeper as the adaptive arm)", ""]
		if "skipped" in b0:
			L += [f"Skipped: {b0['skipped']}", ""]
		else:
			L += [f"Overall: **{b0.get('b0') or 'no verdict'}** (exit {b0.get('exit_code')}, {b0.get('seconds', 0):.0f} s, "
				f"reference swings/min {_f(b0.get('reference_spm'), '{:.1f}')})", "", "| check | result |", "|---|---|"]
			L += [f"| {k} | {v} |" for k, v in b0.get("checks", {}).items()]
			if b0.get("rows"):
				L += ["", "| profile | skill | script dmg/min | RL dmg/min | spm script | spm RL | xch script | xch RL | verdict |",
					"|---|---|---|---|---|---|---|---|---|"]
				for r in b0["rows"]:
					L.append(f"| {r['profile']} | {r['skill']} | {np.mean(r['script_dmg']):.1f} | {np.mean(r['adaptive_dmg']):.1f} | "
						f"{r['spm_script']:.1f} | {r['spm_adaptive']:.1f} | {r['xch_script']:.2f} | {r['xch_adaptive']:.2f} | {r['verdict']} |")
			L.append("")
	cv = result.get("curve")
	if cv is not None:
		L += ["## Adaptation curve (held-out habit players, greedy policy)", ""]
		if "skipped" in cv:
			L += [f"Skipped: {cv['skipped']}", ""]
		else:
			L += ["P(keeper attack hits) by the index k of the attack in the session:", "",
				"| k | low alpha (strong habits) | n | high alpha (near random) | n |", "|---|---|---|---|---|"]
			for bl, bh in zip(cv["low_alpha"]["bins"], cv["high_alpha"]["bins"]):
				L.append(f"| {bl['k']} | {_f(bl['p_hit'])} | {bl['n']} | {_f(bh['p_hit'])} | {bh['n']} |")
			L += ["", f"Rise (k 21-60 minus k 1-5): low alpha {_f(cv['low_alpha']['rise'])}, high alpha {_f(cv['high_alpha']['rise'])}.", ""]
			for nm in ("switch_low_alpha", "switch_high_alpha"):
				sw = cv[nm].get("switch")
				if not sw:
					continue
				L += [f"Around the scheduled habit switch ({nm}; offset 0 = first attack answered from table B, "
					f"{sw['sessions_with_switch']} sessions reached it):", "", "| offset | P(hit) | n |", "|---|---|---|"]
				L += [f"| {b['offset']} | {_f(b['p_hit'])} | {b['n']} |" for b in sw["bins"]]
				L += ["", f"before {_f(sw['before'])} → after {_f(sw['after'])} → recovered {_f(sw['recovered'])}", ""]
			L += ["| check | result |", "|---|---|"] + [f"| {k} | {'PASS' if v else 'FAIL'} |" for k, v in cv["checks"].items()] + [""]
	arms = result.get("arms")
	if arms is not None:
		L += ["## Arms on identical seeded players (C++)", ""]
		if "skipped" in arms:
			L += [f"Skipped: {arms['skipped']}", ""]
		else:
			for set_name, res in arms["sets"].items():
				L += [f"**{set_name}**", "", "| arm | dmg/min | taken/min | swings/min | hit rate | keeper wins | reads/min |",
					"|---|---|---|---|---|---|---|"]
				for arm, d in res.items():
					L.append(f"| {arm} | {d['dmg_per_min']:.1f} | {d['boss_dmg_per_min']:.1f} | {d['swings_per_min']:.1f} | "
						f"{d['hit_rate']:.3f} | {d['keeper_win_rate']:.2f} | {d['read_counters_per_min']:.2f} |")
				L.append("")
			if arms.get("checks"):
				L += ["| check | value |", "|---|---|"]
				L += [f"| {k} | {('PASS' if v else 'FAIL') if isinstance(v, bool) else _f(v)} |" for k, v in arms["checks"].items()]
				L.append("")
	kp = result.get("keepers")
	if kp is not None and "skipped" not in kp:
		L += ["## The three keepers (one network, the identity input)", "",
			"| keeper | dmg/min | taken/min | swings/min | hit rate | feints | heavies | evades | guard breaks/min | bites/min | reads/min |",
			"|---|---|---|---|---|---|---|---|---|---|---|"]
		for nm in hwcore.KEEPER_NAMES:
			d = kp[nm]
			L.append(f"| {nm} | {d['dmg_per_min']:.0f} | {d['taken_per_min']:.0f} | {d['swings_per_min']:.0f} | {d['hit_rate']:.2f} | "
				f"{d['share_feints']:.2f} | {d['share_heavies']:.2f} | {d['share_evades']:.2f} | {d['guard_breaks_per_min']:.2f} | "
				f"{d['feint_bites_per_min']:.2f} | {d['reads_per_min']:.2f} |")
		L += ["", "| check | result |", "|---|---|"] + [f"| {k} | {'PASS' if v else 'FAIL'} |" for k, v in kp["checks"].items()] + [""]
	ld = result.get("ladder")
	if ld is not None and "skipped" not in ld:
		L += ["## Difficulty ladder (mortal fights, held-out + reference players)", "",
			"| keeper | skill | sampling T | swing gap | keeper wins | player wins | dmg/min | taken/min | swings/min | fight s |",
			"|---|---|---|---|---|---|---|---|---|---|"]
		for r in ld["rows"]:
			L.append(f"| {r['arm']} | {_f(r['skill'], '{:.2f}')} | {_f(r['temperature'], '{:.1f}')} | {r.get('swing_gap', 0) or '—'} | {r['keeper_win_rate']:.2f} | "
				f"{r['player_win_rate']:.2f} | {r['dmg_per_min']:.0f} | {r['taken_per_min']:.0f} | {r['swings_per_min']:.0f} | {r['fight_seconds']:.0f} |")
		L += ["", "| check | result |", "|---|---|"] + [f"| {k} | {'PASS' if v else 'FAIL'} |" for k, v in ld["checks"].items()] + [""]
	rd = result.get("reading")
	if rd is not None and "skipped" not in rd:
		import habits as habits_mod
		L += ["## The reading test (pure habits; clean hits)", "", "| players who always… | first 5 attacks | attacks 21-60 | clean hit rate |",
			"|---|---|---|---|"]
		for g in habits_mod.GROUPS:
			e = rd[g]
			L.append(f"| {g} | {', '.join(f'{n} {v:.0%}' for n, v in e['early_mix'][:2])} | {', '.join(f'{n} {v:.0%}' for n, v in e['late_mix'][:2])} | "
				f"{e['early']['p_hit']:.2f} → {e['late']['p_hit']:.2f} |")
		sw = rd["switch_parry_to_stepleft"]
		L += ["", f"Move-mix divergence early {rd['js_early']:.2f} → late {rd['js_late']:.2f} bits; habit switch at swing #30: "
			f"{sw['pre_switch']['p_hit']:.2f} → {sw['post_switch']['p_hit']:.2f} → {sw['recovered']['p_hit']:.2f}.", ""]
	path = out_dir / f"{name}.md"
	path.write_text("\n".join(L), encoding="utf-8")
	return path


# =====================================================================================================================
# Main
# =====================================================================================================================

def main(argv=None) -> int:
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--ckpt", default=None, help="torch checkpoint (the curve uses it; exported to .hwrl if --hwrl is missing)")
	ap.add_argument("--hwrl", default=None, help="exported policy (b0 / arms; the curve falls back to it)")
	ap.add_argument("--name", default=None, help="report name (default eval-<timestamp>)")
	ap.add_argument("--only", default="b0,curve,arms,keepers,ladder,reading", help="comma-separated subset of b0,curve,arms,keepers,ladder,reading")
	ap.add_argument("--identity", type=int, default=-1, help="the keeper for b0 / curve / arms / reading (0 Warden, 1 Sage, 2 Returned; "
		"default: follows --script — script 1 is the Sage's, else the Warden)")
	ap.add_argument("--skill", type=float, default=1.0, help="the keeper's skill for b0 / arms")
	ap.add_argument("--sessions", type=int, default=4, help="ThesisSim --sessions (b0)")
	ap.add_argument("--script", type=int, default=0, help="Pathbreaker script (0 Warden, 1 Sage)")
	ap.add_argument("--curve-sessions", type=int, default=128)
	ap.add_argument("--curve-players", type=int, default=64)
	ap.add_argument("--switch-after", type=int, default=30)
	ap.add_argument("--arm-players", type=int, default=16)
	ap.add_argument("--arm-sessions", type=int, default=2)
	ap.add_argument("--seed", type=int, default=12345, help="held-out seed (never a training seed)")
	ap.add_argument("--device", default=None)
	ap.add_argument("--backend", choices=("dll", "mock"), default="dll", help="the curve's env (mock = pipeline test only)")
	args = ap.parse_args(argv)
	only = {s.strip() for s in args.only.split(",") if s.strip()}
	ident = args.identity if args.identity >= 0 else (1 if args.script == 1 else 0)
	bad = only - {"b0", "curve", "arms", "keepers", "ladder", "reading"}
	if bad:
		ap.error(f"unknown --only pieces: {sorted(bad)}")
	if not args.ckpt and not args.hwrl:
		ap.error("give --ckpt and/or --hwrl")
	name = args.name or f"eval-{time.strftime('%Y%m%d-%H%M%S')}"
	result: dict = {"name": name, "date": time.strftime("%Y-%m-%d %H:%M:%S"), "ckpt": args.ckpt, "hwrl": args.hwrl,
		"seed": args.seed}

	model = None
	if args.ckpt:
		from model import load_checkpoint
		model, extra = load_checkpoint(args.ckpt)
		result["trained"] = {k: extra.get(k) for k in ("stage", "run", "iteration", "decisions") if isinstance(extra, dict)}
	hwrl = args.hwrl
	if hwrl is None and model is not None and only & {"b0", "arms", "keepers", "ladder", "reading"}:
		try:
			from export import export_hwrl
			REPORTS_DIR.mkdir(parents=True, exist_ok=True)
			hwrl = export_hwrl(model, str(REPORTS_DIR / f"{name}.hwrl"), obs_layout_version=hwcore.OBS_LAYOUT_VERSION)
			result["hwrl"] = hwrl
		except Exception as e:
			print(f"[eval] could not export a .hwrl ({type(e).__name__}: {e}); b0 / arms need --hwrl")

	if "b0" in only:
		result["b0"] = run_b0(hwrl, args.sessions, args.script, identity=args.identity, skill=args.skill) if hwrl else {"skipped": "no .hwrl"}
		print(f"[eval] b0: {result['b0'].get('b0') or result['b0'].get('skipped') or result['b0'].get('error')}", flush=True)

	policy = None
	if hwrl and hwcore.available() and (only & {"arms", "keepers", "ladder", "reading"} or ("curve" in only and model is None)):
		try:
			policy = hwcore.Policy(hwrl)
		except Exception as e:
			print(f"[eval] hwcore.Policy({hwrl}) failed: {e}")

	if "curve" in only:
		if args.backend == "dll" and not hwcore.available():
			result["curve"] = {"skipped": "hwrl.dll not available (build it with RL\\native\\build.bat, or --backend mock)"}
		elif model is not None:
			import torch
			device = args.device or ("cuda" if torch.cuda.is_available() else "cpu")
			result["curve"] = run_curve(TorchEngine(model, device), args.seed, args.backend, args.curve_sessions,
				switch_after=args.switch_after, n_players=args.curve_players, identity=ident, keeper_skill=args.skill)
			result["curve"]["engine"] = f"torch ({device})"
		elif policy is not None:
			result["curve"] = run_curve(CppEngine(policy), args.seed, args.backend, args.curve_sessions,
				switch_after=args.switch_after, n_players=args.curve_players, identity=ident, keeper_skill=args.skill)
			result["curve"]["engine"] = "C++ FRLPolicy"
		else:
			result["curve"] = {"skipped": "no policy to run"}

	if "arms" in only:
		result["arms"] = run_arms(policy, args.seed, n_players=args.arm_players, sessions=args.arm_sessions,
			script_index=args.script, identity=ident, keeper_skill=args.skill) if policy is not None else \
			(run_arms(None, args.seed, args.arm_players, args.arm_sessions) if hwcore.available() else
			{"skipped": "hwrl.dll not available (build it with RL\\native\\build.bat)"})
	for piece, fn in (("keepers", lambda: run_keepers(policy, args.seed, args.arm_players, args.arm_sessions)),
			("ladder", lambda: run_ladder(policy, args.seed, args.arm_players, args.arm_sessions)),
			("reading", lambda: __import__("habits").reading_test(policy, 256, identity=ident))):
		if piece in only:
			result[piece] = fn() if policy is not None else {"skipped": "no policy"}
	if policy is not None:
		policy.free()

	path = write_report(name, result)
	print(f"[eval] report: {path} (+ .json)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
