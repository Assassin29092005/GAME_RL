"""HellwalkerRL — ctypes binding of RL/native/out/hwrl.dll (the C ABI declared in RL/native/hwrl_capi.h).

Why ctypes and not pybind11: nothing to compile against Python and nothing to install — the DLL is built with the same
MSVC flags as Sim/build.bat from the same engine-free core the game compiles, and this file mirrors its header.

The struct definitions below mirror HWRLPlayerSpec / HWRLEnvConfig / HWRLStepOut / HWRLEvalStats EXACTLY (field order
and C types). If the header changes, change them here in the same commit; `_check_layout()` catches a drift in the
sizes that the DLL reports at load time.

The DLL is loaded lazily: importing this module (for the struct types, the constants, make_spec) never needs it, so
players.py / env.py (MockBatch) / the trainer's unit tests run on a machine without a build.
"""

from __future__ import annotations

import ctypes
import os
from ctypes import POINTER, Structure, byref, c_char, c_char_p, c_float, c_int8, c_int32, c_uint8, c_uint64, c_void_p
from pathlib import Path
from typing import Dict, Iterable, Optional, Sequence, Tuple

import numpy as np

RL_DIR = Path(__file__).resolve().parent
DEFAULT_DLL = RL_DIR / "native" / "out" / "hwrl.dll"

# ---- layout (HWCore/HWRLTypes.h) — mirrored so everything works without the DLL; validated against it on load ------
ABI_VERSION = 1
OBS_LAYOUT_VERSION = 2
OBS_DIM = 103
NUM_ACTIONS = 23
HISTORY_TOKENS = 32
TOKEN_FIELDS = 6
AUX_CLASSES = 12
TOKEN_VOCAB = (10, 13, 6, 5, 10, 4)

# Actions: the 22 boss moves of EMoveId (BFastSlash .. BRetreat, in enum order) + Wait.
ACTION_NAMES = (
	"BFastSlash", "BSweepLeft", "BSweepLeftLate", "BSweepRight", "BSweepRightLate",
	"BHeavyCleave", "BDelayedHeavy", "BHeavySweepLeft", "BHeavySweepRight",
	"BFeintEarly", "BFeintMid", "BFeintLate",
	"BKillerThrust", "BGrab",
	"BGuard", "BCounterStance", "BBackstep", "BSideStepL", "BSideStepR",
	"BApproach", "BDashIn", "BRetreat",
	"Wait",
)
ACTION_WAIT = 22
NUM_ATTACK_ACTIONS = 14          # actions 0..13 (BFastSlash .. BGrab) are attacks
ACTION_IS_ATTACK = np.array([a < NUM_ATTACK_ACTIONS for a in range(NUM_ACTIONS)], dtype=bool)

# The read head's classes: the 12 player symbols (ESym Neutral .. Switch).
ANSWER_NAMES = ("Neutral", "Advance", "Retreat", "Light", "Heavy", "Block", "Parry", "StepF", "StepB", "StepL", "StepR", "Switch")

# Simulated players (hwrl_capi.h): kinds 0-5 are the reference bots, 6 a procedural habit player, 7 an exploiter policy.
KIND_MASHER, KIND_TURTLE, KIND_HABITUAL, KIND_VARIED, KIND_DODGER_LEFT, KIND_RHYTHM_PARRIER, KIND_HABIT, KIND_POLICY = range(8)
KIND_NAMES = ("Masher", "Turtle", "Habitual", "Varied", "DodgerLeft", "RhythmParrier", "Habit", "Policy")
NUM_REFERENCE_KINDS = 6
# Habit tables: [table A / B][boss swing class][response] weights.
HABIT_CLASSES = ("BFast", "BHeavy", "BFeint", "BKiller")
HABIT_RESPONSES = ("Parry", "Block", "StepL", "StepR", "StepB", "StepF", "Attack", "None")

# HWRLStepOut.result
RESULT_NONE, RESULT_KEEPER_WON, RESULT_KEEPER_DIED, RESULT_TIMEOUT = 0, 1, 2, 3
# hwrl_eval_sessions arms
ARM_SCRIPT, ARM_CLASSIC, ARM_RL = 0, 1, 2
ARM_NAMES = ("Pathbreaker (script)", "Hellwalker (classic)", "Hellwalker (RL)")


class HWRLUnavailable(RuntimeError):
	"""hwrl.dll is missing or does not match this binding."""


# =====================================================================================================================
# Structs (mirror hwrl_capi.h exactly)
# =====================================================================================================================

class HWRLPlayerSpec(Structure):
	_fields_ = [
		("kind", c_int32),
		("skill", c_float),
		("fights_in_session", c_int32),
		("habit", ((c_float * 8) * 4) * 2),     # float habit[2][4][8]
		("habit_switch_after", c_int32),
		("habit_noise", c_float),
		("adapts", c_int32),
		("react_mean", c_float),
		("react_sigma", c_float),
		("timing_sigma", c_float),
		("parry_aim", c_float),
		("step_aim", c_float),
		("feint_read", c_float),
		("killer_read", c_float),
		("punish_rate", c_float),
		("aggro_rate", c_float),
		("heavy_rate", c_float),
		("switch_rate", c_float),
		("preferred_range", c_float),
		("chain_len", c_int32),
		("policy_id", c_int32),
		("tag", c_int32),
	]


# The skill-profile overrides (a negative value keeps the profile's default).
SPEC_OVERRIDES = ("react_mean", "react_sigma", "timing_sigma", "parry_aim", "step_aim", "feint_read", "killer_read",
	"punish_rate", "aggro_rate", "heavy_rate", "switch_rate", "preferred_range", "chain_len")


class HWRLEnvConfig(Structure):
	_fields_ = [
		("max_fight_seconds", c_int32),
		("target_spm", c_float),
		("start_distance_min", c_float),
		("start_distance_max", c_float),
		("immortal", c_int32),
		("num_threads", c_int32),
	]


class HWRLStepOut(Structure):
	_fields_ = [
		("reward", POINTER(c_float)),
		("cost", POINTER(c_float)),
		("fight_done", POINTER(c_uint8)),
		("session_done", POINTER(c_uint8)),
		("result", POINTER(c_uint8)),
		("aux_label", POINTER(c_int8)),
		("frames", POINTER(c_int32)),
		("swings", POINTER(c_int32)),
		("dmg_dealt", POINTER(c_float)),
		("dmg_taken", POINTER(c_float)),
		("read_counter", POINTER(c_uint8)),
		("tag", POINTER(c_int32)),
		("fight_index", POINTER(c_int32)),
		("habit_phase", POINTER(c_int32)),
	]


class HWRLEvalStats(Structure):
	_fields_ = [
		("fights", c_int32),
		("player_deaths", c_int32),
		("boss_deaths", c_int32),
		("timeouts", c_int32),
		("seconds", c_float),
		("player_dmg_taken", c_float),
		("boss_dmg_taken", c_float),
		("boss_swings", c_int32),
		("boss_hits", c_int32),
		("boss_whiffs", c_int32),
		("boss_blocked", c_int32),
		("boss_parried", c_int32),
		("decisions", c_int32),
		("read_counters", c_int32),
	]


# Expected sizes (all fields are 4 bytes, so no padding): a cheap guard against a header drift.
assert ctypes.sizeof(HWRLPlayerSpec) == 340, ctypes.sizeof(HWRLPlayerSpec)
assert ctypes.sizeof(HWRLEnvConfig) == 24
assert ctypes.sizeof(HWRLStepOut) == 14 * ctypes.sizeof(c_void_p)
assert ctypes.sizeof(HWRLEvalStats) == 56


def env_config(max_fight_seconds: int = 180, target_spm: float = 66.0, start_distance_min: float = 350.0,
		start_distance_max: float = 900.0, immortal: bool = False, num_threads: int = 0) -> HWRLEnvConfig:
	"""An HWRLEnvConfig. target_spm defaults to FRLConfig::TargetSwingsPerMin (66): the ObsSwingDeficit feature the
	game's keeper sees must match what the policy was trained with."""
	return HWRLEnvConfig(int(max_fight_seconds), float(target_spm), float(start_distance_min), float(start_distance_max),
		1 if immortal else 0, int(num_threads))


def make_spec(kind: int = KIND_HABITUAL, skill: float = 0.5, fights_in_session: int = 1, habit=None,
		habit_switch_after: int = -1, habit_noise: float = 0.0, adapts: bool = True, tag: int = 0, policy_id: int = -1,
		**overrides) -> HWRLPlayerSpec:
	"""An HWRLPlayerSpec with every profile override at -1 (keep the skill-derived default) unless given.
	habit: array-like [2][4][8] (or [4][8]: used for both tables)."""
	s = HWRLPlayerSpec()
	s.kind = int(kind)
	s.skill = float(skill)
	s.fights_in_session = int(fights_in_session)
	if habit is not None:
		set_habit(s, habit)
	s.habit_switch_after = int(habit_switch_after)
	s.habit_noise = float(habit_noise)
	s.adapts = 1 if adapts else 0
	for name in SPEC_OVERRIDES:
		v = overrides.pop(name, -1)
		setattr(s, name, int(v) if name == "chain_len" else float(v))
	if overrides:
		raise TypeError(f"unknown HWRLPlayerSpec fields: {sorted(overrides)}")
	s.policy_id = int(policy_id)
	s.tag = int(tag)
	return s


def habit_array(spec: HWRLPlayerSpec) -> np.ndarray:
	"""A writable numpy VIEW [2, 4, 8] of the spec's habit tables."""
	return np.ctypeslib.as_array(spec.habit).reshape(2, 4, 8)


def set_habit(spec: HWRLPlayerSpec, habit) -> None:
	h = np.asarray(habit, dtype=np.float32)
	if h.shape == (4, 8):
		h = np.stack([h, h])
	if h.shape != (2, 4, 8):
		raise ValueError(f"habit tables must be [2,4,8] or [4,8], got {h.shape}")
	habit_array(spec)[...] = h


def spec_to_dict(spec: HWRLPlayerSpec) -> dict:
	d = {}
	for name, _ in HWRLPlayerSpec._fields_:
		if name == "habit":
			d[name] = habit_array(spec).tolist()
		else:
			d[name] = getattr(spec, name)
	return d


def spec_from_dict(d: dict) -> HWRLPlayerSpec:
	s = HWRLPlayerSpec()
	for name, _ in HWRLPlayerSpec._fields_:
		if name not in d:
			continue
		if name == "habit":
			set_habit(s, d[name])
		else:
			setattr(s, name, d[name])
	return s


def copy_spec(spec: HWRLPlayerSpec) -> HWRLPlayerSpec:
	c = HWRLPlayerSpec()
	ctypes.memmove(byref(c), byref(spec), ctypes.sizeof(HWRLPlayerSpec))
	return c


# =====================================================================================================================
# Library loading
# =====================================================================================================================

_lib = None
_lib_path: Optional[Path] = None
_dll_dir_cookies = []   # os.add_dll_directory handles: keep them alive for the process


def _declare(lib) -> None:
	"""argtypes / restype for every function in hwrl_capi.h. Array arguments are c_void_p (numpy .ctypes.data) —
	the cheapest way through ctypes on the per-step path."""
	V = c_void_p
	sig = {
		"hwrl_abi_version": ([], c_int32),
		"hwrl_obs_layout_version": ([], c_int32),
		"hwrl_obs_dim": ([], c_int32),
		"hwrl_num_actions": ([], c_int32),
		"hwrl_history_tokens": ([], c_int32),
		"hwrl_token_fields": ([], c_int32),
		"hwrl_aux_classes": ([], c_int32),
		"hwrl_token_vocab": ([POINTER(c_int32)], None),
		"hwrl_obs_feature_name": ([c_int32], c_char_p),
		"hwrl_action_name": ([c_int32], c_char_p),
		"hwrl_answer_name": ([c_int32], c_char_p),
		"hwrl_batch_create": ([c_int32, c_uint64, POINTER(HWRLEnvConfig)], V),
		"hwrl_batch_destroy": ([V], None),
		"hwrl_batch_size": ([V], c_int32),
		"hwrl_batch_set_next_player": ([V, c_int32, POINTER(HWRLPlayerSpec)], None),
		"hwrl_batch_reset": ([V], None),
		"hwrl_batch_observe": ([V, V, V, V], None),
		"hwrl_batch_step": ([V, V, V, V, POINTER(HWRLStepOut)], None),
		"hwrl_policy_load": ([c_char_p, POINTER(c_char), c_int32], V),
		"hwrl_policy_free": ([V], None),
		"hwrl_policy_hidden": ([V], c_int32),
		"hwrl_policy_forward": ([V, c_int32, V, V, V, V, V, V, V, V], None),
		"hwrl_policy_aux": ([V, c_int32, V, V, V], None),
		"hwrl_eval_sessions": ([c_int32, V, POINTER(HWRLPlayerSpec), c_int32, c_uint64, c_int32, c_int32, c_int32,
			POINTER(HWRLEvalStats)], c_int32),
	}
	missing = []
	for name, (args, res) in sig.items():
		try:
			fn = getattr(lib, name)
		except AttributeError:
			missing.append(name)
			continue
		fn.argtypes = args
		fn.restype = res
	if missing:
		raise HWRLUnavailable(f"hwrl.dll lacks exports: {', '.join(missing)}")


def _check_layout(lib) -> None:
	got = {
		"abi": lib.hwrl_abi_version(), "obs_layout": lib.hwrl_obs_layout_version(), "obs_dim": lib.hwrl_obs_dim(),
		"num_actions": lib.hwrl_num_actions(), "history_tokens": lib.hwrl_history_tokens(),
		"token_fields": lib.hwrl_token_fields(), "aux_classes": lib.hwrl_aux_classes(),
	}
	want = {"abi": ABI_VERSION, "obs_layout": OBS_LAYOUT_VERSION, "obs_dim": OBS_DIM, "num_actions": NUM_ACTIONS,
		"history_tokens": HISTORY_TOKENS, "token_fields": TOKEN_FIELDS, "aux_classes": AUX_CLASSES}
	bad = {k: (got[k], want[k]) for k in want if got[k] != want[k]}
	vocab = (c_int32 * TOKEN_FIELDS)()
	lib.hwrl_token_vocab(vocab)
	if tuple(vocab) != TOKEN_VOCAB:
		bad["token_vocab"] = (tuple(vocab), TOKEN_VOCAB)
	if bad:
		raise HWRLUnavailable("hwrl.dll layout differs from RL/hwcore.py (got, expected): "
			+ ", ".join(f"{k} {v[0]} != {v[1]}" for k, v in bad.items()) + " — rebuild the DLL or update the binding")


def load_library(path: Optional[os.PathLike] = None):
	"""Load (once) and validate hwrl.dll. Search: `path`, $HWRL_DLL, RL/native/out/hwrl.dll."""
	global _lib, _lib_path
	if _lib is not None and path is None:
		return _lib
	p = Path(path or os.environ.get("HWRL_DLL") or DEFAULT_DLL).resolve()
	if _lib is not None and p == _lib_path:
		return _lib
	if not p.exists():
		raise HWRLUnavailable(f"{p} not found — build it with RL\\native\\build.bat (or use the mock backend)")
	if hasattr(os, "add_dll_directory"):
		_dll_dir_cookies.append(os.add_dll_directory(str(p.parent)))
	try:
		lib = ctypes.CDLL(str(p))
	except OSError as e:
		raise HWRLUnavailable(f"cannot load {p}: {e}") from e
	_declare(lib)
	_check_layout(lib)
	_lib, _lib_path = lib, p
	return lib


def available(path: Optional[os.PathLike] = None) -> bool:
	try:
		load_library(path)
		return True
	except HWRLUnavailable:
		return False


def obs_feature_names(lib=None) -> list:
	lib = lib or load_library()
	return [lib.hwrl_obs_feature_name(i).decode() for i in range(OBS_DIM)]


def action_names(lib=None) -> list:
	"""The move table's names (from the DLL); falls back to the EMoveId names without it."""
	try:
		lib = lib or load_library()
	except HWRLUnavailable:
		return list(ACTION_NAMES)
	return [lib.hwrl_action_name(a).decode() for a in range(NUM_ACTIONS)]


def answer_names(lib=None) -> list:
	try:
		lib = lib or load_library()
	except HWRLUnavailable:
		return list(ANSWER_NAMES)
	return [lib.hwrl_answer_name(c).decode() for c in range(AUX_CLASSES)]


def _ptr(a: Optional[np.ndarray]) -> Optional[int]:
	return None if a is None else a.ctypes.data


def _need(a, dtype, shape, name) -> np.ndarray:
	"""Validate a caller-provided array (dtype, shape, C-contiguous) — the DLL writes/reads raw memory."""
	if not isinstance(a, np.ndarray) or a.dtype != dtype or tuple(a.shape) != tuple(shape) or not a.flags.c_contiguous:
		got = f"{getattr(a, 'dtype', type(a))} {getattr(a, 'shape', '')}"
		raise ValueError(f"{name}: need a C-contiguous {np.dtype(dtype)} array of shape {tuple(shape)}, got {got}")
	return a


# =====================================================================================================================
# Step results
# =====================================================================================================================

STEP_FIELDS = (
	("reward", np.float32), ("cost", np.float32), ("fight_done", np.uint8), ("session_done", np.uint8),
	("result", np.uint8), ("aux_label", np.int8), ("frames", np.int32), ("swings", np.int32),
	("dmg_dealt", np.float32), ("dmg_taken", np.float32), ("read_counter", np.uint8), ("tag", np.int32),
	("fight_index", np.int32), ("habit_phase", np.int32),
)


class StepOut:
	"""Numpy arrays [num_envs] for every HWRLStepOut field, plus the struct pointing at them (built once)."""

	def __init__(self, num_envs: int):
		self.num_envs = num_envs
		self.struct = HWRLStepOut()
		for name, dt in STEP_FIELDS:
			a = np.zeros(num_envs, dtype=dt)
			setattr(self, name, a)
			ctype = dict(HWRLStepOut._fields_)[name]._type_
			setattr(self.struct, name, a.ctypes.data_as(POINTER(ctype)))

	def as_dict(self, copy: bool = True) -> Dict[str, np.ndarray]:
		return {name: (getattr(self, name).copy() if copy else getattr(self, name)) for name, _ in STEP_FIELDS}


# =====================================================================================================================
# The environment batch
# =====================================================================================================================

class Batch:
	"""N environments stepped together in C++ (FRLEnvBatch). Buffers are numpy; observe() can write straight into
	caller-owned arrays (the trainer's pinned staging buffer), so there is no extra copy on the hot path.

	Lifecycle: set_next_player(i, spec) for every env -> reset() -> [observe() -> step(actions)]* -> destroy().
	After a step with session_done[i], env i is already playing its pending player: give it the next one."""

	def __init__(self, num_envs: int, seed: int = 0, config: Optional[HWRLEnvConfig] = None, lib=None):
		self.lib = lib or load_library()
		self.config = config if config is not None else env_config()
		self._h = self.lib.hwrl_batch_create(int(num_envs), c_uint64(int(seed) & 0xFFFFFFFFFFFFFFFF), byref(self.config))
		if not self._h:
			raise HWRLUnavailable("hwrl_batch_create failed")
		self.num_envs = int(self.lib.hwrl_batch_size(self._h))
		n = self.num_envs
		self.out = StepOut(n)
		self.obs = np.zeros((n, OBS_DIM), np.float32)
		self.tokens = np.zeros((n, HISTORY_TOKENS, TOKEN_FIELDS), np.int8)
		self.mask = np.zeros((n, NUM_ACTIONS), np.uint8)
		self._actions = np.zeros(n, np.int32)

	# -- players
	def set_next_player(self, env: int, spec: HWRLPlayerSpec) -> None:
		if not 0 <= env < self.num_envs:
			raise IndexError(env)
		self.lib.hwrl_batch_set_next_player(self._h, int(env), byref(spec))

	def set_next_players(self, specs: Sequence[HWRLPlayerSpec]) -> None:
		for i, s in enumerate(specs):
			self.set_next_player(i, s)

	# -- stepping
	def reset(self) -> None:
		self.lib.hwrl_batch_reset(self._h)

	def observe(self, obs: Optional[np.ndarray] = None, tokens: Optional[np.ndarray] = None,
			mask: Optional[np.ndarray] = None) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
		"""The current decision of every env -> (obs f32 [n,103], tokens i8 [n,32,6], mask u8 [n,23]). Without out
		arrays the batch's own buffers are returned (overwritten by the next observe)."""
		n = self.num_envs
		obs = self.obs if obs is None else _need(obs, np.float32, (n, OBS_DIM), "obs")
		tokens = self.tokens if tokens is None else _need(tokens, np.int8, (n, HISTORY_TOKENS, TOKEN_FIELDS), "tokens")
		mask = self.mask if mask is None else _need(mask, np.uint8, (n, NUM_ACTIONS), "mask")
		self.lib.hwrl_batch_observe(self._h, obs.ctypes.data, tokens.ctypes.data, mask.ctypes.data)
		return obs, tokens, mask

	def step(self, actions, aux_top: Optional[np.ndarray] = None, aux_top_p: Optional[np.ndarray] = None) -> StepOut:
		"""Apply one action per env; each env simulates to its next decision. Returns the batch's StepOut (arrays are
		overwritten by the next step — copy what you keep). aux_top (int8 0..11) / aux_top_p (float): the read head's
		top answer and its probability for the chosen actions (enables READ-counter stats)."""
		n = self.num_envs
		a = actions if (isinstance(actions, np.ndarray) and actions.dtype == np.int32 and actions.flags.c_contiguous
			and actions.shape == (n,)) else None
		if a is None:
			self._actions[:] = np.asarray(actions).reshape(n)
			a = self._actions
		top = None if aux_top is None else np.ascontiguousarray(aux_top, dtype=np.int8).reshape(n)
		top_p = None if aux_top_p is None else np.ascontiguousarray(aux_top_p, dtype=np.float32).reshape(n)
		if (top is None) != (top_p is None):
			raise ValueError("aux_top and aux_top_p go together")
		self.lib.hwrl_batch_step(self._h, a.ctypes.data, _ptr(top), _ptr(top_p), byref(self.out.struct))
		return self.out

	# -- lifetime
	def destroy(self) -> None:
		if getattr(self, "_h", None):
			self.lib.hwrl_batch_destroy(self._h)
			self._h = None

	close = destroy

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		self.destroy()

	def __del__(self):
		try:
			self.destroy()
		except Exception:
			pass


# =====================================================================================================================
# The C++ policy forward pass (HWCore/HWRLPolicy): parity tests and C++-side evaluation
# =====================================================================================================================

class Policy:
	"""A .hwrl policy run by the game's own C++ forward pass (FRLPolicy)."""

	def __init__(self, path: os.PathLike, lib=None):
		self.lib = lib or load_library()
		self.path = str(path)
		err = ctypes.create_string_buffer(512)
		self._h = self.lib.hwrl_policy_load(os.fsencode(self.path), err, len(err))
		if not self._h:
			raise ValueError(f"hwrl_policy_load({self.path}): {err.value.decode(errors='replace') or 'failed'}")
		self.hidden = int(self.lib.hwrl_policy_hidden(self._h))

	@classmethod
	def load(cls, path: os.PathLike, lib=None) -> "Policy":
		return cls(path, lib)

	@property
	def handle(self):
		return self._h

	def forward(self, obs: np.ndarray, tokens: np.ndarray, mask: Optional[np.ndarray] = None,
			h_in: Optional[np.ndarray] = None) -> Dict[str, np.ndarray]:
		"""n decisions -> {logits [n,A] (masked -1e30), value [n], argmax [n], h_out [n,H]}."""
		obs = np.ascontiguousarray(obs, dtype=np.float32)
		if obs.ndim == 1:
			obs = obs[None]
		n = obs.shape[0]
		_need(obs, np.float32, (n, OBS_DIM), "obs")
		tokens = _need(np.ascontiguousarray(tokens, dtype=np.int8).reshape(n, HISTORY_TOKENS, TOKEN_FIELDS),
			np.int8, (n, HISTORY_TOKENS, TOKEN_FIELDS), "tokens")
		if mask is not None:
			mask = np.ascontiguousarray(mask).astype(np.uint8, copy=False).reshape(n, NUM_ACTIONS)
		if h_in is not None:
			h_in = _need(np.ascontiguousarray(h_in, dtype=np.float32).reshape(n, self.hidden), np.float32, (n, self.hidden), "h_in")
		h_out = np.zeros((n, self.hidden), np.float32)
		logits = np.zeros((n, NUM_ACTIONS), np.float32)
		value = np.zeros(n, np.float32)
		argmax = np.zeros(n, np.int32)
		self.lib.hwrl_policy_forward(self._h, n, obs.ctypes.data, tokens.ctypes.data, _ptr(mask), _ptr(h_in),
			h_out.ctypes.data, logits.ctypes.data, value.ctypes.data, argmax.ctypes.data)
		return {"logits": logits, "value": value, "argmax": argmax, "h_out": h_out}

	def aux(self, h: np.ndarray, actions) -> np.ndarray:
		"""The read head: h [n,H] (the NEW hidden state from forward), actions [n] -> probs [n, 12]."""
		h = np.ascontiguousarray(h, dtype=np.float32)
		if h.ndim == 1:
			h = h[None]
		n = h.shape[0]
		_need(h, np.float32, (n, self.hidden), "h")
		a = np.ascontiguousarray(actions, dtype=np.int32).reshape(n)
		probs = np.zeros((n, AUX_CLASSES), np.float32)
		self.lib.hwrl_policy_aux(self._h, n, h.ctypes.data, a.ctypes.data, probs.ctypes.data)
		return probs

	def free(self) -> None:
		if getattr(self, "_h", None):
			self.lib.hwrl_policy_free(self._h)
			self._h = None

	close = free

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		self.free()

	def __del__(self):
		try:
			self.free()
		except Exception:
			pass


# =====================================================================================================================
# Evaluation with the brains in C++ (RL.md §7)
# =====================================================================================================================

def eval_stats_dict(st: HWRLEvalStats) -> dict:
	d = {name: getattr(st, name) for name, _ in HWRLEvalStats._fields_}
	minutes = max(d["seconds"], 1e-6) / 60.0
	swings = max(d["boss_swings"], 1)
	fights = max(d["fights"], 1)
	d.update({
		"dmg_per_min": d["player_dmg_taken"] / minutes,          # what the keeper deals
		"boss_dmg_per_min": d["boss_dmg_taken"] / minutes,       # what the keeper takes
		"swings_per_min": d["boss_swings"] / minutes,
		"hit_rate": d["boss_hits"] / swings,
		"keeper_win_rate": d["player_deaths"] / fights,
		"keeper_loss_rate": d["boss_deaths"] / fights,
		"timeout_rate": d["timeouts"] / fights,
		"read_counters_per_min": d["read_counters"] / minutes,
	})
	return d


def sum_eval_stats(dicts: Iterable[dict]) -> dict:
	"""Sum raw HWRLEvalStats dicts and recompute the rates."""
	acc = HWRLEvalStats()
	for d in dicts:
		for name, ctype in HWRLEvalStats._fields_:
			setattr(acc, name, getattr(acc, name) + (d[name] if ctype is c_int32 else float(d[name])))
	return eval_stats_dict(acc)


def eval_sessions(arm: int, spec: HWRLPlayerSpec, sessions: int, seed: int, policy: Optional[Policy] = None,
		immortal: bool = True, fight_seconds: int = 90, script_index: int = 0, lib=None) -> dict:
	"""`sessions` sessions of spec.fights_in_session fights vs the spec's player, the brain deciding in C++ (arm 0
	script, 1 classic, 2 RL — needs `policy`). Deterministic in `seed`: every arm sees identical players."""
	lib = lib or load_library()
	if arm == ARM_RL and policy is None:
		raise ValueError("arm 2 (RL) needs a policy")
	st = HWRLEvalStats()
	rc = lib.hwrl_eval_sessions(int(arm), policy.handle if policy is not None else None, byref(spec), int(sessions),
		c_uint64(int(seed) & 0xFFFFFFFFFFFFFFFF), 1 if immortal else 0, int(fight_seconds), int(script_index), byref(st))
	if rc < 0:
		raise RuntimeError(f"hwrl_eval_sessions(arm={arm}) returned {rc}")
	d = eval_stats_dict(st)
	d["rc"] = int(rc)
	return d
