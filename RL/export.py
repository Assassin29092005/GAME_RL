"""HellwalkerRL - export the keeper's network (RL.md 8, DESIGN.md 8).

    .hwrl   the weights file the game and the tools load with HW::FRLPolicy (format in HWRLPolicy.h)
    .onnx   a single decision as an ONNX graph (tooling / NNE experiments; the game itself runs the .hwrl)

CLI:
    python export.py --ckpt <file.pt> --out <file.hwrl> [--onnx <file.onnx>]

API:
    export_hwrl(model, path, obs_layout_version=2)   write a .hwrl
    load_hwrl(path) -> OrderedDict[name, np.float32 array]   every tensor, meta.* included (round-trip tests)
    write_hwrl(path, tensors)                        the raw writer (tests use it to forge bad files)
    model_from_hwrl(path_or_tensors) -> HellwalkerNet
    export_onnx(model, path)                         inputs obs [B,obs] f32, tokens [B,K,F] int64, mask [B,A] bool,
                                                     h_in [B,H] f32 -> logits [B,A], value [B], h_out [B,H],
                                                     aux_all [B,A,aux] (the read head for every action); B dynamic
"""

from __future__ import annotations

import argparse
import contextlib
import logging
import os
import struct
import sys
import warnings
from collections import OrderedDict
from typing import Dict, Mapping, Union

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import HellwalkerNet, load_checkpoint  # noqa: E402

HWRL_MAGIC = b"HWRL"
HWRL_VERSION = 1
# RL::ObsLayoutVersion (HWRLTypes.h). The C++ loader refuses a file whose layout differs from the build's. A trainer
# that knows better (hwcore.hwrl_obs_layout_version() at training time) stores it in the checkpoint's extra
# ("obs_layout_version") and the CLI writes that instead.
OBS_LAYOUT_VERSION = 2

# The .hwrl names of the scalars, in the order HWRLPolicy.h lists them.
META_KEYS = ("meta.obs_layout_version", "meta.obs_dim", "meta.num_actions", "meta.aux_classes", "meta.enc_hidden",
             "meta.embed_dim", "meta.hidden", "meta.recurrent", "meta.history_tokens", "meta.token_fields",
             "meta.side")


def _np(t: torch.Tensor) -> np.ndarray:
    return np.ascontiguousarray(t.detach().to("cpu", torch.float32).numpy())


def hwrl_tensors(model: HellwalkerNet, obs_layout_version: int = OBS_LAYOUT_VERSION) -> "OrderedDict[str, np.ndarray]":
    """Every tensor of the .hwrl, in file order: meta.* first, then the layers (PyTorch Linear layout [out, in])."""
    out: "OrderedDict[str, np.ndarray]" = OrderedDict()
    meta = (obs_layout_version, model.obs_dim, model.num_actions, model.aux_classes, model.enc_hidden,
            model.embed_dim, model.hidden, 1 if model.recurrent else 0, model.history_tokens, model.token_fields,
            model.side)
    for key, value in zip(META_KEYS, meta):
        out[key] = np.array([float(value)], dtype=np.float32)  # shape [1]: the loader reads meta as rank-1, dim 1
    out["enc1.weight"] = _np(model.enc1.weight)
    out["enc1.bias"] = _np(model.enc1.bias)
    out["enc2.weight"] = _np(model.enc2.weight)
    out["enc2.bias"] = _np(model.enc2.bias)
    for f, emb in enumerate(model.tok_emb):
        out[f"tok.emb{f}"] = _np(emb.weight)
    # A token-less net (history_tokens 0, e.g. a future player policy) still carries a [1, EmbedDim] age tensor: the
    # loader expects { max(NTok, 1), NEmbed } and never reads it.
    out["tok.age"] = _np(model.tok_age) if model.history_tokens > 0 else np.zeros((1, model.embed_dim), np.float32)
    if model.recurrent:
        out["gru.weight_ih"] = _np(model.gru.weight_ih)
        out["gru.weight_hh"] = _np(model.gru.weight_hh)
        out["gru.bias_ih"] = _np(model.gru.bias_ih)
        out["gru.bias_hh"] = _np(model.gru.bias_hh)
    else:
        out["ff.weight"] = _np(model.ff.weight)
        out["ff.bias"] = _np(model.ff.bias)
    out["pi.weight"] = _np(model.pi.weight)
    out["pi.bias"] = _np(model.pi.bias)
    out["v.weight"] = _np(model.v.weight)
    out["v.bias"] = _np(model.v.bias)
    out["aux.weight"] = _np(model.aux.weight)
    out["aux.bias"] = _np(model.aux.bias)
    out["aux.action"] = _np(model.aux_action)
    return out


def write_hwrl(path: str, tensors: Mapping[str, np.ndarray]) -> None:
    """The raw writer: "HWRL", u32 version, u32 count, then per tensor u32 name length, name (ASCII), u32 ndim,
    u32 dims[ndim], f32 data (row-major), all little-endian. Written to a temp file and renamed (atomic)."""
    chunks = [HWRL_MAGIC, struct.pack("<II", HWRL_VERSION, len(tensors))]
    for name, arr in tensors.items():
        raw = name.encode("ascii")
        a = np.ascontiguousarray(np.asarray(arr, dtype="<f4"))
        if not 0 < len(raw) <= 256 or a.ndim < 1 or a.ndim > 4 or any(d <= 0 for d in a.shape):
            raise ValueError(f"write_hwrl: tensor {name!r} {a.shape} cannot be stored (name 1-256 chars, rank 1-4, "
                             "no empty dims)")
        chunks.append(struct.pack("<I", len(raw)))
        chunks.append(raw)
        chunks.append(struct.pack(f"<I{a.ndim}I", a.ndim, *a.shape))
        chunks.append(a.tobytes(order="C"))
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        for c in chunks:
            fh.write(c)
    os.replace(tmp, path)


def export_hwrl(model: HellwalkerNet, path: str, obs_layout_version: int = OBS_LAYOUT_VERSION) -> str:
    """Write the model as a .hwrl the C++ HW::FRLPolicy loads. Returns the path."""
    write_hwrl(path, hwrl_tensors(model, obs_layout_version))
    return path


def load_hwrl(path: str) -> "OrderedDict[str, np.ndarray]":
    """Read every tensor of a .hwrl (meta.* included) as float32 arrays with their stored shapes."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != HWRL_MAGIC:
        raise ValueError(f"{path}: not a .hwrl file")
    version, count = struct.unpack_from("<II", data, 4)
    if version != HWRL_VERSION:
        raise ValueError(f"{path}: unsupported .hwrl version {version}")
    pos = 12
    out: "OrderedDict[str, np.ndarray]" = OrderedDict()
    for _ in range(count):
        (name_len,) = struct.unpack_from("<I", data, pos)
        pos += 4
        name = data[pos:pos + name_len].decode("ascii")
        pos += name_len
        (ndim,) = struct.unpack_from("<I", data, pos)
        pos += 4
        dims = struct.unpack_from(f"<{ndim}I", data, pos)
        pos += 4 * ndim
        n = int(np.prod(dims)) if ndim else 1
        if pos + 4 * n > len(data):
            raise ValueError(f"{path}: truncated tensor {name}")
        out[name] = np.frombuffer(data, dtype="<f4", count=n, offset=pos).reshape(dims).astype(np.float32)
        pos += 4 * n
    if pos != len(data):
        raise ValueError(f"{path}: {len(data) - pos} trailing bytes")
    return out


def model_from_hwrl(src: Union[str, Mapping[str, np.ndarray]]) -> HellwalkerNet:
    """Rebuild the PyTorch network from a .hwrl (path or load_hwrl() result)."""
    t = load_hwrl(src) if isinstance(src, str) else src
    meta = {k: int(round(float(t[k][0]))) for k in META_KEYS}
    fields = meta["meta.token_fields"]
    vocab = [int(t[f"tok.emb{f}"].shape[0]) for f in range(fields)]
    net = HellwalkerNet(obs_dim=meta["meta.obs_dim"], num_actions=meta["meta.num_actions"],
                        aux_classes=meta["meta.aux_classes"], token_vocab=vocab,
                        history_tokens=meta["meta.history_tokens"], enc_hidden=meta["meta.enc_hidden"],
                        embed_dim=meta["meta.embed_dim"], hidden=meta["meta.hidden"],
                        recurrent=meta["meta.recurrent"] != 0, side=meta["meta.side"])
    state = {
        "enc1.weight": t["enc1.weight"], "enc1.bias": t["enc1.bias"],
        "enc2.weight": t["enc2.weight"], "enc2.bias": t["enc2.bias"],
        "pi.weight": t["pi.weight"], "pi.bias": t["pi.bias"], "v.weight": t["v.weight"], "v.bias": t["v.bias"],
        "aux.weight": t["aux.weight"], "aux.bias": t["aux.bias"], "aux_action": t["aux.action"],
    }
    for f in range(fields):
        state[f"tok_emb.{f}.weight"] = t[f"tok.emb{f}"]
    state["tok_age"] = t["tok.age"] if net.history_tokens > 0 else np.zeros((0, net.embed_dim), np.float32)
    if net.recurrent:
        for k in ("weight_ih", "weight_hh", "bias_ih", "bias_hh"):
            state[f"gru.{k}"] = t[f"gru.{k}"]
    else:
        state["ff.weight"] = t["ff.weight"]
        state["ff.bias"] = t["ff.bias"]
    net.load_state_dict({k: torch.from_numpy(np.array(v, dtype=np.float32)) for k, v in state.items()})
    return net


# -------------------------------------------------------------------------------------------------------------------
# ONNX
# -------------------------------------------------------------------------------------------------------------------
class OnnxStep(nn.Module):
    """One decision with the recurrent state as an explicit input / output (RL.md 8)."""

    def __init__(self, net: HellwalkerNet):
        super().__init__()
        self.net = net

    def forward(self, obs: torch.Tensor, tokens: torch.Tensor, mask: torch.Tensor, h_in: torch.Tensor):
        logits, value, h_out = self.net.step(obs, tokens, mask, h_in)
        return logits, value, h_out, self.net.aux_all(h_out)


ONNX_INPUTS = ("obs", "tokens", "mask", "h_in")
ONNX_OUTPUTS = ("logits", "value", "h_out", "aux_all")


@contextlib.contextmanager
def _quiet_exporter():
    """torch 2.11's exporter logs torchvision / pytree deprecation notices and an axis-renaming remark that say nothing
    about this graph; keep the CLI's output to what was written."""
    loggers = [logging.getLogger(n) for n in ("torch.onnx", "torch.onnx._internal.exporter._registration",
                                             "torch.utils.flop_counter")]
    levels = [lg.level for lg in loggers]
    for lg in loggers:
        lg.setLevel(logging.ERROR)
    try:
        with warnings.catch_warnings():
            warnings.filterwarnings("ignore", message=r".*LeafSpec.*", category=FutureWarning)
            warnings.filterwarnings("ignore", message=r".*axis name.*", category=UserWarning)
            yield
    finally:
        for lg, lv in zip(loggers, levels):
            lg.setLevel(lv)


def export_onnx(model: HellwalkerNet, path: str, opset: int = 18) -> str:
    """Write the single-step graph (dynamic batch) as ONE self-contained .onnx file. Tries the dynamo exporter first
    (torch's default), then the TorchScript one. Returns the path."""
    net = HellwalkerNet(**model.config)
    net.load_state_dict(model.state_dict())
    net = net.to("cpu", torch.float32).eval()
    wrapper = OnnxStep(net).eval()
    b = 2  # a non-1 example batch so no exporter specialises the batch dimension to a constant
    args = (torch.zeros(b, net.obs_dim), torch.zeros(b, net.history_tokens, net.token_fields, dtype=torch.long),
            torch.ones(b, net.num_actions, dtype=torch.bool), torch.zeros(b, net.hidden))
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    errors = []
    try:
        batch = torch.export.Dim("batch", min=1, max=1 << 20)
        with torch.no_grad(), _quiet_exporter():
            program = torch.onnx.export(wrapper, args, dynamo=True, input_names=list(ONNX_INPUTS),
                                        output_names=list(ONNX_OUTPUTS), opset_version=opset,
                                        dynamic_shapes=({0: batch}, {0: batch}, {0: batch}, {0: batch}),
                                        external_data=False, optimize=True, verbose=False)
        program.save(path, external_data=False)
        return path
    except Exception as exc:  # noqa: BLE001 - try the other exporter, report both if neither works
        errors.append(f"dynamo: {type(exc).__name__}: {exc}")
    try:
        axes = {name: {0: "batch"} for name in ONNX_INPUTS + ONNX_OUTPUTS}
        with torch.no_grad():
            torch.onnx.export(wrapper, args, path, dynamo=False, input_names=list(ONNX_INPUTS),
                              output_names=list(ONNX_OUTPUTS), opset_version=opset, dynamic_axes=axes,
                              do_constant_folding=True)
        return path
    except Exception as exc:  # noqa: BLE001
        errors.append(f"torchscript: {type(exc).__name__}: {exc}")
    raise RuntimeError("export_onnx failed:\n  " + "\n  ".join(errors))


# -------------------------------------------------------------------------------------------------------------------
# CLI
# -------------------------------------------------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Export a HellwalkerRL checkpoint as .hwrl (and optionally ONNX).")
    ap.add_argument("--ckpt", required=True, help="checkpoint written by model.save_checkpoint")
    ap.add_argument("--out", required=True, help="the .hwrl to write")
    ap.add_argument("--onnx", default=None, help="also write this single-step ONNX graph")
    ap.add_argument("--opset", type=int, default=18)
    args = ap.parse_args(argv)

    model, extra = load_checkpoint(args.ckpt)
    layout = int(extra.get("obs_layout_version", OBS_LAYOUT_VERSION)) if isinstance(extra, dict) else OBS_LAYOUT_VERSION
    export_hwrl(model, args.out, obs_layout_version=layout)
    kind = "recurrent" if model.recurrent else "feed-forward"
    params = sum(p.numel() for p in model.parameters())
    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes; {kind}, hidden {model.hidden}, {params} params, "
          f"obs layout {layout}, side {model.side})")
    if args.onnx:
        export_onnx(model, args.onnx, opset=args.opset)
        print(f"wrote {args.onnx} ({os.path.getsize(args.onnx)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
