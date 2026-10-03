"""HellwalkerRL - the keeper's network in PyTorch (RL.md 5.1, DESIGN.md 8).

This is the SAME network as the C++ forward pass the game runs (Source/HellwalkerRL/Public/HWCore/HWRLPolicy.h,
Private/HWCore/HWRLPolicy.cpp). export.py writes these weights as a .hwrl file and tests/test_parity.py checks that
both give the same numbers, so every formula below mirrors that file line for line:

    e1 = tanh(enc1(obs)); e2 = tanh(enc2(e1))                          [EncHidden]
    for each history token k (newest first) with ANY non-zero field:
        t_k = relu(sum_f Emb_f[token_k[f]] + Age[k])                  [EmbedDim]
        (a field value outside its vocabulary reads row 0, as in C++; validity is judged on the raw values)
    pool = mean_k t_k over those tokens (zeros when there are none)   [EmbedDim]
    z = concat(e2, pool)
    recurrent:     h' = GRUCell(z, h)   (PyTorch gate order r, z, n; h' = (1 - z) * n + z * h)
    feed-forward:  h' = tanh(ff(z)), h ignored (and h' is what C++ writes as the "new hidden state")
    logits = pi(h') with masked actions -> MASKED_LOGIT;  value = v(h')
    aux(a) = aux(h') + aux_action[:, a]    the read head: the player's answer to action a (12 classes)

Only torch is imported: the trainer, the exporter and the tests all build on this file, and it must stay light.

Shapes (T = time, B = batch, K = history tokens, F = token fields, A = actions, H = hidden):
    forward(obs [T,B,obs_dim], tokens [T,B,K,F] int (int8 ok), mask [T,B,A] bool/uint8, h0 [B,H], starts [T,B] bool)
        -> logits [T,B,A], value [T,B], feats [T,B,H] (hidden AFTER each step), hT [B,H]
    step(obs [B,obs_dim], tokens [B,K,F], mask [B,A], h [B,H]) -> logits [B,A], value [B], h_new [B,H]
    aux_logits(feats [...,H], actions long [...]) -> [..., aux_classes]
    aux_all(feats [...,H]) -> [..., A, aux_classes]  (the read head for every action at once; ONNX / overlays)

Checkpoints (the trainer's convention; export.py rebuilds the model from them):
    save_checkpoint(path, model, extra) / load_checkpoint(path) -> (model, extra)
"""

from __future__ import annotations

import math
import os
from typing import Any, Dict, Optional, Sequence, Tuple

import torch
import torch.nn as nn

# Masked actions get this logit (DESIGN.md 8). Large enough that softmax gives exactly 0 in float32, small enough that
# arithmetic on it (logit differences, logsumexp) stays finite. The C++ side uses -1e30; only unmasked entries are
# ever compared.
MASKED_LOGIT = -1.0e9

# HWRLTypes.h defaults (RL::ObsDim, NumActions, NumAnswerClasses, TokenVocab, HistoryTokens).
DEFAULT_TOKEN_VOCAB = (10, 13, 6, 5, 10, 4)

CHECKPOINT_FORMAT = "hellwalker-rl-checkpoint"
CHECKPOINT_VERSION = 1


def _orthogonal_(weight: torch.Tensor, gain: float) -> None:
    with torch.no_grad():
        nn.init.orthogonal_(weight, gain=gain)


class HellwalkerNet(nn.Module):
    """The keeper's policy: encoder + token pool + GRU (or feed-forward) + policy / value / read heads."""

    def __init__(self, obs_dim: int = 107, num_actions: int = 23, aux_classes: int = 12,
                 token_vocab: Sequence[int] = DEFAULT_TOKEN_VOCAB, history_tokens: int = 32, enc_hidden: int = 256,
                 embed_dim: int = 32, hidden: int = 256, recurrent: bool = True, side: int = 0):
        super().__init__()
        token_vocab = tuple(int(v) for v in token_vocab)
        if obs_dim <= 0 or num_actions <= 0 or aux_classes <= 0 or enc_hidden <= 0 or embed_dim <= 0 or hidden <= 0:
            raise ValueError("HellwalkerNet: every size must be positive")
        if history_tokens < 0 or len(token_vocab) > 16 or any(v <= 0 for v in token_vocab):
            raise ValueError("HellwalkerNet: bad token layout (HWRLPolicy.cpp allows <= 16 fields, vocab > 0)")
        # The C++ loader caps these (FRLPolicyOutput::MaxActions / MaxAux), and a boss policy's recurrent state must fit
        # the session memory (RL::MaxHidden = FRLSession::MaxHidden): the loader refuses a bigger one, so refuse it here,
        # before hours of training.
        if num_actions > 32 or aux_classes > 16:
            raise ValueError("HellwalkerNet: num_actions <= 32 and aux_classes <= 16 (HWRLPolicy.h)")
        if side not in (0, 1):
            raise ValueError("HellwalkerNet: side is 0 (the keeper) or 1 (a player / exploiter)")
        if side == 0 and hidden > 512:
            raise ValueError("HellwalkerNet: a keeper's hidden size must be <= 512 (RL::MaxHidden, the session memory)")

        self.obs_dim = int(obs_dim)
        self.num_actions = int(num_actions)
        self.aux_classes = int(aux_classes)
        self.token_vocab = token_vocab
        self.history_tokens = int(history_tokens)
        self.token_fields = len(token_vocab)
        self.enc_hidden = int(enc_hidden)
        self.embed_dim = int(embed_dim)
        self.hidden = int(hidden)
        self.recurrent = bool(recurrent)
        self.side = int(side)

        self.enc1 = nn.Linear(self.obs_dim, self.enc_hidden)
        self.enc2 = nn.Linear(self.enc_hidden, self.enc_hidden)
        self.tok_emb = nn.ModuleList(nn.Embedding(v, self.embed_dim) for v in token_vocab)
        self.tok_age = nn.Parameter(torch.zeros(self.history_tokens, self.embed_dim))
        z_dim = self.enc_hidden + self.embed_dim
        if self.recurrent:
            self.gru = nn.GRUCell(z_dim, self.hidden)
        else:
            self.ff = nn.Linear(z_dim, self.hidden)
        self.pi = nn.Linear(self.hidden, self.num_actions)
        self.v = nn.Linear(self.hidden, 1)
        self.aux = nn.Linear(self.hidden, self.aux_classes)
        self.aux_action = nn.Parameter(torch.zeros(self.aux_classes, self.num_actions))

        # Token lookups go through ONE concatenated table (see _pool_tokens): each field's index is offset by the vocab
        # sizes before it. Not persistent: they follow from token_vocab.
        offsets = [0]
        for v in token_vocab[:-1]:
            offsets.append(offsets[-1] + v)
        self.register_buffer("_tok_offsets", torch.tensor(offsets, dtype=torch.long), persistent=False)
        self.register_buffer("_tok_vocab", torch.tensor(token_vocab, dtype=torch.long), persistent=False)

        self.reset_parameters()

    # ---------------------------------------------------------------------------------------------------------------
    # construction
    # ---------------------------------------------------------------------------------------------------------------
    @property
    def config(self) -> Dict[str, Any]:
        """The constructor kwargs (plain types): HellwalkerNet(**net.config) rebuilds the same architecture."""
        return {
            "obs_dim": self.obs_dim, "num_actions": self.num_actions, "aux_classes": self.aux_classes,
            "token_vocab": list(self.token_vocab), "history_tokens": self.history_tokens, "enc_hidden": self.enc_hidden,
            "embed_dim": self.embed_dim, "hidden": self.hidden, "recurrent": self.recurrent, "side": self.side,
        }

    def reset_parameters(self) -> None:
        """Random initialisation for PPO from nothing (RL.md 4.3): orthogonal MLPs, a near-uniform initial policy
        (pi gain 0.01) so early exploration is not biased by the init, and a read head that starts at 'no idea'."""
        for layer in (self.enc1, self.enc2):
            _orthogonal_(layer.weight, math.sqrt(2.0))
            nn.init.zeros_(layer.bias)
        # Unit variance before the relu: each of the F fields contributes 1/F of it.
        std = 1.0 / math.sqrt(max(1, self.token_fields))
        for emb in self.tok_emb:
            nn.init.normal_(emb.weight, 0.0, std)
        nn.init.zeros_(self.tok_age)
        if self.recurrent:
            # Orthogonal per gate block (r, z, n), zero biases: the usual recurrent-PPO init.
            for w in (self.gru.weight_ih, self.gru.weight_hh):
                for block in w.data.chunk(3, 0):
                    _orthogonal_(block, 1.0)
            nn.init.zeros_(self.gru.bias_ih)
            nn.init.zeros_(self.gru.bias_hh)
        else:
            _orthogonal_(self.ff.weight, math.sqrt(2.0))
            nn.init.zeros_(self.ff.bias)
        _orthogonal_(self.pi.weight, 0.01)
        nn.init.zeros_(self.pi.bias)
        _orthogonal_(self.v.weight, 1.0)
        nn.init.zeros_(self.v.bias)
        _orthogonal_(self.aux.weight, 0.01)
        nn.init.zeros_(self.aux.bias)
        nn.init.zeros_(self.aux_action)

    def initial_state(self, batch: int, device: Optional[torch.device] = None) -> torch.Tensor:
        """[B, hidden] zeros: the memory of a new session (the feed-forward net ignores it)."""
        if device is None:
            device = self.pi.weight.device
        return torch.zeros(batch, self.hidden, device=device, dtype=self.pi.weight.dtype)

    # ---------------------------------------------------------------------------------------------------------------
    # pieces shared by forward() and step()
    # ---------------------------------------------------------------------------------------------------------------
    def _pool_tokens(self, tokens: torch.Tensor) -> torch.Tensor:
        """tokens [N, K, F] (any int dtype) -> pool [N, EmbedDim]."""
        n = tokens.shape[0]
        dtype = self.enc1.weight.dtype
        if self.history_tokens == 0 or self.token_fields == 0:
            return torch.zeros(n, self.embed_dim, device=tokens.device, dtype=dtype)
        k, f = self.history_tokens, self.token_fields
        t = tokens.reshape(n * k, f).long()
        valid = (t != 0).any(dim=-1).view(n, k)  # judged on the raw values, as HWRLPolicy.cpp does
        idx = torch.where((t >= 0) & (t < self._tok_vocab), t, torch.zeros_like(t)) + self._tok_offsets
        # sum_f Emb_f[token[f]] as (multi-hot over the concatenated vocab) @ (concatenated tables): the fields' index
        # ranges are disjoint, so each row holds exactly F ones. A plain matmul both ways - about 4x faster than
        # embedding_bag for forward + backward (no atomic scatter into the tiny tables) and it exports to ONNX as
        # ScatterElements + MatMul (embedding_bag becomes a Loop).
        table = torch.cat([emb.weight for emb in self.tok_emb], dim=0)
        multihot = torch.zeros(n * k, table.shape[0], device=t.device, dtype=dtype).scatter_(1, idx, 1.0)
        summed = torch.matmul(multihot, table).view(n, k, self.embed_dim)
        vec = torch.relu(summed + self.tok_age)  # [N, K, E]
        w = valid.to(dtype)
        count = w.sum(dim=-1, keepdim=True).clamp_min(1.0)  # no token -> 0 / 1 = zeros
        return torch.bmm(w.unsqueeze(1), vec).squeeze(1) / count

    def _features(self, obs: torch.Tensor, tokens: torch.Tensor) -> torch.Tensor:
        """obs [N, obs_dim], tokens [N, K, F] -> z [N, EncHidden + EmbedDim]."""
        e = torch.tanh(self.enc2(torch.tanh(self.enc1(obs))))
        return torch.cat([e, self._pool_tokens(tokens)], dim=-1)

    @staticmethod
    def _apply_mask(logits: torch.Tensor, mask: Optional[torch.Tensor]) -> torch.Tensor:
        if mask is None:
            return logits
        if mask.dtype != torch.bool:
            mask = mask != 0
        return logits.masked_fill(~mask, MASKED_LOGIT)

    # ---------------------------------------------------------------------------------------------------------------
    # the public API (DESIGN.md 8)
    # ---------------------------------------------------------------------------------------------------------------
    def forward(self, obs: torch.Tensor, tokens: torch.Tensor, mask: Optional[torch.Tensor], h0: Optional[torch.Tensor],
                starts: Optional[torch.Tensor] = None) -> Tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        """Sequence form for PPO. Everything except the recurrence is batched over T*B; the GRU loops over T.
        starts[t] resets that env's hidden state to zeros BEFORE step t (a new session)."""
        t_len, batch = obs.shape[0], obs.shape[1]
        z = self._features(obs.reshape(t_len * batch, self.obs_dim),
                           tokens.reshape(t_len * batch, self.history_tokens, self.token_fields))
        if self.recurrent:
            # nn.GRUCell per step: on CUDA it runs the fused gate kernel, which measured faster than a hand-written
            # cell with the input projection batched over T. unbind (not z[t]) matters as much: the backward of each
            # z[t] slice would materialise a full [T, B, ...] zero gradient, one per step.
            zs = z.view(t_len, batch, -1).unbind(0)
            resets = None
            if starts is not None:
                resets = (starts if starts.dtype == torch.bool else starts != 0).unsqueeze(-1).unbind(0)
            h = h0 if h0 is not None else self.initial_state(batch, obs.device)
            outs = []
            for t in range(t_len):
                if resets is not None:
                    h = h.masked_fill(resets[t], 0.0)
                h = self.gru(zs[t], h)
                outs.append(h)
            feats = torch.stack(outs, dim=0)
            h_last = h
        else:
            feats = torch.tanh(self.ff(z)).view(t_len, batch, self.hidden)
            h_last = feats[-1]
        logits = self._apply_mask(self.pi(feats), mask)
        value = self.v(feats).squeeze(-1)
        return logits, value, feats, h_last

    def step(self, obs: torch.Tensor, tokens: torch.Tensor, mask: Optional[torch.Tensor],
             h: Optional[torch.Tensor]) -> Tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        """One decision for B envs (rollouts, evaluation, play). Same math as one step of forward()."""
        z = self._features(obs, tokens)
        if self.recurrent:
            if h is None:
                h = self.initial_state(obs.shape[0], obs.device)
            h_new = self.gru(z, h)
        else:
            h_new = torch.tanh(self.ff(z))
        logits = self._apply_mask(self.pi(h_new), mask)
        value = self.v(h_new).squeeze(-1)
        return logits, value, h_new

    def aux_logits(self, feats: torch.Tensor, actions: torch.Tensor) -> torch.Tensor:
        """The read head conditioned on the action taken: aux(feats) + aux_action[:, a]. feats are the hidden states
        AFTER the step (forward's feats / step's h_new). An action outside [0, A) adds no action term (as in C++)."""
        base = self.aux(feats)
        a = actions.long()
        ok = (a >= 0) & (a < self.num_actions)
        term = self.aux_action.t()[a.clamp(0, self.num_actions - 1)]
        return torch.where(ok.unsqueeze(-1), base + term, base)

    def aux_all(self, feats: torch.Tensor) -> torch.Tensor:
        """The read head for every action: [..., A, aux_classes]."""
        return self.aux(feats).unsqueeze(-2) + self.aux_action.t()


# -------------------------------------------------------------------------------------------------------------------
# checkpoints
# -------------------------------------------------------------------------------------------------------------------
def _plain(value: Any, where: str) -> Any:
    """Convert `extra` to what torch.load(weights_only=True) can read back (so resuming never needs pickle):
    dict / list / tuple / str / int / float / bool / None / tensors (moved to CPU). NumPy arrays become tensors and
    NumPy scalars Python numbers. Anything else is refused NOW, at save time, rather than at resume time."""
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, torch.Tensor):
        return value.detach().cpu()
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            if not isinstance(k, (str, int)):
                raise TypeError(f"save_checkpoint: extra{where} has a key of type {type(k).__name__} (use str / int)")
            out[k] = _plain(v, f"{where}[{k!r}]")
        return out
    if isinstance(value, (list, tuple)):
        items = [_plain(v, f"{where}[{i}]") for i, v in enumerate(value)]
        return items if isinstance(value, list) else tuple(items)
    # NumPy without importing it: arrays have .__array__ + .dtype, scalars have .item().
    mod = type(value).__module__
    if mod == "numpy" or mod.startswith("numpy."):
        if hasattr(value, "shape") and getattr(value, "shape", ()) != ():
            return torch.from_numpy(value.copy())
        return value.item()
    raise TypeError(f"save_checkpoint: extra{where} is a {type(value).__name__}; store plain types or tensors")


def save_checkpoint(path: str, model: HellwalkerNet, extra: Optional[Dict[str, Any]] = None) -> None:
    """Write {format, version, kwargs (the constructor), state_dict, extra} atomically (a crash mid-save never leaves
    a truncated checkpoint behind). `extra` is the trainer's: optimizer state, step counters, curriculum, the
    obs_layout_version it trained with (export.py writes it into the .hwrl), ... - plain types and tensors only."""
    payload = {
        "format": CHECKPOINT_FORMAT,
        "version": CHECKPOINT_VERSION,
        "kwargs": model.config,
        "state_dict": {k: v.detach().cpu() for k, v in model.state_dict().items()},
        "extra": _plain(extra if extra is not None else {}, ""),
    }
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    tmp = path + ".tmp"
    torch.save(payload, tmp)
    os.replace(tmp, path)


def load_checkpoint(path: str, map_location: Any = "cpu") -> Tuple[HellwalkerNet, Dict[str, Any]]:
    """-> (model rebuilt from the stored kwargs with the stored weights, extra). Uses weights_only=True: loading a
    checkpoint never runs pickled code."""
    payload = torch.load(path, map_location=map_location, weights_only=True)
    if not isinstance(payload, dict) or payload.get("format") != CHECKPOINT_FORMAT:
        raise ValueError(f"{path}: not a HellwalkerRL checkpoint (see model.save_checkpoint)")
    if int(payload.get("version", 0)) > CHECKPOINT_VERSION:
        raise ValueError(f"{path}: checkpoint version {payload.get('version')} is newer than this model.py")
    model = HellwalkerNet(**payload["kwargs"])
    model.load_state_dict(payload["state_dict"])
    return model, payload.get("extra", {})
