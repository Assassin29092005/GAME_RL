"""HellwalkerRL — recurrent PPO with action masking, a Lagrangian aggression constraint and the read head's auxiliary
loss (RL.md §5.2, DESIGN.md §9). CleanRL-style: one file, plain PyTorch, no wrappers — the masking + recurrence +
constraint combination is not covered by any off-the-shelf class.

One iteration = collect() + update():

* collect(): T decisions from every env. Per step the WHOLE observation batch (obs | tokens | mask | starts) crosses to
  the GPU in one pinned non-blocking copy (env.Staging), and the chosen actions (+ the read head's top answer and its
  probability, for the READ-counter stats) come back in one small copy — the only host sync per step. Stored on the
  device: obs f32, tokens int8, masks bool, starts, actions, log-probs, values, and the hidden state at every BPTT chunk
  start (every `bptt` steps). Rewards, costs, session ends and aux labels are gathered on the host and moved once.
* returns: the aggression cost is folded in as r - lambda_c * c (lambda_c is the Lagrange multiplier, updated once per
  iteration from the mean cost per decision, clipped at 0), then normalised by a running std of the discounted return;
  GAE(gamma 0.995, lambda 0.95) with SESSION ends as episode ends — fight ends are not, so reading you in fight 1 pays
  in fight 2 (the meta-RL point).
* update(): epochs x minibatches of whole `bptt`-step sequences, each re-run from its stored chunk-start hidden state
  (with the stored `starts` resetting it at session boundaries inside the chunk). Loss = clipped PPO on the masked
  policy + vf_coef * MSE(value) - ent_coef * masked entropy + aux_coef * CE(read head | action taken) on labelled steps.
  Adam, linear lr and entropy decay, grad-norm clip.

Correctness hooks: `ratio_deviation()` re-evaluates the stored rollout in minibatch-sequence form with the stored hidden
states — before any update the PPO ratio must be 1 (up to float error) or the sequence form is not reproducing the
rollout; `compute_gae` is a plain function with a hand-checked unit test (`python ppo_rnn.py --selftest`).
"""

from __future__ import annotations

import math
import time
from dataclasses import asdict, dataclass, field
from typing import Dict, Optional

import numpy as np
import torch
import torch.nn.functional as F

from hwcore import AUX_CLASSES, HISTORY_TOKENS, NUM_ACTIONS, OBS_DIM, TOKEN_FIELDS


@dataclass
class PPOConfig:
	num_steps: int = 128            # T: decisions per env per rollout
	bptt: int = 32                  # truncated backprop length = the sequence length of a minibatch item
	epochs: int = 4
	minibatches: int = 8
	gamma: float = 0.995
	gae_lambda: float = 0.95
	clip: float = 0.2
	lr: float = 3e-4
	lr_final: float = 3e-5
	ent_coef: float = 0.01
	ent_coef_final: float = 0.001
	vf_coef: float = 0.5
	aux_coef: float = 0.1
	max_grad_norm: float = 0.5
	adam_eps: float = 1e-5
	norm_adv: bool = True           # per-minibatch advantage normalisation
	norm_reward: bool = True        # divide rewards by a running std of the discounted return
	reward_clip: float = 10.0       # clip of the normalised reward
	clip_vloss: bool = False
	target_kl: Optional[float] = None
	# Lagrangian aggression constraint (RL.md §4.5): lambda_c += lambda_lr * mean(cost per decision), clipped to [0, max].
	use_cost: bool = True
	lambda_init: float = 0.0
	lambda_lr: float = 0.01
	lambda_max: float = 10.0
	cost_budget: float = 0.003     # allowed mean hinge cost per decision (the floor is a hinge: 0 is never quite reached)
	read_stats: bool = True         # run the read head in rollouts so the env can count READ counters
	tf32: bool = False              # TF32 matmuls (faster on Ampere; breaks the exact rollout/update ratio == 1 check)


def linear_schedule(start: float, end: float, frac: float) -> float:
	return start + (end - start) * min(max(frac, 0.0), 1.0)


# =====================================================================================================================
# Reward normalisation
# =====================================================================================================================

class RunningMeanStd:
	"""Running mean / variance (Chan et al. parallel update), float64."""

	def __init__(self, shape=(), eps: float = 1e-4):
		self.mean = np.zeros(shape, np.float64)
		self.var = np.ones(shape, np.float64)
		self.count = eps

	def update(self, x: np.ndarray) -> None:
		x = np.asarray(x, np.float64)
		b_mean, b_var, b_count = x.mean(0), x.var(0), x.shape[0]
		delta = b_mean - self.mean
		tot = self.count + b_count
		self.mean = self.mean + delta * b_count / tot
		m2 = self.var * self.count + b_var * b_count + delta ** 2 * self.count * b_count / tot
		self.var = m2 / tot
		self.count = tot

	def state_dict(self) -> dict:
		return {"mean": float(self.mean), "var": float(self.var), "count": float(self.count)}

	def load_state_dict(self, s: dict) -> None:
		self.mean = np.float64(float(s["mean"]))
		self.var = np.float64(float(s["var"]))
		self.count = float(s["count"])


class ReturnNormalizer:
	"""Rewards / running std of the discounted return, per env, episodes = sessions (gym's NormalizeReward, with the
	return zeroed AFTER the step that ended the session)."""

	def __init__(self, num_envs: int, gamma: float, clip: float = 10.0, eps: float = 1e-8):
		self.gamma = gamma
		self.clip = clip
		self.eps = eps
		self.ret = np.zeros(num_envs, np.float64)
		self.rms = RunningMeanStd(())

	def std(self) -> float:
		return float(np.sqrt(self.rms.var + self.eps))

	def __call__(self, rewards: np.ndarray, dones: np.ndarray) -> np.ndarray:
		"""rewards [T, N], dones [T, N] (the session ended with step t) -> normalised rewards [T, N] float32."""
		out = np.empty(rewards.shape, np.float32)
		for t in range(rewards.shape[0]):
			self.ret = self.ret * self.gamma + rewards[t]
			self.rms.update(self.ret)
			out[t] = rewards[t] / np.sqrt(self.rms.var + self.eps)
			self.ret[dones[t]] = 0.0
		return np.clip(out, -self.clip, self.clip)

	def state_dict(self) -> dict:
		return {"rms": self.rms.state_dict(), "ret": self.ret.copy()}

	def load_state_dict(self, s: dict) -> None:
		self.rms.load_state_dict(s["rms"])
		ret = np.asarray(s.get("ret", []), np.float64)
		if ret.shape == self.ret.shape:   # a different --envs on resume keeps the scale, restarts the returns
			self.ret = ret


# =====================================================================================================================
# GAE
# =====================================================================================================================

def compute_gae(rewards: torch.Tensor, values: torch.Tensor, dones: torch.Tensor, last_value: torch.Tensor,
		gamma: float, lam: float):
	"""Generalised advantage estimation.

	rewards, values: [T, N]; dones: [T, N] — dones[t] = the episode (session) ENDED with step t, so the value of the
	next observation (a new player) is not bootstrapped; last_value: [N] = V(s_T), the observation after the rollout.
	Returns (advantages, returns = advantages + values), both [T, N]."""
	T = rewards.shape[0]
	adv = torch.zeros_like(rewards)
	last = torch.zeros_like(last_value)
	nonterminal = 1.0 - dones.to(rewards.dtype)
	for t in reversed(range(T)):
		next_value = last_value if t == T - 1 else values[t + 1]
		delta = rewards[t] + gamma * next_value * nonterminal[t] - values[t]
		last = delta + gamma * lam * nonterminal[t] * last
		adv[t] = last
	return adv, adv + values


def masked_entropy(logp_all: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
	"""Entropy over the allowed actions only (masked actions have p = 0 and a -1e9 logit: excluded explicitly so
	0 * -1e9 can never become a NaN)."""
	p = logp_all.exp()
	return -torch.where(mask, p * logp_all, torch.zeros_like(p)).sum(-1)


def set_matmul_precision(tf32: bool) -> None:
	torch.set_float32_matmul_precision("high" if tf32 else "highest")
	try:
		torch.backends.cudnn.allow_tf32 = bool(tf32)
	except Exception:  # pragma: no cover
		pass


# =====================================================================================================================
# Rollout storage
# =====================================================================================================================

class RolloutStorage:
	def __init__(self, T: int, N: int, bptt: int, hidden: int, device: torch.device, pin: bool):
		self.T, self.N, self.L = T, N, bptt
		dev = device
		self.obs = torch.zeros(T, N, OBS_DIM, device=dev)
		self.tokens = torch.zeros(T, N, HISTORY_TOKENS, TOKEN_FIELDS, dtype=torch.int8, device=dev)
		self.masks = torch.zeros(T, N, NUM_ACTIONS, dtype=torch.bool, device=dev)
		self.starts = torch.zeros(T, N, dtype=torch.bool, device=dev)
		self.actions = torch.zeros(T, N, dtype=torch.long, device=dev)
		self.logp = torch.zeros(T, N, device=dev)
		self.values = torch.zeros(T, N, device=dev)
		self.hidden = torch.zeros(T // bptt, N, hidden, device=dev)
		# Filled on the host during the rollout, moved once at its end.
		self.h_reward = torch.zeros(T, N, pin_memory=pin)
		self.h_cost = torch.zeros(T, N, pin_memory=pin)
		self.h_done = torch.zeros(T, N, dtype=torch.bool, pin_memory=pin)
		self.h_aux = torch.zeros(T, N, dtype=torch.int8, pin_memory=pin)
		self.np_reward = self.h_reward.numpy()
		self.np_cost = self.h_cost.numpy()
		self.np_done = self.h_done.numpy()
		self.np_aux = self.h_aux.numpy()
		# Device copies after the rollout.
		self.rewards = None      # normalised, cost-folded
		self.dones = None
		self.aux = None
		self.advantages = None
		self.returns = None


# =====================================================================================================================
# The trainer
# =====================================================================================================================

class PPOTrainer:
	"""model: model.HellwalkerNet; env: env.HellwalkerVecEnv. total_iterations drives the lr / entropy schedules."""

	def __init__(self, model, env, cfg: PPOConfig, device="cuda", total_iterations: int = 1, seed: int = 0):
		self.cfg = cfg
		self.device = torch.device(device)
		self.cuda = self.device.type == "cuda"
		set_matmul_precision(cfg.tf32)
		self.model = model.to(self.device)
		self.env = env
		self.N = env.num_envs
		if cfg.num_steps % cfg.bptt != 0:
			raise ValueError(f"num_steps ({cfg.num_steps}) must be a multiple of bptt ({cfg.bptt})")
		num_seq = (cfg.num_steps // cfg.bptt) * self.N
		if num_seq % cfg.minibatches != 0:
			raise ValueError(f"(num_steps/bptt)*envs = {num_seq} sequences must split into {cfg.minibatches} minibatches")
		self.opt = torch.optim.Adam(self.model.parameters(), lr=cfg.lr, eps=cfg.adam_eps)
		self.total_iterations = max(1, int(total_iterations))
		self.iteration = 0
		self.decisions = 0
		self.lam = float(cfg.lambda_init)
		self.normalizer = ReturnNormalizer(self.N, cfg.gamma, cfg.reward_clip)
		self.hidden_size = int(self.model.initial_state(1, self.device).shape[-1])
		self.storage = RolloutStorage(cfg.num_steps, self.N, cfg.bptt, self.hidden_size, self.device, self.cuda)
		self.gen = torch.Generator(device=self.device)
		self.gen.manual_seed(int(seed))
		st = env.staging
		if st.tensor is None:
			raise RuntimeError("the env's staging buffer needs torch")
		# The device side of the one-copy-per-step transfer (on CPU the host buffer itself is used).
		self.dev_stage = torch.empty(st.nbytes, dtype=torch.uint8, device=self.device) if self.cuda else st.tensor
		self.act_host = torch.zeros(3, self.N, pin_memory=self.cuda)
		self.act_np = self.act_host.numpy()
		self.h = None
		self.started = False

	# -- schedules
	def schedule(self) -> Dict[str, float]:
		frac = self.iteration / self.total_iterations
		lr = linear_schedule(self.cfg.lr, self.cfg.lr_final, frac)
		ent = linear_schedule(self.cfg.ent_coef, self.cfg.ent_coef_final, frac)
		for g in self.opt.param_groups:
			g["lr"] = lr
		return {"lr": lr, "ent_coef": ent}

	def start(self) -> None:
		self.env.reset()
		self.h = self.model.initial_state(self.N, self.device)
		self.started = True

	def _upload(self):
		st = self.env.staging
		if self.cuda:
			self.dev_stage.copy_(st.tensor, non_blocking=True)   # THE host->device copy of this decision
		return st.unpack(self.dev_stage)

	# -- rollouts
	@torch.no_grad()
	def collect(self) -> Dict[str, float]:
		if not self.started:
			self.start()
		cfg, s, model = self.cfg, self.storage, self.model
		T, L = cfg.num_steps, cfg.bptt
		h = self.h
		t_env = 0.0
		for t in range(T):
			obs, tok, mask, starts = self._upload()
			s.obs[t].copy_(obs)
			s.tokens[t].copy_(tok)
			s.masks[t].copy_(mask)
			s.starts[t].copy_(starts)
			h = h.masked_fill(starts.unsqueeze(-1), 0.0)       # a new session: fresh memory
			if t % L == 0:
				s.hidden[t // L].copy_(h)
			logits, value, h_new = model.step(obs, tok, mask, h)
			logp_all = torch.log_softmax(logits, dim=-1)
			# Gumbel-max sampling: one fused pass, no host sync; masked logits (-1e9) are never chosen.
			# u is kept away from 0: -log(-log(0)) = +inf would outrank a masked (-1e9) logit (~1 in 3M draws).
			u = torch.rand(logits.shape, device=self.device, generator=self.gen).clamp_(min=1e-12)
			action = (logits - torch.log(-torch.log(u))).argmax(dim=-1)
			s.actions[t] = action
			s.logp[t] = logp_all.gather(-1, action.unsqueeze(-1)).squeeze(-1)
			s.values[t] = value
			if cfg.read_stats:
				top_p, top = torch.softmax(model.aux_logits(h_new, action), dim=-1).max(dim=-1)
				self.act_host.copy_(torch.stack([action.float(), top.float(), top_p]), non_blocking=True)
			else:
				self.act_host[0].copy_(action.float(), non_blocking=True)
			if self.cuda:
				torch.cuda.current_stream(self.device).synchronize()   # actions ready on the host
			actions_np = self.act_np[0].astype(np.int32)
			aux_top = self.act_np[1].astype(np.int8) if cfg.read_stats else None
			aux_p = self.act_np[2].copy() if cfg.read_stats else None
			t0 = time.perf_counter()
			_, r, c, sdone, _, info = self.env.step(actions_np, aux_top, aux_p)
			t_env += time.perf_counter() - t0
			s.np_reward[t] = r
			s.np_cost[t] = c
			s.np_done[t] = sdone
			s.np_aux[t] = info["aux_label"]
			h = h_new
		# Bootstrap value of the observation after the rollout (its step is taken at the start of the next rollout).
		obs, tok, mask, starts = self._upload()
		h = h.masked_fill(starts.unsqueeze(-1), 0.0)
		_, last_value, _ = model.step(obs, tok, mask, h)
		self.h = h

		# Returns: fold the cost with the current multiplier, normalise, GAE; then update the multiplier.
		raw_r, raw_c, done = s.np_reward, s.np_cost, s.np_done
		lam_used = self.lam if cfg.use_cost else 0.0
		folded = raw_r.astype(np.float64) - lam_used * raw_c.astype(np.float64)
		rew = self.normalizer(folded, done) if cfg.norm_reward else folded.astype(np.float32)
		s.rewards = torch.from_numpy(rew).to(self.device, non_blocking=True)
		s.dones = s.h_done.to(self.device, non_blocking=True)
		s.aux = s.h_aux.to(self.device, non_blocking=True).long()
		s.advantages, s.returns = compute_gae(s.rewards, s.values, s.dones, last_value, cfg.gamma, cfg.gae_lambda)
		mean_cost = float(raw_c.mean())
		if cfg.use_cost:
			self.lam = float(min(max(self.lam + cfg.lambda_lr * (mean_cost - cfg.cost_budget), 0.0), cfg.lambda_max))
		return {
			"reward_per_decision": float(raw_r.mean()),
			"cost_per_decision": mean_cost,
			"lambda": lam_used,
			"lambda_next": self.lam,
			"return_std": self.normalizer.std(),
			"value_mean": float(s.values.mean()),
			"sessions_ended": float(done.sum()),
			"env_seconds": t_env,
		}

	# -- the PPO update
	def _sequences(self):
		"""Rollout tensors [T, N, ...] -> [L, C*N, ...] (sequence j = chunk j // N of env j % N) + chunk-start hidden."""
		s, L = self.storage, self.cfg.bptt
		T, N = s.T, s.N
		C = T // L

		def seq(x):
			return x.view(C, L, N, *x.shape[2:]).transpose(0, 1).reshape(L, C * N, *x.shape[2:])

		return {
			"obs": seq(s.obs), "tokens": seq(s.tokens), "masks": seq(s.masks), "starts": seq(s.starts),
			"actions": seq(s.actions), "logp": seq(s.logp), "values": seq(s.values), "adv": seq(s.advantages),
			"ret": seq(s.returns), "aux": seq(s.aux), "h0": s.hidden.reshape(C * N, -1),
		}

	def _evaluate(self, q, idx):
		logits, value, feats, _ = self.model(q["obs"][:, idx], q["tokens"][:, idx], q["masks"][:, idx], q["h0"][idx],
			q["starts"][:, idx])
		return logits, value, feats

	def update(self, ent_coef: float) -> Dict[str, float]:
		cfg, model = self.cfg, self.model
		model.train()
		q = self._sequences()
		num_seq = q["h0"].shape[0]
		mb = num_seq // cfg.minibatches
		acc = {k: [] for k in ("loss", "pg_loss", "v_loss", "entropy", "aux_loss", "aux_acc", "approx_kl", "clipfrac",
			"grad_norm")}
		ratio_dev_first = None
		epochs_run = 0
		for epoch in range(cfg.epochs):
			perm = torch.randperm(num_seq, device=self.device, generator=self.gen)
			kls = []
			for k in range(cfg.minibatches):
				idx = perm[k * mb:(k + 1) * mb]
				logits, value, feats = self._evaluate(q, idx)
				logp_all = torch.log_softmax(logits, dim=-1)
				a = q["actions"][:, idx]
				newlogp = logp_all.gather(-1, a.unsqueeze(-1)).squeeze(-1)
				logratio = newlogp - q["logp"][:, idx]
				ratio = logratio.exp()
				if ratio_dev_first is None:
					ratio_dev_first = (ratio - 1.0).abs().max().detach()
				adv = q["adv"][:, idx]
				if cfg.norm_adv:
					adv = (adv - adv.mean()) / (adv.std() + 1e-8)
				pg_loss = torch.max(-adv * ratio, -adv * ratio.clamp(1.0 - cfg.clip, 1.0 + cfg.clip)).mean()
				ret = q["ret"][:, idx]
				if cfg.clip_vloss:
					old_v = q["values"][:, idx]
					v_clip = old_v + (value - old_v).clamp(-cfg.clip, cfg.clip)
					v_loss = torch.max((value - ret) ** 2, (v_clip - ret) ** 2).mean()
				else:
					v_loss = ((value - ret) ** 2).mean()
				entropy = masked_entropy(logp_all, q["masks"][:, idx]).mean()
				# The read head, conditioned on the action taken, on steps with a label (no host sync: -1 is ignored).
				lab = q["aux"][:, idx]
				aux_logits = model.aux_logits(feats, a)
				n_lab = (lab >= 0).sum().clamp_min(1)
				aux_loss = F.cross_entropy(aux_logits.reshape(-1, AUX_CLASSES), lab.reshape(-1), ignore_index=-1,
					reduction="sum") / n_lab
				aux_acc = ((aux_logits.argmax(-1) == lab) & (lab >= 0)).sum() / n_lab
				loss = pg_loss + cfg.vf_coef * v_loss - ent_coef * entropy + cfg.aux_coef * aux_loss

				self.opt.zero_grad(set_to_none=True)
				loss.backward()
				grad_norm = torch.nn.utils.clip_grad_norm_(model.parameters(), cfg.max_grad_norm)
				self.opt.step()

				with torch.no_grad():
					approx_kl = ((ratio - 1.0) - logratio).mean()
					clipfrac = ((ratio - 1.0).abs() > cfg.clip).float().mean()
				for name, v in (("loss", loss), ("pg_loss", pg_loss), ("v_loss", v_loss), ("entropy", entropy),
						("aux_loss", aux_loss), ("aux_acc", aux_acc), ("approx_kl", approx_kl), ("clipfrac", clipfrac),
						("grad_norm", grad_norm)):
					acc[name].append(v.detach().float())
				kls.append(approx_kl)
			epochs_run += 1
			if cfg.target_kl is not None and float(torch.stack(kls).mean()) > cfg.target_kl:
				break
		out = {k: float(torch.stack(v).mean()) for k, v in acc.items()}
		out["ratio_dev_first_mb"] = float(ratio_dev_first)
		out["epochs_run"] = epochs_run
		with torch.no_grad():
			v, r = self.storage.values.flatten(), self.storage.returns.flatten()
			var_r = torch.var(r)
			out["explained_variance"] = float(1.0 - torch.var(r - v) / var_r) if float(var_r) > 0 else float("nan")
		return out

	@torch.no_grad()
	def ratio_deviation(self, minibatches: Optional[int] = None) -> Dict[str, float]:
		"""Re-run the stored rollout in minibatch-sequence form from the stored chunk-start hidden states (no update):
		-> max |ratio - 1| and max |value - stored value|. Both ~0 when the sequence form reproduces the rollout."""
		q = self._sequences()
		num_seq = q["h0"].shape[0]
		k = minibatches or self.cfg.minibatches
		mb = num_seq // k
		perm = torch.randperm(num_seq, device=self.device, generator=self.gen)
		dev_r = torch.zeros((), device=self.device)
		dev_v = torch.zeros((), device=self.device)
		for j in range(k):
			idx = perm[j * mb:(j + 1) * mb]
			logits, value, _ = self._evaluate(q, idx)
			newlogp = torch.log_softmax(logits, dim=-1).gather(-1, q["actions"][:, idx].unsqueeze(-1)).squeeze(-1)
			dev_r = torch.maximum(dev_r, ((newlogp - q["logp"][:, idx]).exp() - 1.0).abs().max())
			dev_v = torch.maximum(dev_v, (value - q["values"][:, idx]).abs().max())
		return {"max_ratio_dev": float(dev_r), "max_value_dev": float(dev_v)}

	def iteration_step(self) -> Dict[str, float]:
		"""One PPO iteration (collect + update) -> stats."""
		sched = self.schedule()
		t0 = time.perf_counter()
		roll = self.collect()
		if self.cuda:
			torch.cuda.synchronize(self.device)
		t1 = time.perf_counter()
		upd = self.update(sched["ent_coef"])
		if self.cuda:
			torch.cuda.synchronize(self.device)
		t2 = time.perf_counter()
		n = self.N * self.cfg.num_steps
		self.iteration += 1
		self.decisions += n
		out = {**sched, **roll, **upd}
		out.update({"rollout_seconds": t1 - t0, "update_seconds": t2 - t1, "decisions_per_s": n / (t2 - t0),
			"rollout_decisions_per_s": n / max(t1 - t0, 1e-9), "update_samples_per_s": n * upd["epochs_run"] / max(t2 - t1, 1e-9),
			"iteration": self.iteration, "decisions": self.decisions})
		return out

	# -- persistence
	def state_dict(self) -> dict:
		return {"optimizer": self.opt.state_dict(), "lambda": self.lam, "normalizer": self.normalizer.state_dict(),
			"iteration": self.iteration, "decisions": self.decisions, "total_iterations": self.total_iterations,
			"generator": self.gen.get_state(), "ppo_config": asdict(self.cfg)}

	def load_state_dict(self, s: dict, load_optimizer: bool = True) -> None:
		if load_optimizer and "optimizer" in s:
			self.opt.load_state_dict(s["optimizer"])
		self.lam = float(s.get("lambda", self.lam))
		if "normalizer" in s:
			self.normalizer.load_state_dict(s["normalizer"])
		self.iteration = int(s.get("iteration", 0))
		self.decisions = int(s.get("decisions", 0))
		if "generator" in s:
			try:
				self.gen.set_state(s["generator"])
			except Exception:
				pass


# =====================================================================================================================
# Self-test (no DLL): GAE by hand, and the rollout / sequence-form ratio on a MockBatch
# =====================================================================================================================

def _test_gae() -> None:
	g, lam = 0.9, 0.8
	# 1 env, 3 steps; the session ends with step 1 (so step 2 is the first step of a new session).
	r = torch.tensor([[1.0], [2.0], [3.0]])
	v = torch.tensor([[0.5], [1.0], [1.5]])
	d = torch.tensor([[False], [True], [False]])
	last_v = torch.tensor([2.0])
	adv, ret = compute_gae(r, v, d, last_v, g, lam)
	# By hand: delta_2 = 3 + .9*2 - 1.5 = 3.3;             A_2 = 3.3
	#          delta_1 = 2 + 0 (session ended) - 1 = 1.0;  A_1 = 1.0 (no carry across the end)
	#          delta_0 = 1 + .9*1.0 - .5 = 1.4;            A_0 = 1.4 + .9*.8*1.0 = 2.12
	want = torch.tensor([[2.12], [1.0], [3.3]])
	assert torch.allclose(adv, want, atol=1e-6), (adv, want)
	assert torch.allclose(ret, want + v, atol=1e-6)
	# No session end: A_1 = delta_1 + g*lam*A_2 with delta_1 = 2 + .9*1.5 - 1 = 2.35 -> 2.35 + .72*3.3 = 4.726
	adv2, _ = compute_gae(r, v, torch.zeros_like(d), last_v, g, lam)
	assert abs(float(adv2[1, 0]) - 4.726) < 1e-5, adv2
	print(f"GAE hand check ok: {adv.flatten().tolist()} / {adv2.flatten().tolist()}")


def _selftest(device: str) -> None:
	import env as env_mod
	import players as players_mod
	from model import HellwalkerNet

	_test_gae()
	torch.manual_seed(0)
	for recurrent in (True, False):
		pop = players_mod.Population(0, unlocked=4)
		e = env_mod.HellwalkerVecEnv(32, pop, seed=1, backend="mock", pin_memory=(device == "cuda"), fight_len=(5, 40))
		net = HellwalkerNet(recurrent=recurrent, enc_hidden=64, embed_dim=16, hidden=64)
		cfg = PPOConfig(num_steps=64, bptt=32, minibatches=4, epochs=1)
		tr = PPOTrainer(net, e, cfg, device=device, total_iterations=2, seed=3)
		tr.collect()
		inner = int(tr.storage.starts[[t for t in range(cfg.num_steps) if t % cfg.bptt != 0]].sum())
		assert inner > 0, "the test must include session starts inside a BPTT chunk"
		dev = tr.ratio_deviation()
		assert dev["max_ratio_dev"] < 1e-5, dev
		if recurrent:
			# Negative control: with the chunk-start hidden states zeroed the sequences must NOT reproduce the rollout.
			saved = tr.storage.hidden.clone()
			tr.storage.hidden.zero_()
			bad = tr.ratio_deviation()
			tr.storage.hidden.copy_(saved)
			assert bad["max_value_dev"] > 1e-4, bad
			print(f"  negative control (zeroed chunk-start hidden): {bad}; session starts inside chunks: {inner}")
		st = tr.update(ent_coef=0.01)
		assert st["ratio_dev_first_mb"] < 1e-5, st
		assert all(math.isfinite(v) for k, v in st.items() if k != "explained_variance"), st
		print(f"ratio check ok (recurrent={recurrent}, {device}): {dev}, first-minibatch {st['ratio_dev_first_mb']:.2e}, "
			f"loss {st['loss']:.4f}")
		e.close()


if __name__ == "__main__":
	import argparse
	import os
	import sys

	sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
	ap = argparse.ArgumentParser()
	ap.add_argument("--selftest", action="store_true")
	ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
	args = ap.parse_args()
	_selftest(args.device)
