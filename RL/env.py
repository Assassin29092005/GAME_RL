"""HellwalkerRL — the vector environment the trainer steps (DESIGN.md §9 "Env wrapper").

HellwalkerVecEnv wraps an hwcore.Batch (the C++ FRLEnvBatch) — or MockBatch, a pure-numpy stand-in with the same
interface — plus a player source from players.py:

* reset() starts a session on every env with a sampled player and queues the NEXT player right away: the C++ side
  starts a new session from the pending spec the moment a session ends inside step(), so after every session_done
  this wrapper hands that env its next-next player (and remembers which player the env is now playing);
* step(actions) -> obs, reward, cost, session_done, fight_done, info — session_done means the next observation
  belongs to a NEW player (the trainer resets the recurrent state; returns treat it as the episode end); fight_done
  alone does not (the keeper's memory carries across the fights of a session — the meta-RL point);
* running statistics per player group (band0..band3, ref, ...): keeper win rate, reward per fight, swings/min — for
  the curriculum (players.record_fight) and for logging.

Observations are written by the C++ observe call straight into ONE flat staging buffer (obs f32 | tokens i8 | mask u8 |
starts u8). With pin_memory=True it is page-locked torch memory: the trainer moves a whole decision to the GPU with a
single non-blocking copy per step and slices it there (Staging.unpack) — no per-array transfers, no extra host copy.
"""

from __future__ import annotations

import math
from collections import deque
from typing import Dict, List, Optional, Tuple

import numpy as np

import hwcore
import players as players_mod
from hwcore import (ACTION_WAIT, AUX_CLASSES, HISTORY_TOKENS, NUM_ACTIONS, NUM_ATTACK_ACTIONS, OBS_DIM, TOKEN_FIELDS,
	TOKEN_VOCAB)

try:  # torch is optional here: the env works on numpy alone (pinned staging needs torch + CUDA)
	import torch
except Exception:  # pragma: no cover
	torch = None


def _align(x: int, a: int = 64) -> int:
	return (x + a - 1) // a * a


# =====================================================================================================================
# Staging: one flat buffer per decision batch
# =====================================================================================================================

class Staging:
	"""obs f32 [n,103] | tokens i8 [n,32,6] | mask u8 [n,23] | starts u8 [n], each region 64-byte aligned in one flat
	uint8 buffer. `tensor` is the torch view (pinned when requested and possible); obs/tokens/mask/starts are numpy
	views into the same memory (what the DLL writes)."""

	def __init__(self, n: int, pin_memory: bool = False):
		self.n = n
		self.off_obs, self.nb_obs = 0, n * OBS_DIM * 4
		self.off_tok = _align(self.off_obs + self.nb_obs)
		self.nb_tok = n * HISTORY_TOKENS * TOKEN_FIELDS
		self.off_mask = _align(self.off_tok + self.nb_tok)
		self.nb_mask = n * NUM_ACTIONS
		self.off_start = _align(self.off_mask + self.nb_mask)
		self.nb_start = n
		self.nbytes = _align(self.off_start + self.nb_start)
		self.pinned = False
		self.tensor = None
		if torch is not None:
			pin = bool(pin_memory) and torch.cuda.is_available()
			self.tensor = torch.zeros(self.nbytes, dtype=torch.uint8, pin_memory=pin)
			self.pinned = pin
			buf = self.tensor.numpy()
		else:
			buf = np.zeros(self.nbytes, np.uint8)
		self.buf = buf
		self.obs = buf[self.off_obs:self.off_obs + self.nb_obs].view(np.float32).reshape(n, OBS_DIM)
		self.tokens = buf[self.off_tok:self.off_tok + self.nb_tok].view(np.int8).reshape(n, HISTORY_TOKENS, TOKEN_FIELDS)
		self.mask = buf[self.off_mask:self.off_mask + self.nb_mask].reshape(n, NUM_ACTIONS)
		self.starts = buf[self.off_start:self.off_start + self.nb_start]

	def unpack(self, t):
		"""Views (obs f32 [n,O], tokens i8 [n,K,F], mask bool [n,A], starts bool [n]) of a uint8 torch tensor with
		this layout — typically the device copy of `tensor`."""
		n = self.n
		obs = t[self.off_obs:self.off_obs + self.nb_obs].view(torch.float32).view(n, OBS_DIM)
		tok = t[self.off_tok:self.off_tok + self.nb_tok].view(torch.int8).view(n, HISTORY_TOKENS, TOKEN_FIELDS)
		mask = t[self.off_mask:self.off_mask + self.nb_mask].view(torch.bool).view(n, NUM_ACTIONS)
		starts = t[self.off_start:self.off_start + self.nb_start].view(torch.bool)
		return obs, tok, mask, starts


# =====================================================================================================================
# MockBatch: the hwcore.Batch interface in pure numpy (tests without the DLL)
# =====================================================================================================================

class MockBatch:
	"""Same interface as hwcore.Batch, no DLL. Random observations and masks, but with a learnable signal so a smoke
	run can show learning: every session draws a hidden "habit" (a target action, always legal) and, like a real habit
	player, it shows in the history tokens' answer field (and in the aux labels) — a reader that uses its memory scores
	+0.05 per decision, anything else -0.005. A scheduled habit switch (spec.habit_switch_after) redraws it.
	Fights last U{fight_len} decisions; results: keeper wins with P = fraction of on-habit decisions in the fight."""

	TARGETS = np.array([0, 1, 3, 5, 9, 12, 14, 16], np.int64)   # the habit's counter, one of 8 legal actions

	def __init__(self, num_envs: int, seed: int = 0, config: Optional[hwcore.HWRLEnvConfig] = None,
			fight_len: Tuple[int, int] = (20, 120), pool: int = 8192):
		self.num_envs = n = int(num_envs)
		self.config = config if config is not None else hwcore.env_config()
		self.rng = np.random.Generator(np.random.PCG64(np.random.SeedSequence(int(seed) & 0xFFFFFFFFFFFFFFFF, spawn_key=(77,))))
		self.fight_len = fight_len
		self.out = hwcore.StepOut(n)
		self.obs = np.zeros((n, OBS_DIM), np.float32)
		self.tokens = np.zeros((n, HISTORY_TOKENS, TOKEN_FIELDS), np.int8)
		self.mask = np.zeros((n, NUM_ACTIONS), np.uint8)
		# Pools of random rows (fancy-indexing a pool is ~10x cheaper than drawing fresh randoms every step).
		self.obs_pool = self.rng.standard_normal((pool, OBS_DIM), dtype=np.float32) * 0.5
		self.mask_pool = (self.rng.random((pool, NUM_ACTIONS)) < 0.5).astype(np.uint8)
		self.mask_pool[:, ACTION_WAIT] = 1
		self.pending: List[Optional[hwcore.HWRLPlayerSpec]] = [None] * n
		# Per-env session state.
		self.tag = np.zeros(n, np.int32)
		self.fights_in_session = np.ones(n, np.int32)
		self.switch_after = np.full(n, -1, np.int32)
		self.fight_index = np.zeros(n, np.int32)
		self.fight_left = np.zeros(n, np.int32)
		self.fight_steps = np.zeros(n, np.int32)
		self.fight_good = np.zeros(n, np.int32)
		self.session_swings = np.zeros(n, np.int32)
		self.habit_phase = np.zeros(n, np.int32)
		self.target = np.zeros(n, np.int64)
		self.decision = np.zeros(n, np.int64)
		self.cur_rows = np.zeros(n, np.int64)
		self.rows = np.arange(n)

	# -- hwcore.Batch interface
	def set_next_player(self, env: int, spec: hwcore.HWRLPlayerSpec) -> None:
		self.pending[env] = hwcore.copy_spec(spec)

	def set_next_players(self, specs) -> None:
		for i, s in enumerate(specs):
			self.set_next_player(i, s)

	def reset(self) -> None:
		self._start_sessions(self.rows)

	def observe(self, obs=None, tokens=None, mask=None):
		obs = self.obs if obs is None else obs
		tokens = self.tokens if tokens is None else tokens
		mask = self.mask if mask is None else mask
		obs[...] = self.obs_pool[self.cur_rows]
		obs[:, 0] = np.minimum(self.fight_steps / 120.0, 1.0)
		obs[:, 1] = np.minimum(self.fight_index / 3.0, 1.0)
		tokens[...] = self.tokens
		m = self.mask_pool[self.cur_rows]
		m[self.rows, self.target] = 1
		mask[...] = m
		self.mask[...] = m
		return obs, tokens, mask

	def step(self, actions, aux_top=None, aux_top_p=None) -> hwcore.StepOut:
		n, rng, o = self.num_envs, self.rng, self.out
		a = np.asarray(actions, dtype=np.int64).reshape(n)
		legal = (a >= 0) & (a < NUM_ACTIONS)
		legal[legal] = self.mask[self.rows[legal], a[legal]] != 0
		a = np.where(legal, a, ACTION_WAIT)                 # illegal -> Wait, like FRLObserver::ApplyAction
		good = a == self.target
		frames = rng.integers(6, 41, size=n).astype(np.int32)
		swings = (a < NUM_ATTACK_ACTIONS).astype(np.int32)
		reward = np.where(good, 0.05, -0.005).astype(np.float32)
		label = np.where(rng.random(n) < 0.8, self.target % AUX_CLASSES, rng.integers(0, AUX_CLASSES, size=n))
		label = np.where(rng.random(n) < 0.05, -1, label).astype(np.int8)

		o.tag[:] = self.tag
		o.fight_index[:] = self.fight_index
		o.habit_phase[:] = self.habit_phase
		o.frames[:] = frames
		o.swings[:] = swings
		o.cost[:] = (self.config.target_spm / 3600.0 * frames - swings).astype(np.float32)
		o.dmg_dealt[:] = np.where(good, 8.0, 0.0)
		o.dmg_taken[:] = np.where(rng.random(n) < 0.1, 6.0, 0.0)
		o.aux_label[:] = label
		if aux_top is not None and aux_top_p is not None:
			top = np.asarray(aux_top).reshape(n)
			p = np.asarray(aux_top_p).reshape(n)
			o.read_counter[:] = (good & swings.astype(bool) & (top == label) & (p >= 0.55)).astype(np.uint8)
		else:
			o.read_counter[:] = 0

		# History: push this decision's token (newest first). Answer = the habit + 1 most of the time.
		self.tokens[:, 1:] = self.tokens[:, :-1]
		answer = np.where(rng.random(n) < 0.8, self.target % 12 + 1, rng.integers(1, 13, size=n))
		self.tokens[:, 0, 0] = np.where(a == ACTION_WAIT, 9, a % 8 + 1)
		self.tokens[:, 0, 1] = answer
		self.tokens[:, 0, 2] = np.where(swings > 0, rng.integers(2, 6, size=n), 1)
		self.tokens[:, 0, 3] = 1 + good.astype(np.int8)
		self.tokens[:, 0, 4] = rng.integers(1, TOKEN_VOCAB[4], size=n)
		self.tokens[:, 0, 5] = 1

		# Habit switches (after N keeper swings in the session, like the C++ env).
		self.session_swings += swings
		sw = (self.switch_after >= 0) & (self.habit_phase == 0) & (self.session_swings >= self.switch_after)
		if sw.any():
			self.habit_phase[sw] = 1
			self.target[sw] = self.TARGETS[(np.searchsorted(self.TARGETS, self.target[sw]) + 1 + rng.integers(0, 7, size=int(sw.sum()))) % 8]

		# Fights and sessions.
		self.fight_steps += 1
		self.fight_good += good
		self.fight_left -= 1
		done = self.fight_left <= 0
		result = np.zeros(n, np.uint8)
		if done.any():
			idx = np.flatnonzero(done)
			p_win = self.fight_good[idx] / np.maximum(self.fight_steps[idx], 1)
			u = rng.random(idx.size)
			res = np.where(u < p_win, 1, np.where(rng.random(idx.size) < 0.5, 2, 3)).astype(np.uint8)
			result[idx] = res
			reward[idx] += np.where(res == 1, 1.0, np.where(res == 2, -1.0, 0.0)).astype(np.float32)
			self.fight_index[idx] += 1
			sess = idx[self.fight_index[idx] >= self.fights_in_session[idx]]
			nxt = idx[self.fight_index[idx] < self.fights_in_session[idx]]
			self._start_fights(nxt)
			o.session_done[:] = 0
			o.session_done[sess] = 1
			self._start_sessions(sess)
		else:
			o.session_done[:] = 0
		o.fight_done[:] = done
		o.result[:] = result
		o.reward[:] = reward
		self.decision += 1
		self.cur_rows = rng.integers(0, self.obs_pool.shape[0], size=n)
		return o

	def destroy(self) -> None:
		pass

	close = destroy

	# -- internals
	def _start_fights(self, idx: np.ndarray) -> None:
		if idx.size == 0:
			return
		self.fight_left[idx] = self.rng.integers(self.fight_len[0], self.fight_len[1] + 1, size=idx.size)
		self.fight_steps[idx] = 0
		self.fight_good[idx] = 0

	def _start_sessions(self, idx: np.ndarray) -> None:
		if idx.size == 0:
			return
		for i in idx:
			s = self.pending[i]
			if s is None:
				raise RuntimeError(f"env {i}: no pending player (set_next_player before reset / after session_done)")
			self.tag[i] = s.tag
			self.fights_in_session[i] = max(1, s.fights_in_session)
			self.switch_after[i] = s.habit_switch_after
		self.fight_index[idx] = 0
		self.session_swings[idx] = 0
		self.habit_phase[idx] = 0
		self.target[idx] = self.TARGETS[self.rng.integers(0, 8, size=idx.size)]
		self.tokens[idx] = 0
		self._start_fights(idx)


# =====================================================================================================================
# Running statistics per player group
# =====================================================================================================================

class FightStats:
	"""Per-group fight records: cumulative-since-last-pop counters (logging) and a sliding window (win rates)."""

	FIELDS = ("fights", "wins", "losses", "timeouts", "reward", "frames", "swings", "dealt", "taken")

	def __init__(self, window: int = 2000):
		self.window = window
		self.interval: Dict[str, Dict[str, float]] = {}
		self.recent: Dict[str, deque] = {}
		self.total_fights = 0

	def add(self, group: str, result: int, reward: float, frames: int, swings: int, dealt: float, taken: float) -> None:
		for g in (group, "all"):
			d = self.interval.setdefault(g, dict.fromkeys(self.FIELDS, 0.0))
			d["fights"] += 1
			d["wins"] += result == hwcore.RESULT_KEEPER_WON
			d["losses"] += result == hwcore.RESULT_KEEPER_DIED
			d["timeouts"] += result == hwcore.RESULT_TIMEOUT
			d["reward"] += reward
			d["frames"] += frames
			d["swings"] += swings
			d["dealt"] += dealt
			d["taken"] += taken
			self.recent.setdefault(g, deque(maxlen=self.window)).append(1 if result == hwcore.RESULT_KEEPER_WON else 0)
		self.total_fights += 1

	@staticmethod
	def summarise(d: Dict[str, float]) -> Dict[str, float]:
		f = max(d["fights"], 1.0)
		minutes = max(d["frames"], 1.0) / 3600.0
		return {"fights": d["fights"], "win_rate": d["wins"] / f, "loss_rate": d["losses"] / f,
			"timeout_rate": d["timeouts"] / f, "reward_per_fight": d["reward"] / f,
			"swings_per_min": d["swings"] / minutes, "dmg_per_min": d["dealt"] / minutes,
			"taken_per_min": d["taken"] / minutes, "fight_seconds": d["frames"] / 60.0 / f}

	def pop(self) -> Dict[str, Dict[str, float]]:
		"""Summaries of the fights since the last pop (+ windowed win rates), then reset the interval."""
		out = {}
		for g, d in self.interval.items():
			s = self.summarise(d)
			w = self.recent.get(g)
			s["win_rate_window"] = float(np.mean(w)) if w else float("nan")
			out[g] = s
		for g, w in self.recent.items():
			if g not in out:
				out[g] = {"fights": 0.0, "win_rate_window": float(np.mean(w)) if w else float("nan")}
		self.interval = {}
		return out


# =====================================================================================================================
# The vector env
# =====================================================================================================================

class HellwalkerVecEnv:
	"""num_envs keeper-vs-player environments with session handling and per-group statistics.

	backend: "dll" (hwcore.Batch — the real C++ env) or "mock" (MockBatch). players: a source from players.py
	(sample() / record_fight()). Observations are views into `staging` (overwritten by the next step)."""

	def __init__(self, num_envs: int, players, seed: int = 0, config: Optional[hwcore.HWRLEnvConfig] = None,
			backend: str = "dll", pin_memory: bool = False, stats_window: int = 2000, batch=None, **mock_kwargs):
		self.num_envs = n = int(num_envs)
		self.players = players
		self.config = config if config is not None else hwcore.env_config()
		self.backend = backend
		if batch is not None:
			self.batch = batch
		elif backend == "dll":
			self.batch = hwcore.Batch(n, seed, self.config)
		elif backend == "mock":
			self.batch = MockBatch(n, seed, self.config, **mock_kwargs)
		else:
			raise ValueError(f"backend {backend!r}")
		self.staging = Staging(n, pin_memory)
		self.current: List[Optional[hwcore.HWRLPlayerSpec]] = [None] * n
		self.pending: List[Optional[hwcore.HWRLPlayerSpec]] = [None] * n
		self.stats = FightStats(stats_window)
		self.fight_reward = np.zeros(n, np.float64)
		self.fight_frames = np.zeros(n, np.int64)
		self.fight_swings = np.zeros(n, np.int64)
		self.fight_dealt = np.zeros(n, np.float64)
		self.fight_taken = np.zeros(n, np.float64)
		# Interval counters (per decision) since the last pop_stats().
		self.int_decisions = 0
		self.int_cost = 0.0
		self.int_frames = 0
		self.int_swings = 0
		self.int_reads = 0
		self.int_sessions = 0
		self.int_labelled = 0
		self.total_decisions = 0
		self.total_sessions = 0

	# -- players
	def _assign_next(self, i: int) -> None:
		spec = self.players.sample()
		self.pending[i] = spec
		self.batch.set_next_player(i, spec)

	# -- gym-like API
	def reset(self) -> Dict[str, np.ndarray]:
		for i in range(self.num_envs):
			spec = self.players.sample()
			self.batch.set_next_player(i, spec)
			self.current[i] = spec
		self.batch.reset()
		for i in range(self.num_envs):
			self._assign_next(i)
		self.fight_reward[:] = 0
		self.fight_frames[:] = 0
		self.fight_swings[:] = 0
		self.fight_dealt[:] = 0
		self.fight_taken[:] = 0
		st = self.staging
		self.batch.observe(st.obs, st.tokens, st.mask)
		st.starts[:] = 1
		return self.obs_dict()

	def obs_dict(self) -> Dict[str, np.ndarray]:
		st = self.staging
		return {"obs": st.obs, "tokens": st.tokens, "mask": st.mask.view(np.bool_), "starts": st.starts.view(np.bool_)}

	def step(self, actions, aux_top: Optional[np.ndarray] = None, aux_top_p: Optional[np.ndarray] = None):
		"""-> obs (dict of staging views), reward f32 [n], cost f32 [n], session_done bool [n], fight_done bool [n], info
		(result, aux_label, frames, swings, dmg_dealt, dmg_taken, tag, fight_index, habit_phase, read_counter; copies)."""
		out = self.batch.step(actions, aux_top, aux_top_p)
		reward = out.reward.copy()
		cost = out.cost.copy()
		session_done = out.session_done.astype(bool)
		fight_done = out.fight_done.astype(bool)
		info = {
			"result": out.result.copy(), "aux_label": out.aux_label.copy(), "frames": out.frames.copy(),
			"swings": out.swings.copy(), "dmg_dealt": out.dmg_dealt.copy(), "dmg_taken": out.dmg_taken.copy(),
			"tag": out.tag.copy(), "fight_index": out.fight_index.copy(), "habit_phase": out.habit_phase.copy(),
			"read_counter": out.read_counter.astype(bool),
		}
		info["dmg"] = info["dmg_dealt"] - info["dmg_taken"]

		# Fight bookkeeping (per group of the player the finished fight was against).
		self.fight_reward += reward
		self.fight_frames += info["frames"]
		self.fight_swings += info["swings"]
		self.fight_dealt += info["dmg_dealt"]
		self.fight_taken += info["dmg_taken"]
		for i in np.flatnonzero(fight_done):
			tag = int(info["tag"][i])
			res = int(info["result"][i])
			self.stats.add(players_mod.tag_group(tag), res, float(self.fight_reward[i]), int(self.fight_frames[i]),
				int(self.fight_swings[i]), float(self.fight_dealt[i]), float(self.fight_taken[i]))
			self.players.record_fight(tag, res)
			self.fight_reward[i] = 0.0
			self.fight_frames[i] = 0
			self.fight_swings[i] = 0
			self.fight_dealt[i] = 0.0
			self.fight_taken[i] = 0.0

		# Sessions that ended: the C++ side already started the pending player — queue the next one.
		done_idx = np.flatnonzero(session_done)
		for i in done_idx:
			self.current[i] = self.pending[i]
			self._assign_next(int(i))

		self.int_decisions += self.num_envs
		self.int_cost += float(cost.sum())
		self.int_frames += int(info["frames"].sum())
		self.int_swings += int(info["swings"].sum())
		self.int_reads += int(info["read_counter"].sum())
		self.int_sessions += int(done_idx.size)
		self.int_labelled += int((info["aux_label"] >= 0).sum())
		self.total_decisions += self.num_envs
		self.total_sessions += int(done_idx.size)

		st = self.staging
		self.batch.observe(st.obs, st.tokens, st.mask)
		st.starts[:] = out.session_done
		return self.obs_dict(), reward, cost, session_done, fight_done, info

	def pop_stats(self) -> Dict[str, object]:
		"""Everything since the last call: per-group fight summaries + per-decision rates."""
		groups = self.stats.pop()
		d = max(self.int_decisions, 1)
		minutes = max(self.int_frames, 1) / 3600.0
		out = {
			"groups": groups,
			"decisions": self.int_decisions,
			"cost_per_decision": self.int_cost / d,
			"swings_per_min": self.int_swings / minutes,
			"reads_per_min": self.int_reads / minutes,
			"frames_per_decision": self.int_frames / d,
			"sessions": self.int_sessions,
			"label_rate": self.int_labelled / d,
		}
		self.int_decisions = self.int_cost = self.int_frames = self.int_swings = 0
		self.int_reads = self.int_sessions = self.int_labelled = 0
		return out

	def current_tags(self) -> np.ndarray:
		return np.array([s.tag if s is not None else -1 for s in self.current], np.int32)

	def close(self) -> None:
		if self.batch is not None:
			self.batch.destroy()
			self.batch = None

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		self.close()


def make_env(num_envs: int, players, seed: int = 0, backend: str = "auto", config=None, pin_memory: bool = False,
		**kw) -> HellwalkerVecEnv:
	"""backend "auto": the DLL if it loads, else the mock (with a warning)."""
	if backend == "auto":
		backend = "dll" if hwcore.available() else "mock"
		if backend == "mock":
			print("[env] hwrl.dll not available: using MockBatch")
	return HellwalkerVecEnv(num_envs, players, seed=seed, config=config, backend=backend, pin_memory=pin_memory, **kw)


def _selftest() -> None:
	"""MockBatch through the wrapper: shapes, session rollover, next-player handling, stats."""
	pop = players_mod.Population(3, unlocked=4)
	env = HellwalkerVecEnv(64, pop, seed=5, backend="mock", fight_len=(3, 6))
	obs = env.reset()
	assert obs["obs"].shape == (64, OBS_DIM) and obs["tokens"].shape == (64, HISTORY_TOKENS, TOKEN_FIELDS)
	assert obs["mask"].dtype == np.bool_ and obs["mask"][:, ACTION_WAIT].all() and obs["starts"].all()
	rng = np.random.default_rng(0)
	sessions = 0
	for _ in range(200):
		m = env.staging.mask.astype(bool)
		acts = np.array([rng.choice(np.flatnonzero(r)) for r in m], np.int32)
		obs, r, c, sd, fd, info = env.step(acts)
		assert np.all(sd <= fd), "a session ends only with a fight"
		assert np.array_equal(obs["starts"], sd)
		sessions += int(sd.sum())
		assert np.all(np.isfinite(r)) and np.all(np.isfinite(c))
	st = env.pop_stats()
	assert sessions > 0 and st["groups"]["all"]["fights"] > 0
	print(f"env selftest ok: {sessions} sessions, {st['groups']['all']['fights']:.0f} fights, "
		f"win {st['groups']['all']['win_rate']:.2f}, groups {sorted(st['groups'])}")


if __name__ == "__main__":
	_selftest()
