"""The Unreal-vs-simulator gap: the same simulated player against the same keeper, fought in the real game (the UE
arena with autoplay, -HWParity) and in the simulator the keeper was trained in (hwrl_eval_sessions: the training env's
frame loop on the 2-D arena), compared metric by metric.

What can differ: in Unreal the fighters are real characters — positions come from character movement, collision and
root-motion displacement (steps), hit-stop holds the duel while the walk input keeps moving the player, the autoplay
walk is forward-only (the simulator also integrates lateral movement), and the geometry is mirrored (bMirrorY). The
frame logic, the brains and the bots are the same code. What is controlled: the policy file, the bot kind and skill,
the keeper's skill / temperature / identity (read back from the game's own records), the start distance (drawn 450-800
as the simulator draws it) and the 180 s cap.

One UE launch = one session (memory carried across its fights, dropped on quit); -HWSeedBase separates launches. Rates
are compared per session (ratio of sums) with bootstrap 95% intervals; a metric is flagged when the gap is both larger
than the tolerance and outside the intervals.

  Tools\\Parity.bat
  python parity.py --bots rhythm:0.7,habitual:0.7 --arms rl,script --sessions 6 --fights 3
  python parity.py --compare-only RL\\reports\\parity_raw   (re-analyse saved UE records)
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List

import numpy as np

import hwcore

ROOT = Path(__file__).resolve().parent.parent
UE_EXE = Path(os.environ.get("HW_UE", r"D:\Shadow\Epic Games\UE_5.8")) / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
UPROJECT = ROOT / "HellwalkerRL.uproject"
SHIPPED = ROOT / "Content" / "HellwalkerRL" / "RL" / "hellwalker_rl.hwrl"
REPORTS = ROOT / "RL" / "reports"

# The UE autoplay names (hw.Autoplay / -HWAutoplay) -> the C ABI's reference kinds.
BOTS = {"masher": hwcore.KIND_MASHER, "turtle": hwcore.KIND_TURTLE, "habitual": hwcore.KIND_HABITUAL, "varied": hwcore.KIND_VARIED,
	"dodger": hwcore.KIND_DODGER_LEFT, "rhythm": hwcore.KIND_RHYTHM_PARRIER}

METRICS = [
	# name, label, tolerance (relative), fn(sums) -> value
	("keeper_dmg_min", "keeper damage / min", 0.15, lambda s: 60.0 * s["player_dmg_taken"] / max(s["seconds"], 1e-6)),
	("player_dmg_min", "player damage / min", 0.20, lambda s: 60.0 * s["boss_dmg_taken"] / max(s["seconds"], 1e-6)),
	("keeper_spm", "keeper swings / min", 0.10, lambda s: 60.0 * s["boss_swings"] / max(s["seconds"], 1e-6)),
	("player_spm", "player swings / min", 0.15, lambda s: 60.0 * s["player_swings"] / max(s["seconds"], 1e-6)),
	("hit_rate", "keeper hit rate", 0.10, lambda s: s["boss_hits"] / max(s["boss_swings"], 1)),
	("whiff_rate", "keeper whiff rate", 0.25, lambda s: s["boss_whiffs"] / max(s["boss_swings"], 1)),
	("blocked_rate", "blocked rate", 0.20, lambda s: s["boss_blocked"] / max(s["boss_swings"], 1)),
	("parried_rate", "parried rate", 0.20, lambda s: s["boss_parried"] / max(s["boss_swings"], 1)),
	("reads_min", "read counters / min", 0.20, lambda s: 60.0 * s["read_counters"] / max(s["seconds"], 1e-6)),
	("fight_s", "fight length (s)", 0.15, lambda s: s["seconds"] / max(s["fights"], 1)),
	("player_death_rate", "player deaths / fight", 0.10, lambda s: s["player_deaths"] / max(s["fights"], 1)),
	("distance", "distance at keeper commits", 0.10, lambda s: s["distance_sum"] / max(s["distance_samples"], 1)),
]


# ---------------------------------------------------------------------------------------------------------------------
# Unreal
# ---------------------------------------------------------------------------------------------------------------------

def run_ue_session(out: Path, arm: str, bot: str, bot_skill: float, fights: int, seed_base: int, policy: Path,
		identity: int, timeout: int, fast: bool, extra: List[str] = (), difficulty: str = "Hellwalker") -> int:
	"""One UE launch: `fights` encounters in the arena, autoplay `bot`, one JSON line each appended to `out`."""
	args = [str(UE_EXE), str(UPROJECT), "/Game/HellwalkerRL/Maps/L_Arena", "-game", "-windowed", "-ResX=640", "-ResY=360",
		"-HWNoSave", f"-HWTier={'Pathbreaker' if arm == 'script' else 'Hellwalker'}", f"-HWAutoplay={bot}",
		f"-HWAutoplaySkill={bot_skill}", "-HWAutoStart", f"-HWEncounters={fights}", f"-HWParity={out}",
		f"-HWSeedBase={seed_base}", f"-HWPolicy={policy}", f"-HWKeeper={identity}", f"-HWDifficulty={difficulty}",
		"-nosound", "-unattended", "-nosplash",
		f"-log=Parity_{seed_base}.log"]
	if fast:
		args += ["-benchmark", "-fps=60"]  # fixed 1/60 s steps as fast as the machine goes (the duel steps one frame per tick)
	args += list(extra)                     # experiments: -HWMoveAccel= -HWMoveBraking= -HWNoHitstop
	before = count_lines(out)
	t0 = time.time()
	try:
		subprocess.run(args, timeout=timeout, check=False, env={**os.environ, "MSYS_NO_PATHCONV": "1"})
	except subprocess.TimeoutExpired:
		print(f"[parity] UE session {seed_base} timed out after {timeout} s", flush=True)
	got = count_lines(out) - before
	print(f"[parity] UE {arm} vs {bot} {bot_skill}: session {seed_base}: {got}/{fights} fights in {time.time() - t0:.0f} s", flush=True)
	return got


def count_lines(p: Path) -> int:
	if not p.exists():
		return 0
	with open(p, encoding="utf-8") as fh:
		return sum(1 for line in fh if line.strip())


def load_ue(p: Path) -> List[dict]:
	recs = []
	if p.exists():
		with open(p, encoding="utf-8") as fh:
			for line in fh:
				line = line.strip()
				if line:
					recs.append(json.loads(line))
	return recs


def ue_session_sums(recs: List[dict]) -> List[dict]:
	"""Records grouped by launch (seed_base) -> the same sums hwrl_eval_sessions returns."""
	by: Dict[int, List[dict]] = {}
	for r in recs:
		by.setdefault(int(r["seed_base"]), []).append(r)
	out = []
	for _, rs in sorted(by.items()):
		s = {"fights": len(rs), "seconds": sum(r["frames"] for r in rs) / 60.0,
			"player_dmg_taken": sum(r["player_dmg_taken"] for r in rs), "boss_dmg_taken": sum(r["boss_dmg_taken"] for r in rs),
			"boss_swings": sum(r["boss_swings"] for r in rs), "player_swings": sum(r["player_swings"] for r in rs),
			"boss_hits": sum(r["boss_hits"] for r in rs), "boss_whiffs": sum(r["boss_whiffs"] for r in rs),
			"boss_blocked": sum(r["boss_blocked"] for r in rs), "boss_parried": sum(r["boss_parried"] for r in rs),
			"read_counters": sum(r["read_counters"] for r in rs), "player_deaths": sum(1 for r in rs if r["player_died"]),
			"boss_deaths": sum(1 for r in rs if r["boss_died"]), "timeouts": sum(1 for r in rs if r["timeout"]),
			"distance_sum": sum(r["distance_sum"] for r in rs), "distance_samples": sum(r["distance_samples"] for r in rs),
			"boss_moves": np.sum([r["boss_moves"] for r in rs], axis=0).tolist(),
			"real_seconds": sum(r.get("real_seconds", 0.0) for r in rs)}
		diags = [r["bot_diag"] for r in rs if "bot_diag" in r]
		if diags:
			s["bot_diag"] = {k: float(sum(d[k] for d in diags)) for k in diags[0]}
		out.append(s)
	return out


# ---------------------------------------------------------------------------------------------------------------------
# Simulator
# ---------------------------------------------------------------------------------------------------------------------

def sim_session_sums(arm: str, bot: str, bot_skill: float, fights: int, sessions: int, policy, identity: int, skill: float,
		temperature: float, gap: int, seed: int) -> List[dict]:
	spec = hwcore.make_spec(kind=BOTS[bot], skill=bot_skill, fights_in_session=fights, keeper_skill=skill, keeper_identity=identity)
	out = []
	for i in range(sessions):
		d = hwcore.eval_sessions(hwcore.ARM_RL if arm == "rl" else hwcore.ARM_SCRIPT, spec, 1, seed * 1000003 + 7919 * i,
			policy=policy if arm == "rl" else None, immortal=False, fight_seconds=180, temperature=temperature,
			min_swing_gap=gap, bot_diag=hwcore.has_eval_gap())
		raw = d.get("raw", d)
		out.append({k: raw[k] for k in ("fights", "seconds", "player_dmg_taken", "boss_dmg_taken", "boss_swings", "player_swings",
			"boss_hits", "boss_whiffs", "boss_blocked", "boss_parried", "read_counters", "player_deaths", "boss_deaths", "timeouts",
			"distance_sum", "distance_samples", "boss_moves")})
		if "bot_diag" in d:
			out[-1]["bot_diag"] = {k: float(v) for k, v in d["bot_diag"].items()}
	return out


# ---------------------------------------------------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------------------------------------------------

def pooled(sums: List[dict]) -> dict:
	keys = [k for k in sums[0] if k not in ("boss_moves", "bot_diag") and not isinstance(sums[0][k], list)]
	p = {k: float(sum(s[k] for s in sums)) for k in keys}
	p["boss_moves"] = np.sum([s["boss_moves"] for s in sums], axis=0)
	if all("bot_diag" in s for s in sums):
		p["bot_diag"] = {k: float(sum(s["bot_diag"][k] for s in sums)) for k in sums[0]["bot_diag"]}
	return p


# The simulated player's own view (FBotDiag): share of its frames, or events per minute of its frames.
BOT_METRICS = [
	("actionable", "frames it could act", "share"), ("in_range", "frames within its attack range (185)", "share"),
	("in_range_actionable", "... in range AND able to act", "share"), ("boss_open", "frames the keeper was open (punish window)", "share"),
	("boss_open_in_range", "... open AND in range", "share"), ("boss_swinging", "frames a keeper swing was in flight", "share"),
	("defence_pending", "frames an answer was pending", "share"), ("walk_fwd", "frames walking in", "share"),
	("walk_back", "frames backing off", "share"), ("guarding", "frames guarding", "share"),
	("punish_starts", "punish strings begun / min", "rate"), ("aggro_starts", "aggression strings begun / min", "rate"),
	("response_attacks", "keeper swings answered by swinging / min", "rate"), ("attack_commits", "attacks committed / min", "rate"),
	("distance_sum", "mean distance (every frame)", "mean"),
]


def bot_value(kind: str, key: str):
	def fn(s):
		d = s.get("bot_diag")
		if not d or d["frames"] <= 0:
			return float("nan")
		if kind == "share":
			return d[key] / d["frames"]
		if kind == "rate":
			return 3600.0 * d[key] / d["frames"]
		return d[key] / d["frames"]
	return fn


def bootstrap(sums: List[dict], fn, n: int = 2000, seed: int = 1):
	"""Pooled value and a 95% interval, resampling whole sessions."""
	rng = np.random.default_rng(seed)
	val = fn(pooled(sums))
	if len(sums) < 2:
		return val, float("nan"), float("nan")
	bs = []
	for _ in range(n):
		idx = rng.integers(0, len(sums), len(sums))
		bs.append(fn(pooled([sums[i] for i in idx])))
	return val, float(np.percentile(bs, 2.5)), float(np.percentile(bs, 97.5))


def js_divergence(p, q) -> float:
	p = np.asarray(p, dtype=float) + 1e-9
	q = np.asarray(q, dtype=float) + 1e-9
	p, q = p / p.sum(), q / q.sum()
	m = 0.5 * (p + q)
	return float(0.5 * np.sum(p * np.log2(p / m)) + 0.5 * np.sum(q * np.log2(q / m)))


def compare(ue: List[dict], sim: List[dict]) -> dict:
	rows = []
	for name, label, tol, fn in METRICS:
		u, ulo, uhi = bootstrap(ue, fn)
		s, slo, shi = bootstrap(sim, fn, seed=2)
		rel = (u - s) / abs(s) if abs(s) > 1e-9 else float("nan")
		overlap = not (math.isnan(ulo) or math.isnan(slo)) and not (uhi < slo or shi < ulo)
		ok = (abs(rel) <= tol) if not math.isnan(rel) else (abs(u - s) < 1e-6)
		rows.append({"metric": name, "label": label, "ue": u, "ue_lo": ulo, "ue_hi": uhi, "sim": s, "sim_lo": slo, "sim_hi": shi,
			"rel_gap": rel, "tolerance": tol, "intervals_overlap": overlap, "consistent": bool(ok or overlap)})
	bot_rows = []
	if all("bot_diag" in s for s in ue) and all("bot_diag" in s for s in sim):
		for key, label, kind in BOT_METRICS:
			u, ulo, uhi = bootstrap(ue, bot_value(kind, key))
			s, slo, shi = bootstrap(sim, bot_value(kind, key), seed=2)
			bot_rows.append({"metric": key, "label": label, "ue": u, "ue_lo": ulo, "ue_hi": uhi, "sim": s, "sim_lo": slo, "sim_hi": shi,
				"rel_gap": (u - s) / abs(s) if abs(s) > 1e-9 else float("nan")})
	pu, ps = pooled(ue)["boss_moves"], pooled(sim)["boss_moves"]
	mix = {"js_bits": js_divergence(pu, ps),
		"ue_top": top_moves(pu), "sim_top": top_moves(ps)}
	return {"rows": rows, "bot_rows": bot_rows, "move_mix": mix, "ue_sessions": len(ue), "sim_sessions": len(sim),
		"ue_fights": int(sum(s["fights"] for s in ue)), "sim_fights": int(sum(s["fights"] for s in sim))}


def top_moves(counts, k: int = 4):
	counts = np.asarray(counts, dtype=float)
	tot = max(counts.sum(), 1.0)
	names = hwcore.action_names()
	order = np.argsort(-counts)[:k]
	return [(names[i], float(counts[i] / tot)) for i in order if counts[i] > 0]


def validate(recs: List[dict], arm: str, bot: str, bot_skill: float, identity: int, fights: int) -> List[str]:
	"""Problems that make a cell's Unreal records not comparable with the simulator run built for it."""
	problems = []
	want_bot = hwcore.KIND_NAMES[BOTS[bot]]
	for r in recs:
		if r.get("arm") != arm:
			problems.append(f"a record played the {r.get('arm')} arm (the RL model may have failed to load)")
		if int(r.get("identity", -1)) != identity:
			problems.append(f"a record's keeper is {r.get('identity')}, not {identity}")
		if r.get("bot") != want_bot or abs(float(r.get("bot_skill", -1)) - bot_skill) > 1e-3 or not r.get("autoplay", False):
			problems.append(f"a record's player is {r.get('bot')} {r.get('bot_skill')} (autoplay {r.get('autoplay')})")
	configs = {(round(float(r.get("skill", 1)), 3), round(float(r.get("temperature", 0)), 3), int(r.get("swing_gap", 0))) for r in recs}
	if len(configs) > 1:
		problems.append(f"the keeper's skill / temperature / breather changed between fights {sorted(configs)} (Adaptive?)")
	per_session: Dict[int, int] = {}
	for r in recs:
		per_session[int(r["seed_base"])] = per_session.get(int(r["seed_base"]), 0) + 1
	short = [k for k, v in per_session.items() if v != fights]
	if short:
		problems.append(f"{len(short)} session(s) without exactly {fights} fights")
	return sorted(set(problems))


def write_report(name: str, cells: List[dict], missing: List[dict] = ()) -> Path:
	REPORTS.mkdir(parents=True, exist_ok=True)
	with open(REPORTS / f"{name}.json", "w") as fh:
		json.dump(cells, fh, indent=2, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
	L = [f"# Unreal vs simulator — {name}", "", f"- date: {time.strftime('%Y-%m-%d %H:%M:%S')}", "",
		"Same policy, bot, keeper settings, start-distance distribution and 180 s cap; Unreal runs the arena with autoplay "
		"(characters, collision, hit-stop), the simulator the training env's 2-D arena. Pooled per-session rates with "
		"bootstrap 95% intervals; **gap** = (UE - sim) / sim. A metric is flagged when the gap exceeds its tolerance "
		"AND the intervals do not overlap.", ""]
	for c in cells:
		cmp = c["compare"]
		L += [f"## {c['arm'].upper()} vs {c['bot']} {c['bot_skill']:.1f} (keeper {c['identity']}, skill {c['skill']:.2f}, "
			f"T {c['temperature']:.1f}, gap {c['gap']}) — UE {cmp['ue_fights']} fights / {cmp['ue_sessions']} sessions, "
			f"sim {cmp['sim_fights']} / {cmp['sim_sessions']}", "",
			"| metric | Unreal | simulator | gap | tolerance | |", "|---|---|---|---|---|---|"]
		for r in cmp["rows"]:
			L.append(f"| {r['label']} | {r['ue']:.3g} [{r['ue_lo']:.3g}, {r['ue_hi']:.3g}] | {r['sim']:.3g} [{r['sim_lo']:.3g}, "
				f"{r['sim_hi']:.3g}] | {r['rel_gap']:+.0%} | ±{r['tolerance']:.0%} | {'ok' if r['consistent'] else '**GAP**'} |")
		mm = cmp["move_mix"]
		L += ["", f"Keeper move mix: JS divergence {mm['js_bits']:.3f} bits; Unreal "
			+ ", ".join(f"{n} {v:.0%}" for n, v in mm["ue_top"]) + "; simulator " + ", ".join(f"{n} {v:.0%}" for n, v in mm["sim_top"]),
			""]
		if cmp.get("bot_rows"):
			L += ["The simulated player's own view (the same bot code on both sides; FBotDiag):", "",
				"| | Unreal | simulator | gap |", "|---|---|---|---|"]
			for r in cmp["bot_rows"]:
				L.append(f"| {r['label']} | {r['ue']:.3g} [{r['ue_lo']:.3g}, {r['ue_hi']:.3g}] | {r['sim']:.3g} [{r['sim_lo']:.3g}, "
					f"{r['sim_hi']:.3g}] | {r['rel_gap']:+.0%} |")
			L.append("")
		if c.get("ue_real_seconds"):
			L += [f"Unreal wall time {c['ue_real_seconds']:.0f} s for {cmp['ue_fights']} fights.", ""]
	flagged = [(c, r) for c in cells for r in c["compare"]["rows"] if not r["consistent"]]
	L += ["## Summary", ""]
	if missing:
		L += [f"{len(missing)} cell(s) not compared:", ""] + [f"- {m['arm']} vs {m['bot']} {m['bot_skill']:.1f}: {m['why']}" for m in missing] + [""]
	if not flagged and missing:
		L += ["Every compared metric is within tolerance or inside the intervals — but not every cell was compared.", ""]
	elif not flagged:
		L += ["Every metric is within tolerance or inside the intervals in every cell: the simulator the keeper was trained in "
			"predicts the game.", ""]
	else:
		L += [f"{len(flagged)} flagged metric(s):", ""] + [
			f"- {c['arm']} vs {c['bot']} {c['bot_skill']:.1f}: {r['label']} — Unreal {r['ue']:.3g}, simulator {r['sim']:.3g} ({r['rel_gap']:+.0%})"
			for c, r in flagged] + [""]
	p = REPORTS / f"{name}.md"
	p.write_text("\n".join(L), encoding="utf-8")
	return p


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--policy", default=str(SHIPPED))
	ap.add_argument("--bots", default="rhythm:0.7,habitual:0.7,varied:0.5", help="kind:skill,... (masher turtle habitual varied dodger rhythm)")
	ap.add_argument("--arms", default="rl,script")
	ap.add_argument("--identity", type=int, default=0, help="the keeper (0 Warden: health x1, the arena's boss)")
	ap.add_argument("--sessions", type=int, default=6, help="UE launches per cell")
	ap.add_argument("--fights", type=int, default=3, choices=(1, 2, 3, 4), help="fights per session (memory carried; the simulator's sessions hold 1-4)")
	ap.add_argument("--difficulty", default="Hellwalker", help="-HWDifficulty for the game (Easy Normal Hard Hellwalker; not Adaptive)")
	ap.add_argument("--sim-sessions", type=int, default=200)
	ap.add_argument("--seed", type=int, default=11)
	ap.add_argument("--timeout", type=int, default=900, help="seconds per UE launch")
	ap.add_argument("--realtime", action="store_true", help="run UE at real-time 60 fps (default: -benchmark fixed steps)")
	ap.add_argument("--raw-dir", default=str(REPORTS / "parity_raw"))
	ap.add_argument("--compare-only", action="store_true", help="skip UE; analyse the records already in --raw-dir")
	ap.add_argument("--name", default="parity")
	ap.add_argument("--ue-args", default="", help="extra game arguments in one string, e.g. \"-HWMoveBraking=20000 -HWNoHitstop\"")
	ap.add_argument("--ue-arg", action="append", default=[], help="one extra game argument (repeatable): --ue-arg=-HWNoHitstop")
	args = ap.parse_args()
	if args.difficulty.lower() == "adaptive":
		ap.error("--difficulty Adaptive changes the keeper between fights: the simulator side cannot follow it")

	raw = Path(args.raw_dir).resolve()  # the game resolves a relative path against its own launch folder
	raw.mkdir(parents=True, exist_ok=True)
	policy_path = Path(args.policy).resolve()
	policy = hwcore.Policy(str(policy_path))
	cells, missing = [], []
	extra = args.ue_args.split() + list(args.ue_arg)
	for bspec in args.bots.split(","):
		bot, sk = bspec.split(":")
		bot_skill = float(sk)
		for arm in args.arms.split(","):
			out = raw / f"{arm}_{bot}_{bot_skill:.2f}.jsonl"
			if not args.compare_only:
				if out.exists():
					out.unlink()
				for i in range(args.sessions):
					run_ue_session(out, arm, bot, bot_skill, args.fights, 100000 * (i + 1) + args.seed, policy_path, args.identity,
						args.timeout, not args.realtime, extra, args.difficulty)
			recs = load_ue(out)
			if not recs:
				print(f"[parity] no Unreal records for {arm} vs {bot} {bot_skill} ({out})", flush=True)
				missing.append({"arm": arm, "bot": bot, "bot_skill": bot_skill, "why": "no Unreal records"})
				continue
			problems = validate(recs, arm, bot, bot_skill, args.identity, args.fights)
			if problems:
				print(f"[parity] {arm} vs {bot} {bot_skill}: not compared — " + "; ".join(problems), flush=True)
				missing.append({"arm": arm, "bot": bot, "bot_skill": bot_skill, "why": "; ".join(problems)})
				continue
			r0 = recs[0]
			skill, temp, gap = float(r0.get("skill", 1.0)), float(r0.get("temperature", 0.0)), int(r0.get("swing_gap", 0))
			ue = ue_session_sums(recs)
			sim = sim_session_sums(arm, bot, bot_skill, args.fights, args.sim_sessions, policy, args.identity, skill, temp, gap, args.seed)
			cmp = compare(ue, sim)
			cells.append({"arm": arm, "bot": bot, "bot_skill": bot_skill, "identity": args.identity, "skill": skill, "temperature": temp,
				"gap": gap, "compare": cmp, "ue_real_seconds": sum(s["real_seconds"] for s in ue)})
			bad = [r["label"] for r in cmp["rows"] if not r["consistent"]]
			print(f"[parity] {arm} vs {bot} {bot_skill}: {cmp['ue_fights']} UE fights, {cmp['sim_fights']} sim fights; "
				f"move-mix JS {cmp['move_mix']['js_bits']:.3f}; flagged: {', '.join(bad) or 'none'}", flush=True)
	if not cells:
		return 1
	p = write_report(args.name, cells, missing)
	print(f"[parity] report: {p}", flush=True)
	return 0


if __name__ == "__main__":
	sys.exit(main())
