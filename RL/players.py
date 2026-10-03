"""HellwalkerRL — the opponent population and its curriculum (RL.md §6, DESIGN.md §9).

The policy can only learn to read the kinds of players it meets, so the population IS the curriculum:

* reference bots (HWSim kinds 0-5: Masher, Turtle, Habitual, Varied, DodgerLeft, RhythmParrier), skill U(0.2, 1), in
  ~20% of sessions — the B0 instrument, so the keeper never forgets the players it is graded against;
* procedural habit players (kind 6) otherwise — a response table per boss swing class, each row ~ Dirichlet(alpha * 1)
  with alpha from the curriculum band (band 0 strong habits ... band 3 near random), random timing / reads / noise, and
  a mid-session habit switch with a band-dependent probability (the keeper must notice a stale read);
* band unlocking: start with band 0; unlock the next band once the keeper wins >= 60% of the last ~2000 fights against
  the NEWEST band; sample bands proportional to (1, 1, 1.5, 1.5) over the unlocked ones so old habits stay covered;
* learning players (layout 3): ~35% of habit players learn which answers work against THIS keeper (learn_rate > 0) —
  a keeper that leans on one counter teaches them to stop walking into it;
* exploiters (RL-4): trained player networks registered by the league (train.py --league) join at exploit_fraction;
* every session also fixes the KEEPER's side: its identity (uniform over the three) and skill (half the sessions at
  full strength — the tier B0 grades — the rest uniform in [0, 1]), so one network learns every keeper and difficulty.

Held-out players come from a separate seed stream (never the training stream), so evaluation meets players the keeper
has not seen. Every spec carries a `tag` (echoed back by the C++ env in each step result) naming its source and band:
per-band statistics and the curriculum are keyed on it (see tag_group).
"""

from __future__ import annotations

from collections import deque
from typing import Dict, List, Optional, Sequence

import numpy as np

import hwcore
from hwcore import HWRLPlayerSpec, make_spec

# ---- the population's knobs (DESIGN.md §9) --------------------------------------------------------------------------
NUM_BANDS = 4
BAND_ALPHA = ((0.05, 0.2), (0.2, 1.0), (1.0, 5.0), (5.0, 50.0))   # Dirichlet concentration range per band
BAND_SWITCH_P = (0.1, 0.25, 0.35, 0.35)                            # P(habit switch in a session) per band
BAND_WEIGHTS = (1.0, 1.0, 1.5, 1.5)                                # sampling weights over unlocked bands
REF_FRACTION = 0.2
SWITCH_AFTER_RANGE = (15, 60)       # boss swings in the session before table B replaces table A (inclusive)
FIGHTS_RANGE = (1, 4)               # fights_in_session ~ U{1..4}
UNLOCK_WIN_RATE = 0.6
UNLOCK_WINDOW = 2000

# Timing / behaviour ranges of the habit players: name -> (low, high), uniform.
HABIT_RANGES = {
	"parry_aim": (1.0, 7.0),
	"step_aim": (3.0, 14.0),
	"timing_sigma": (0.8, 4.5),
	"react_mean": (10.0, 25.0),
	"react_sigma": (1.5, 4.0),
	"feint_read": (0.0, 0.6),
	"killer_read": (0.3, 0.95),
	"punish_rate": (0.2, 0.9),
	"aggro_rate": (0.05, 0.6),
	"heavy_rate": (0.05, 0.4),
	"switch_rate": (0.0, 0.3),
	"preferred_range": (140.0, 230.0),
}
NOISE_RANGE = (0.0, 0.1)
SKILL_RANGE = (0.2, 1.0)
LEARNER_FRACTION = 0.35            # habit players that learn (FBotProfile::LearnRate > 0)
LEARN_RATE_RANGE = (0.05, 0.4)
LEARN_TEMP_RANGE = (0.2, 0.6)
KEEPER_FULL_SKILL_P = 0.5          # sessions with the keeper at skill 1 (the rest: skill ~ U(0, 1))
EXPLOIT_FRACTION = 0.12            # sessions against registered exploiters (once there are any)

# ---- tags: which source a spec came from (echoed back per step by the C++ env) ---------------------------------------
TAG_BAND = 0            # + band: procedural habit players, TRAINING stream
TAG_REF = 10            # + kind: reference bots, training stream
TAG_HELDOUT_BAND = 20   # + band: procedural habit players, HELD-OUT stream
TAG_HELDOUT_REF = 30    # + kind: reference bots in the evaluation sets
TAG_FIXED = 40          # + index: named fixed sets ("habitual" = 40)
TAG_EXPLOITER = 100     # + policy id: exploiter networks (kind 7)

# Seed streams (SeedSequence spawn keys): the held-out stream never shares state with the training one.
STREAM_TRAIN, STREAM_HELDOUT = 0, 1


def tag_group(tag: int) -> str:
	"""Statistics key for a tag: band0..band3, ref, heldout0..heldout3, eval_ref, fixed."""
	tag = int(tag)
	if TAG_BAND <= tag < TAG_BAND + NUM_BANDS:
		return f"band{tag - TAG_BAND}"
	if TAG_REF <= tag < TAG_REF + hwcore.NUM_REFERENCE_KINDS:
		return "ref"
	if TAG_HELDOUT_BAND <= tag < TAG_HELDOUT_BAND + NUM_BANDS:
		return f"heldout{tag - TAG_HELDOUT_BAND}"
	if TAG_HELDOUT_REF <= tag < TAG_HELDOUT_REF + hwcore.NUM_REFERENCE_KINDS:
		return "eval_ref"
	if tag >= TAG_EXPLOITER:
		return "exploit"
	return "fixed"


def tag_name(tag: int) -> str:
	tag = int(tag)
	if TAG_REF <= tag < TAG_REF + hwcore.NUM_REFERENCE_KINDS:
		return "ref." + hwcore.KIND_NAMES[tag - TAG_REF]
	if TAG_HELDOUT_REF <= tag < TAG_HELDOUT_REF + hwcore.NUM_REFERENCE_KINDS:
		return "eval_ref." + hwcore.KIND_NAMES[tag - TAG_HELDOUT_REF]
	if tag >= TAG_FIXED:
		return f"fixed.{tag - TAG_FIXED}"
	return tag_group(tag)


def band_of_tag(tag: int) -> int:
	"""The habit band of a tag (training or held-out), -1 for anything else."""
	tag = int(tag)
	if TAG_BAND <= tag < TAG_BAND + NUM_BANDS:
		return tag - TAG_BAND
	if TAG_HELDOUT_BAND <= tag < TAG_HELDOUT_BAND + NUM_BANDS:
		return tag - TAG_HELDOUT_BAND
	return -1


GROUPS_TRAIN = tuple(f"band{b}" for b in range(NUM_BANDS)) + ("ref", "exploit")


def make_rng(seed: int, stream: int, *extra: int) -> np.random.Generator:
	"""A generator on its own stream: (seed, stream, extra...) never overlap another stream's draws."""
	return np.random.Generator(np.random.PCG64(np.random.SeedSequence(int(seed) & 0xFFFFFFFFFFFFFFFF,
		spawn_key=(int(stream),) + tuple(int(e) for e in extra))))


# =====================================================================================================================
# Generators
# =====================================================================================================================

_HABIT_KEYS = tuple(HABIT_RANGES)
_HABIT_LOW = np.array([HABIT_RANGES[k][0] for k in _HABIT_KEYS])
_HABIT_HIGH = np.array([HABIT_RANGES[k][1] for k in _HABIT_KEYS])


def _dirichlet_rows(rng: np.random.Generator, alpha: float, rows: int = 4, k: int = 8) -> np.ndarray:
	"""rows x k response weights, each row ~ Dirichlet(alpha * 1) (one vectorised draw: sampling runs on the rollout
	path every time a session ends). Tiny alphas can underflow to an all-zero row: that limit is "always the same
	answer", so such a row falls back to a one-hot."""
	p = rng.dirichlet(np.full(k, alpha), size=rows)
	bad = ~np.isfinite(p).all(axis=1) | (p.sum(axis=1) <= 0.0)
	if bad.any():
		p[bad] = 0.0
		p[np.flatnonzero(bad), rng.integers(k, size=int(bad.sum()))] = 1.0
	return p.astype(np.float32)


def habit_table_entropy(table: np.ndarray) -> float:
	"""Mean row entropy (bits) of a [4, 8] habit table: 0 = one fixed answer per swing class, 3 = uniform."""
	t = np.asarray(table, np.float64)
	t = t / np.maximum(t.sum(-1, keepdims=True), 1e-12)
	with np.errstate(divide="ignore", invalid="ignore"):
		h = -np.where(t > 0, t * np.log2(t), 0.0).sum(-1)
	return float(h.mean())


def sample_alpha(rng: np.random.Generator, band: int) -> float:
	"""Log-uniform in the band's range: the bands span decades of concentration."""
	lo, hi = BAND_ALPHA[band]
	return float(np.exp(rng.uniform(np.log(lo), np.log(hi))))


def keeper_side(rng: np.random.Generator, full_skill_p: float = KEEPER_FULL_SKILL_P) -> dict:
	"""The keeper's side of a training session: identity uniform, skill 1 with full_skill_p, else U(0, 1)."""
	identity = int(rng.integers(hwcore.NUM_KEEPERS))
	skill = 1.0 if rng.random() < full_skill_p else float(rng.uniform(0.0, 1.0))
	return {"keeper_skill": skill, "keeper_identity": identity}


def habit_player(rng: np.random.Generator, band: int, *, tag: int, fights: Optional[int] = None,
		switch: Optional[bool] = None, switch_after: Optional[int] = None, alpha: Optional[float] = None,
		learner: Optional[bool] = None) -> HWRLPlayerSpec:
	"""A procedural habit player (kind 6). switch None = with the band's probability; switch_after None = U{15..60};
	learner None = with LEARNER_FRACTION (draws come after the table / timing draws, so the old streams are a prefix)."""
	if not 0 <= band < NUM_BANDS:
		raise ValueError(f"band {band}")
	a = sample_alpha(rng, band) if alpha is None else float(alpha)
	table_a = _dirichlet_rows(rng, a)
	if switch is None:
		switch = bool(rng.random() < BAND_SWITCH_P[band])
	if switch:
		table_b = _dirichlet_rows(rng, a)   # a fresh draw from the same band: a new habit, equally strong
		after = int(rng.integers(SWITCH_AFTER_RANGE[0], SWITCH_AFTER_RANGE[1] + 1)) if switch_after is None else int(switch_after)
	else:
		table_b = table_a.copy()
		after = -1
	overrides = dict(zip(_HABIT_KEYS, rng.uniform(_HABIT_LOW, _HABIT_HIGH).tolist()))
	overrides["chain_len"] = int(rng.integers(2, 4))
	spec = make_spec(
		kind=hwcore.KIND_HABIT,
		skill=float(rng.uniform(*SKILL_RANGE)),
		fights_in_session=int(rng.integers(FIGHTS_RANGE[0], FIGHTS_RANGE[1] + 1)) if fights is None else int(fights),
		habit=np.stack([table_a, table_b]),
		habit_switch_after=after,
		habit_noise=float(rng.uniform(*NOISE_RANGE)),
		adapts=bool(rng.random() < 0.5),
		tag=tag,
		**overrides,
	)
	if learner is None:
		learner = bool(rng.random() < LEARNER_FRACTION)
	if learner:
		spec.learn_rate = float(rng.uniform(*LEARN_RATE_RANGE))
		spec.learn_temp = float(rng.uniform(*LEARN_TEMP_RANGE))
	return spec


def reference_player(rng: np.random.Generator, *, kind: Optional[int] = None, skill: Optional[float] = None,
		fights: Optional[int] = None, tag: Optional[int] = None) -> HWRLPlayerSpec:
	"""A reference bot (kinds 0-5): its profile comes from skill; no overrides."""
	k = int(rng.integers(hwcore.NUM_REFERENCE_KINDS)) if kind is None else int(kind)
	return make_spec(
		kind=k,
		skill=float(rng.uniform(*SKILL_RANGE)) if skill is None else float(skill),
		fights_in_session=int(rng.integers(FIGHTS_RANGE[0], FIGHTS_RANGE[1] + 1)) if fights is None else int(fights),
		tag=TAG_REF + k if tag is None else int(tag),
	)


# =====================================================================================================================
# Player sources: anything with sample() -> spec, record_fight(tag, result), state_dict() / load_state_dict()
# =====================================================================================================================

class Population:
	"""The training population with its curriculum (DESIGN.md §9).

	stream=STREAM_HELDOUT gives the held-out population (tags 20+band) — same distributions, disjoint draws."""

	def __init__(self, seed: int = 0, *, stream: int = STREAM_TRAIN, ref_fraction: float = REF_FRACTION,
			unlocked: int = 1, curriculum: bool = True, window: int = UNLOCK_WINDOW,
			unlock_win_rate: float = UNLOCK_WIN_RATE, band_weights: Sequence[float] = BAND_WEIGHTS,
			bands: Optional[Sequence[int]] = None, fights: Optional[int] = None):
		self.seed = int(seed)
		self.stream = int(stream)
		self.rng = make_rng(seed, stream)
		self.ref_fraction = float(ref_fraction)
		self.unlocked = int(np.clip(unlocked, 1, NUM_BANDS))
		self.curriculum = bool(curriculum)
		self.window = int(window)
		self.unlock_win_rate = float(unlock_win_rate)
		self.band_weights = tuple(float(w) for w in band_weights)
		self.bands = None if bands is None else tuple(int(b) for b in bands)   # a fixed band subset (no curriculum)
		self.fights = fights
		self.newest_results: deque = deque(maxlen=self.window)   # 1 = keeper won, vs the newest unlocked band
		self.band_windows: Dict[int, deque] = {b: deque(maxlen=self.window) for b in range(NUM_BANDS)}
		self.unlock_log: List[dict] = []
		self.fights_seen = 0
		self.sessions_sampled = 0
		self.band_tag = TAG_HELDOUT_BAND if self.stream == STREAM_HELDOUT else TAG_BAND
		self.ref_tag = TAG_HELDOUT_REF if self.stream == STREAM_HELDOUT else TAG_REF
		# The keeper's side of each session (a separate stream: the player draws stay what they were) and the league.
		self.keeper_rng = make_rng(seed, stream, 77)
		self.vary_keeper = True
		self.full_skill_p = KEEPER_FULL_SKILL_P
		self.exploiters: List[dict] = []     # {"id": policy_id in the batch, "path": .hwrl, "weight": float, "round": int}
		self.exploit_fraction = EXPLOIT_FRACTION

	# -- sampling
	def active_bands(self) -> List[int]:
		return list(self.bands) if self.bands is not None else list(range(self.unlocked))

	def band_probs(self) -> np.ndarray:
		bands = self.active_bands()
		w = np.array([self.band_weights[b] for b in bands], np.float64)
		return w / w.sum()

	def _player(self) -> HWRLPlayerSpec:
		if self.exploiters and self.keeper_rng.random() < self.exploit_fraction:
			w = np.array([e["weight"] for e in self.exploiters], np.float64)
			e = self.exploiters[min(int(np.searchsorted(np.cumsum(w / w.sum()), self.keeper_rng.random(), side="right")), len(w) - 1)]
			return make_spec(kind=hwcore.KIND_POLICY, skill=0.5, policy_id=int(e["id"]), tag=TAG_EXPLOITER + int(e["id"]),
				fights_in_session=int(self.keeper_rng.integers(1, 4)) if self.fights is None else int(self.fights))
		if self.rng.random() < self.ref_fraction:
			k = int(self.rng.integers(hwcore.NUM_REFERENCE_KINDS))
			return reference_player(self.rng, kind=k, fights=self.fights, tag=self.ref_tag + k)
		bands = self.active_bands()
		cum = np.cumsum(self.band_probs())
		j = min(int(np.searchsorted(cum, self.rng.random(), side="right")), len(bands) - 1)
		band = int(bands[j])
		return habit_player(self.rng, band, tag=self.band_tag + band, fights=self.fights)

	def sample(self) -> HWRLPlayerSpec:
		self.sessions_sampled += 1
		spec = self._player()
		if self.vary_keeper:
			side = keeper_side(self.keeper_rng, self.full_skill_p)
			spec.keeper_skill = side["keeper_skill"]
			spec.keeper_identity = side["keeper_identity"]
		return spec

	def add_exploiter(self, policy_id: int, path: str, round_index: int, weight: float = 1.0) -> None:
		"""The league registers an exploiter (its policy_id in the env batch): later rounds weigh more."""
		self.exploiters.append({"id": int(policy_id), "path": str(path), "weight": float(weight), "round": int(round_index)})

	# -- curriculum
	def record_fight(self, tag: int, result: int) -> None:
		band = band_of_tag(tag)
		if band < 0:
			return
		won = 1 if int(result) == hwcore.RESULT_KEEPER_WON else 0
		self.fights_seen += 1
		self.band_windows[band].append(won)
		if not self.curriculum or self.bands is not None or self.unlocked >= NUM_BANDS or band != self.unlocked - 1:
			return
		self.newest_results.append(won)
		if len(self.newest_results) >= self.window and float(np.mean(self.newest_results)) >= self.unlock_win_rate:
			self.unlock_log.append({"band": self.unlocked, "fights_seen": self.fights_seen,
				"win_rate": float(np.mean(self.newest_results))})
			self.unlocked += 1
			self.newest_results.clear()

	def describe(self) -> dict:
		d = {"unlocked": self.unlocked, "newest_band": self.unlocked - 1,
			"newest_window": len(self.newest_results),
			"newest_win_rate": float(np.mean(self.newest_results)) if self.newest_results else 0.0,
			"exploiters": len(self.exploiters)}
		for b in range(NUM_BANDS):
			w = self.band_windows[b]
			d[f"band{b}_win_rate"] = float(np.mean(w)) if w else float("nan")
		return d

	# -- persistence (checkpoints)
	def state_dict(self) -> dict:
		return {"type": "Population", "seed": self.seed, "stream": self.stream, "rng": self.rng.bit_generator.state,
			"unlocked": self.unlocked, "newest_results": list(self.newest_results),
			"band_windows": {b: list(w) for b, w in self.band_windows.items()}, "unlock_log": list(self.unlock_log),
			"fights_seen": self.fights_seen, "sessions_sampled": self.sessions_sampled,
			"keeper_rng": self.keeper_rng.bit_generator.state, "exploiters": list(self.exploiters),
			"exploit_fraction": self.exploit_fraction}

	def load_state_dict(self, s: dict) -> None:
		self.rng.bit_generator.state = s["rng"]
		self.unlocked = int(s["unlocked"])
		self.newest_results = deque(s.get("newest_results", []), maxlen=self.window)
		for b, w in s.get("band_windows", {}).items():
			self.band_windows[int(b)] = deque(w, maxlen=self.window)
		self.unlock_log = list(s.get("unlock_log", []))
		self.fights_seen = int(s.get("fights_seen", 0))
		self.sessions_sampled = int(s.get("sessions_sampled", 0))
		if "keeper_rng" in s:
			self.keeper_rng.bit_generator.state = s["keeper_rng"]
		# Exploiter ids belong to the env batch that registered them: train.py re-registers the paths on resume.
		self.exploiters = [dict(e) for e in s.get("exploiters", [])]
		self.exploit_fraction = float(s.get("exploit_fraction", self.exploit_fraction))


class FixedSet:
	"""A fixed list of specs handed out in order, cycling (deterministic; no curriculum)."""

	def __init__(self, specs: Sequence[HWRLPlayerSpec], name: str = "fixed"):
		if not specs:
			raise ValueError("empty player set")
		self.specs = [hwcore.copy_spec(s) for s in specs]
		self.name = name
		self.next_index = 0
		self.unlocked = NUM_BANDS

	def sample(self) -> HWRLPlayerSpec:
		s = self.specs[self.next_index % len(self.specs)]
		self.next_index += 1
		return hwcore.copy_spec(s)

	def record_fight(self, tag: int, result: int) -> None:
		pass

	def describe(self) -> dict:
		return {"set": self.name, "size": len(self.specs), "handed_out": self.next_index}

	def state_dict(self) -> dict:
		return {"type": "FixedSet", "name": self.name, "next_index": self.next_index}

	def load_state_dict(self, s: dict) -> None:
		self.next_index = int(s.get("next_index", 0))


# =====================================================================================================================
# Named sets
# =====================================================================================================================

def habitual_set(fights: int = 1, skill: float = 0.8) -> FixedSet:
	"""rl1's sanity-check opponent: one Habitual reference bot (skill 0.8), 1-fight sessions."""
	return FixedSet([make_spec(kind=hwcore.KIND_HABITUAL, skill=skill, fights_in_session=fights, tag=TAG_FIXED)], "habitual")


def heldout_habit_players(n: int, band: int, seed: int = 0, *, fights: Optional[int] = None,
		switch: Optional[bool] = None, switch_after: Optional[int] = None, variant: int = 0, learner: bool = False,
		identity: int = 0, keeper_skill: float = 1.0) -> List[HWRLPlayerSpec]:
	"""n held-out habit players of one band (tags 20+band), from the held-out stream. The same (seed, band, variant)
	always gives the same players, whatever n (player i is the i-th draw). Fixed habits unless learner; the keeper
	plays as `identity` at `keeper_skill`."""
	rng = make_rng(seed, STREAM_HELDOUT, 1000 + band, variant)
	out = []
	for _ in range(n):
		sp = habit_player(rng, band, tag=TAG_HELDOUT_BAND + band, fights=fights, switch=switch, switch_after=switch_after,
			learner=learner)
		sp.keeper_identity = int(identity)
		sp.keeper_skill = float(keeper_skill)
		out.append(sp)
	return out


def reference_set(kinds: Sequence[int] = (hwcore.KIND_HABITUAL, hwcore.KIND_RHYTHM_PARRIER, hwcore.KIND_DODGER_LEFT,
		hwcore.KIND_VARIED, hwcore.KIND_TURTLE), skills: Sequence[float] = (0.3, 0.5, 0.7, 0.9),
		fights: int = 3, identity: int = 0, keeper_skill: float = 1.0) -> List[HWRLPlayerSpec]:
	"""The B0 sweep's reference bots (tags 30+kind): every kind at every skill."""
	return [make_spec(kind=k, skill=s, fights_in_session=fights, tag=TAG_HELDOUT_REF + k, keeper_identity=identity,
		keeper_skill=keeper_skill) for k in kinds for s in skills]


def eval_sets(seed: int = 0, n: int = 16, fights: int = 3, identity: int = 0, keeper_skill: float = 1.0) -> Dict[str, List[HWRLPlayerSpec]]:
	"""The evaluation sets of eval.py (held-out stream; identical for every arm and every run with the same seed).
	The keeper plays as `identity` at `keeper_skill` (the RL arm only; the script and classic arms ignore it)."""
	kw = dict(fights=fights, identity=identity, keeper_skill=keeper_skill)
	return {
		"habit_low": heldout_habit_players(n, 0, seed, switch=False, **kw),        # strong habits: reading pays
		"habit_mid": heldout_habit_players(n, 1, seed, switch=False, **kw),
		"habit_high": heldout_habit_players(n, 3, seed, switch=False, **kw),       # near random: not-a-bully
		"switch_low": heldout_habit_players(n, 0, seed, switch=True, switch_after=30, variant=1, **kw),
		"learners": heldout_habit_players(n, 0, seed, switch=False, variant=2, learner=True, **kw),  # players who adapt
		"reference": reference_set(fights=fights, identity=identity, keeper_skill=keeper_skill),
		"masher": [make_spec(kind=hwcore.KIND_MASHER, skill=0.1, fights_in_session=1, tag=TAG_HELDOUT_REF + hwcore.KIND_MASHER,
			keeper_identity=identity, keeper_skill=keeper_skill)],
	}


PLAYER_SETS = ("habitual", "population", "population_all", "heldout", "band0", "band1", "band2", "band3", "reference")


def make_player_source(name: str, seed: int = 0, **kw):
	"""A player source by name (train.py --players / the stage configs)."""
	if name == "habitual":
		return habitual_set(fights=kw.get("fights", 1), skill=kw.get("skill", 0.8))
	if name == "population":
		return Population(seed, **kw)
	if name == "population_all":
		return Population(seed, unlocked=NUM_BANDS, curriculum=False, **kw)
	if name == "heldout":
		return Population(seed, stream=STREAM_HELDOUT, unlocked=NUM_BANDS, curriculum=False, **kw)
	if name.startswith("band") and name[4:].isdigit():
		return Population(seed, bands=(int(name[4:]),), ref_fraction=kw.pop("ref_fraction", 0.0), curriculum=False, **kw)
	if name == "reference":
		return FixedSet(reference_set(fights=kw.get("fights", 3)), "reference")
	raise ValueError(f"unknown player set {name!r} (known: {', '.join(PLAYER_SETS)})")


def source_from_state(state: dict, seed: int = 0):
	"""Rebuild a player source from its state_dict (checkpoint resume)."""
	if state.get("type") == "Population":
		p = Population(state.get("seed", seed), stream=state.get("stream", STREAM_TRAIN))
		p.load_state_dict(state)
		return p
	raise ValueError("only Population sources are rebuilt from state; recreate FixedSets by name")


if __name__ == "__main__":
	# Quick look at the population: band mix, table entropies, switch rates.
	pop = Population(1, unlocked=NUM_BANDS)
	counts: Dict[str, int] = {}
	ent: Dict[str, list] = {}
	switches: Dict[str, int] = {}
	for _ in range(4000):
		s = pop.sample()
		g = tag_group(s.tag)
		counts[g] = counts.get(g, 0) + 1
		if s.kind == hwcore.KIND_HABIT:
			ent.setdefault(g, []).append(habit_table_entropy(hwcore.habit_array(s)[0]))
			switches[g] = switches.get(g, 0) + (s.habit_switch_after >= 0)
	for g in sorted(counts):
		e = ent.get(g)
		print(f"{g:8s} {counts[g] / 4000:6.1%}" + (f"  table entropy {np.mean(e):.2f} bits  switch {switches[g] / counts[g]:.0%}" if e else ""))
