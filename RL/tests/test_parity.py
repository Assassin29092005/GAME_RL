"""HellwalkerRL - parity between the PyTorch network (RL/model.py) and the game's C++ forward pass (HWRLPolicy.cpp).

The C++ side is RL/native/out_harness/policy_harness.exe (RL/native/build_harness.bat), which links the exact
HWCore/HWRLPolicy.cpp the game runs. For random networks - recurrent AND feed-forward, small and full size, weights
at a non-trivial scale - and random inputs (padding token rows, partial rows, out-of-vocab values, random masks, random
hidden states), every output must agree within 1e-4 absolute:
    logits (unmasked entries), action probabilities, value, new hidden state, read-head (aux) probabilities;
    argmax equal except for genuine near-ties (top-two logits closer than the tolerance).
Three input modes: independent decisions with mask + hidden state; independent decisions with NO mask and NO hidden
state (the C++ null paths); and sequences played step by step in C++ exactly like FRLBrain (state aliasing, resets at
session starts) against model.forward() over [T, B].

Also: .hwrl round trip (export -> load_hwrl -> identical tensors; rebuild -> identical state; re-write -> identical
bytes), the checkpoint convention and the export.py CLI, the C++ loader refusing bad files, and - when onnxruntime
imports - the ONNX graph against torch.

Run:   RL\\.venv\\Scripts\\python.exe RL\\tests\\test_parity.py [--rebuild] [--no-cuda] [--no-onnx] [--seed N]
Exit code 0 = every check passed. Temp files go to RL/.pip-tmp/parity.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import time
import traceback
from typing import Dict, List, Optional

import numpy as np
import torch

RL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, RL_DIR)
import export as E  # noqa: E402
import model as M  # noqa: E402

ROOT = os.path.dirname(RL_DIR)
NATIVE = os.path.join(RL_DIR, "native")
HARNESS = os.path.join(NATIVE, "out_harness", "policy_harness.exe")
BUILD = os.path.join(NATIVE, "build_harness.bat")
HARNESS_SOURCES = [
    os.path.join(NATIVE, "policy_harness.cpp"),
    os.path.join(ROOT, "Source", "HellwalkerRL", "Private", "HWCore", "HWRLPolicy.cpp"),
    os.path.join(ROOT, "Source", "HellwalkerRL", "Public", "HWCore", "HWRLPolicy.h"),
    os.path.join(ROOT, "Source", "HellwalkerRL", "Public", "HWCore", "HWRLTypes.h"),
]
TMP = os.path.join(RL_DIR, ".pip-tmp", "parity")
TOL = 1e-4
CPP_MASKED = -1.0e30

# HWRLTypes.h: a boss policy must have exactly these (the C++ loader checks).
OBS_DIM, NUM_ACTIONS, AUX, K_TOK, F_TOK = 107, 23, 12, 32, 6
VOCAB = np.array(M.DEFAULT_TOKEN_VOCAB, dtype=np.int64)
WAIT = NUM_ACTIONS - 1

VARIANTS = [
    # name, recurrent, enc_hidden, embed_dim, hidden
    ("recurrent-small", True, 64, 16, 40),
    ("recurrent-full", True, 256, 32, 256),
    ("feedforward-small", False, 48, 8, 24),
    ("feedforward-full", False, 256, 32, 256),
]

RESULTS: List[str] = []


def log(msg: str) -> None:
    print(msg, flush=True)
    RESULTS.append(msg)


# -------------------------------------------------------------------------------------------------------------------
# harness build / io
# -------------------------------------------------------------------------------------------------------------------
def ensure_harness(force: bool = False) -> None:
    """Build the harness if it is missing, older than its sources, or asked to."""
    stale = not os.path.exists(HARNESS)
    if not stale:
        exe_time = os.path.getmtime(HARNESS)
        stale = any(os.path.getmtime(s) > exe_time for s in HARNESS_SOURCES if os.path.exists(s))
    if not (force or stale):
        return
    print(f"building {HARNESS} ...", flush=True)
    proc = subprocess.run(["cmd", "/c", BUILD], capture_output=True, text=True)
    out = (proc.stdout + proc.stderr).strip()
    # vcvars prints a 'vswhere.exe' notice on some installs; anything cl prints beyond the file names is a warning.
    lines = [ln for ln in out.splitlines() if ln.strip() and "vswhere" not in ln and "operable program" not in ln]
    noise = [ln for ln in lines if not ln.strip().endswith(".cpp") and ln.strip() != "Generating Code..."]
    if proc.returncode != 0 or not os.path.exists(HARNESS):
        raise RuntimeError("build_harness.bat failed:\n" + out)
    if noise:
        raise RuntimeError("build_harness.bat produced warnings:\n" + "\n".join(noise))
    print("  built, no warnings", flush=True)


def write_input(path: str, inp: Dict[str, Optional[np.ndarray]], hidden: int, seq_len: int) -> None:
    n = inp["obs"].shape[0]
    flags = (1 if inp.get("mask") is not None else 0) | (2 if inp.get("h_in") is not None else 0) \
        | (4 if inp.get("starts") is not None else 0)
    parts = [b"HWPI", struct.pack("<9i", 1, n, OBS_DIM, K_TOK, F_TOK, NUM_ACTIONS, hidden, flags, seq_len),
             np.ascontiguousarray(inp["obs"], "<f4").tobytes(),
             np.ascontiguousarray(inp["tokens"], np.int8).tobytes()]
    if inp.get("mask") is not None:
        parts.append(np.ascontiguousarray(inp["mask"], np.uint8).tobytes())
    if inp.get("h_in") is not None:
        parts.append(np.ascontiguousarray(inp["h_in"], "<f4").tobytes())
    if inp.get("starts") is not None:
        parts.append(np.ascontiguousarray(inp["starts"], np.uint8).tobytes())
    parts.append(np.ascontiguousarray(inp["actions"], "<i4").tobytes())
    with open(path, "wb") as fh:
        fh.write(b"".join(parts))


def read_output(path: str) -> Dict[str, np.ndarray]:
    with open(path, "rb") as fh:
        data = fh.read()
    assert data[:4] == b"HWPO", "harness output: bad magic"
    ver, n, a, h, c = struct.unpack_from("<5i", data, 4)
    assert ver == 1
    pos = 24
    out = {}
    for name, count, dt, shape in (("logits", n * a, "<f4", (n, a)), ("probs", n * a, "<f4", (n, a)),
                                   ("value", n, "<f4", (n,)), ("h_out", n * h, "<f4", (n, h)),
                                   ("argmax", n, "<i4", (n,)), ("aux", n * c, "<f4", (n, c))):
        out[name] = np.frombuffer(data, dtype=dt, count=count, offset=pos).reshape(shape).copy()
        pos += 4 * count
    assert pos == len(data), "harness output: size mismatch"
    return out


def run_harness(hwrl: str, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    proc = subprocess.run([HARNESS, hwrl, *args], capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise RuntimeError(f"policy_harness {' '.join(args)} failed ({proc.returncode}): {proc.stderr.strip()}")
    return proc


# -------------------------------------------------------------------------------------------------------------------
# random networks and inputs
# -------------------------------------------------------------------------------------------------------------------
def make_net(recurrent: bool, enc: int, emb: int, hid: int, gen: torch.Generator) -> M.HellwalkerNet:
    """A network with every parameter random at a non-trivial scale: pre-activations O(1-2) (tanh / sigmoid well
    into their non-linear range), logits spread over a few units, a read head with real action terms."""
    net = M.HellwalkerNet(enc_hidden=enc, embed_dim=emb, hidden=hid, recurrent=recurrent)
    with torch.no_grad():
        for name, p in net.named_parameters():
            if name.split(".")[-1].startswith("bias"):  # Linear .bias, GRUCell .bias_ih / .bias_hh
                p.copy_(torch.randn(p.shape, generator=gen) * 0.3)
            elif name.startswith("tok_emb"):
                p.copy_(torch.randn(p.shape, generator=gen) * 0.5)
            elif name == "tok_age":
                p.copy_(torch.randn(p.shape, generator=gen) * 0.3)
            elif name == "aux_action":
                p.copy_(torch.randn(p.shape, generator=gen) * 1.0)
            else:  # [out, in] weights
                fan_in = p.shape[1]
                gain = 2.0 if name.startswith(("pi.", "aux.", "v.")) else 1.5
                p.copy_(torch.randn(p.shape, generator=gen) * (gain / np.sqrt(fan_in)))
    return net.eval()


def make_inputs(n: int, hidden: int, rng: np.random.Generator, with_mask: bool = True,
                with_hidden: bool = True) -> Dict[str, Optional[np.ndarray]]:
    obs = np.clip(rng.normal(0.0, 1.0, (n, OBS_DIM)), -3, 3).astype(np.float32)
    obs[:, :30] = (rng.random((n, 30)) < 0.2).astype(np.float32)  # one-hot-like block

    tokens = np.zeros((n, K_TOK, F_TOK), dtype=np.int64)
    for i in range(n):
        choice = i % 6
        n_valid = 0 if choice == 0 else K_TOK if choice == 1 else int(rng.integers(1, K_TOK))
        vals = rng.integers(0, VOCAB, size=(n_valid, F_TOK))
        vals[:, 0] = rng.integers(1, VOCAB[0], size=n_valid)  # the boss field is always set in a real token
        partial = rng.random((n_valid, F_TOK)) < 0.25  # some zero fields inside valid tokens
        partial[:, 0] = False
        vals[partial] = 0
        tokens[i, :n_valid] = vals
        if n_valid > 4 and choice >= 4:
            # padding rows in the middle and tokens made only of out-of-vocab / zero fields
            holes = rng.choice(n_valid, size=2, replace=False)
            tokens[i, holes] = 0
            odd = int(rng.integers(0, n_valid))
            tokens[i, odd] = 0
            tokens[i, odd, int(rng.integers(0, F_TOK))] = int(rng.choice([-3, -128, 40, 127]))
        if choice == 5 and n_valid > 0:
            # scattered out-of-vocab values in otherwise normal tokens (C++ reads row 0 for them)
            k = int(rng.integers(0, n_valid))
            tokens[i, k, int(rng.integers(1, F_TOK))] = int(rng.choice([-1, 13, 99, -77]))
    tokens = tokens.astype(np.int8)

    mask = None
    if with_mask:
        density = rng.uniform(0.1, 1.0, size=(n, 1))
        mask = (rng.random((n, NUM_ACTIONS)) < density).astype(np.uint8)
        mask[::7] = 1                  # all legal
        mask[3::11] = 0                # only Wait
        mask[:, WAIT] = 1              # Wait is always legal (DESIGN.md 1)
    h_in = rng.uniform(-1.0, 1.0, (n, hidden)).astype(np.float32) if with_hidden else None
    actions = rng.integers(0, NUM_ACTIONS, size=n).astype(np.int32)
    actions[rng.random(n) < 0.05] = -1  # no action term (C++ Aux skips an out-of-range action)
    return {"obs": obs, "tokens": tokens, "mask": mask, "h_in": h_in, "actions": actions}


# -------------------------------------------------------------------------------------------------------------------
# torch references
# -------------------------------------------------------------------------------------------------------------------
def _t(x: Optional[np.ndarray], device: str, dtype=None) -> Optional[torch.Tensor]:
    if x is None:
        return None
    t = torch.from_numpy(np.ascontiguousarray(x)).to(device)
    return t.to(dtype) if dtype is not None else t


def torch_independent(net: M.HellwalkerNet, inp, device: str) -> Dict[str, np.ndarray]:
    net = net.to(device)
    with torch.no_grad():
        mask = _t(inp["mask"], device, torch.bool)
        logits, value, h_new = net.step(_t(inp["obs"], device), _t(inp["tokens"], device), mask,
                                        _t(inp["h_in"], device))
        aux = torch.softmax(net.aux_logits(h_new, _t(inp["actions"], device).long()), dim=-1)
        probs = torch.softmax(logits, dim=-1)
    return {k: v.cpu().numpy() for k, v in
            {"logits": logits, "probs": probs, "value": value, "h_out": h_new, "aux": aux}.items()}


def torch_sequence(net: M.HellwalkerNet, inp, t_len: int, batch: int, device: str) -> Dict[str, np.ndarray]:
    net = net.to(device)

    def seq(x, dtype=None):
        if x is None:
            return None
        return _t(x.reshape(t_len, batch, *x.shape[1:]), device, dtype)

    with torch.no_grad():
        logits, value, feats, h_last = net(seq(inp["obs"]), seq(inp["tokens"]), seq(inp["mask"], torch.bool),
                                           _t(inp["h_in"], device), seq(inp["starts"], torch.bool))
        aux = torch.softmax(net.aux_logits(feats, seq(inp["actions"]).long()), dim=-1)
        probs = torch.softmax(logits, dim=-1)
    flat = lambda x: x.reshape(t_len * batch, *x.shape[2:]).cpu().numpy()  # noqa: E731
    return {"logits": flat(logits), "probs": flat(probs), "value": flat(value), "h_out": flat(feats),
            "aux": flat(aux), "h_last": h_last.cpu().numpy()}


# -------------------------------------------------------------------------------------------------------------------
# comparison
# -------------------------------------------------------------------------------------------------------------------
def compare(label: str, cpp: Dict[str, np.ndarray], ref: Dict[str, np.ndarray], mask: Optional[np.ndarray]) -> Dict:
    allowed = np.ones_like(cpp["logits"], dtype=bool) if mask is None else mask.astype(bool)
    errs = {
        "logits": float(np.abs(cpp["logits"] - ref["logits"])[allowed].max()),
        "probs": float(np.abs(cpp["probs"] - ref["probs"]).max()),
        "value": float(np.abs(cpp["value"] - ref["value"]).max()),
        "h_out": float(np.abs(cpp["h_out"] - ref["h_out"]).max()),
        "aux": float(np.abs(cpp["aux"] - ref["aux"]).max()),
    }
    # C++ writes exactly -1e30 for masked actions; torch MASKED_LOGIT. Both must mark the same entries.
    assert np.all(cpp["logits"][~allowed] == np.float32(CPP_MASKED)), f"{label}: C++ masked logits are not -1e30"
    assert np.all(ref["logits"][~allowed] <= M.MASKED_LOGIT / 2), f"{label}: torch masked logits are not masked"
    # argmax: equal unless the two candidates are a genuine near-tie in torch's own logits.
    ref_arg = np.where(allowed, ref["logits"], -np.inf).argmax(axis=1)
    diff = np.nonzero(ref_arg != cpp["argmax"])[0]
    rows = np.arange(len(ref_arg))
    gaps = np.abs(ref["logits"][diff, ref_arg[diff]] - ref["logits"][diff, cpp["argmax"][diff]]) if len(diff) else []
    near_ties = int(sum(g < TOL for g in gaps))
    real = len(diff) - near_ties
    assert np.all(allowed[rows, cpp["argmax"]]), f"{label}: C++ argmax picked a masked action"
    worst = max(errs.values())
    status = "ok" if worst <= TOL and real == 0 else "FAIL"
    log(f"  {label:<44s} logits {errs['logits']:.2e}  probs {errs['probs']:.2e}  value {errs['value']:.2e}  "
        f"h_out {errs['h_out']:.2e}  aux {errs['aux']:.2e}  argmax diff {len(diff)} (near-ties {near_ties})  {status}")
    assert worst <= TOL, f"{label}: max abs error {worst:.3e} > {TOL}"
    assert real == 0, f"{label}: {real} argmax mismatches that are not near-ties"
    errs["argmax_mismatch"] = len(diff)
    errs["near_ties"] = near_ties
    return errs


# -------------------------------------------------------------------------------------------------------------------
# tests
# -------------------------------------------------------------------------------------------------------------------
def test_parity_harness(seed: int = 1, use_cuda: bool = True) -> Dict[str, Dict[str, float]]:
    ensure_harness()
    os.makedirs(TMP, exist_ok=True)
    gen = torch.Generator().manual_seed(seed)
    rng = np.random.default_rng(seed)
    devices = ["cpu"] + (["cuda"] if use_cuda and torch.cuda.is_available() else [])
    # The trainer's GPU numbers are only comparable with full float32 matmuls.
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    summary: Dict[str, Dict[str, float]] = {}
    for name, rec, enc, emb, hid in VARIANTS:
        log(f"[{name}] recurrent={rec} enc_hidden={enc} embed_dim={emb} hidden={hid}")
        net = make_net(rec, enc, emb, hid, gen)
        hwrl = E.export_hwrl(net, os.path.join(TMP, f"{name}.hwrl"))
        info = run_harness(hwrl, "--info").stdout.strip()
        assert ("recurrent" if rec else "feed-forward") in info and f"hidden {hid}" in info, info
        worst: Dict[str, float] = {}

        cases = []
        # 1) independent decisions with mask and hidden state
        inp = make_inputs(768, hid, rng)
        cases.append(("independent, mask + h_in", inp, 0, 0))
        # 2) no mask, no hidden state: FRLPolicy's null paths vs torch's (mask None, h None = zeros)
        inp = make_inputs(96, hid, rng, with_mask=False, with_hidden=False)
        cases.append(("independent, no mask, no h_in", inp, 0, 0))
        # 3) sequences stepped like FRLBrain (state in place, resets at session starts) vs forward() over [T, B]
        t_len, batch = 24, 20
        inp = make_inputs(t_len * batch, hid, rng)
        inp["h_in"] = inp["h_in"][:batch]
        starts = (rng.random((t_len, batch)) < 0.12)
        starts[0, ::3] = True
        inp["starts"] = starts.astype(np.uint8).reshape(-1)
        cases.append((f"sequence T={t_len} B={batch}, starts", inp, t_len, batch))

        for cname, inp, t_len, batch in cases:
            in_path = os.path.join(TMP, f"{name}.in.bin")
            out_path = os.path.join(TMP, f"{name}.out.bin")
            write_input(in_path, inp, hid, t_len)
            run_harness(hwrl, in_path, out_path)
            cpp = read_output(out_path)
            for dev in devices:
                ref = torch_independent(net, inp, dev) if t_len == 0 else torch_sequence(net, inp, t_len, batch, dev)
                errs = compare(f"{cname} [{dev}]", cpp, ref, inp["mask"])
                for k in ("logits", "probs", "value", "h_out", "aux"):
                    worst[k] = max(worst.get(k, 0.0), errs[k])
            net = net.cpu()
        summary[name] = worst

        if name == "recurrent-full":
            bench = run_harness(hwrl, "--bench", "20000").stdout.strip()
            log(f"  C++ {bench}")
    return summary


def test_roundtrip(seed: int = 2) -> None:
    os.makedirs(TMP, exist_ok=True)
    gen = torch.Generator().manual_seed(seed)
    for name, rec, enc, emb, hid in VARIANTS[:3]:
        net = make_net(rec, enc, emb, hid, gen)
        path = E.export_hwrl(net, os.path.join(TMP, f"rt-{name}.hwrl"))
        tensors = E.load_hwrl(path)
        expected = E.hwrl_tensors(net)
        assert list(tensors.keys()) == list(expected.keys()), f"{name}: tensor names / order differ"
        # the exact names HWRLPolicy.h documents
        must = {"enc1.weight", "enc1.bias", "enc2.weight", "enc2.bias", "tok.age", "pi.weight", "pi.bias", "v.weight",
                "v.bias", "aux.weight", "aux.bias", "aux.action"} | {f"tok.emb{f}" for f in range(F_TOK)} \
            | ({"gru.weight_ih", "gru.weight_hh", "gru.bias_ih", "gru.bias_hh"} if rec else {"ff.weight", "ff.bias"}) \
            | set(E.META_KEYS)
        assert set(tensors.keys()) == must, f"{name}: names {sorted(set(tensors) ^ must)}"
        for k, v in expected.items():
            assert tensors[k].shape == v.shape and np.array_equal(tensors[k], v), f"{name}: {k} differs after load"
        meta = {k: float(tensors[k][0]) for k in E.META_KEYS}
        assert meta == {"meta.obs_layout_version": 3.0, "meta.obs_dim": 107.0, "meta.num_actions": 23.0,
                        "meta.aux_classes": 12.0, "meta.enc_hidden": float(enc), "meta.embed_dim": float(emb),
                        "meta.hidden": float(hid), "meta.recurrent": 1.0 if rec else 0.0,
                        "meta.history_tokens": 32.0, "meta.token_fields": 6.0, "meta.side": 0.0}, meta
        assert all(tensors[k].shape == (1,) for k in E.META_KEYS), "meta scalars must be rank-1 [1]"
        # parameters: model -> .hwrl -> model is exact
        rebuilt = E.model_from_hwrl(path)
        a, b = net.state_dict(), rebuilt.state_dict()
        assert a.keys() == b.keys() and all(torch.equal(a[k], b[k]) for k in a), f"{name}: rebuilt state differs"
        assert rebuilt.config == net.config, f"{name}: rebuilt config differs"
        # writer is deterministic: re-writing what was read gives the same bytes
        again = os.path.join(TMP, f"rt-{name}.again.hwrl")
        E.write_hwrl(again, tensors)
        with open(path, "rb") as f1, open(again, "rb") as f2:
            assert f1.read() == f2.read(), f"{name}: re-written file differs"
    log("  .hwrl round trip (names, shapes, meta, exact values, rebuild, deterministic bytes)       ok")

    # checkpoint convention + the CLI
    net = make_net(True, 64, 16, 40, gen)
    ckpt = os.path.join(TMP, "rt.pt")
    extra = {"step": 123, "lr": 3e-4, "obs_layout_version": 3, "nested": {"band": [0, 1], "arr": np.arange(3.0)},
             "scalar": np.float32(0.5), "t": torch.ones(2), "tup": (1, "a")}
    M.save_checkpoint(ckpt, net, extra)
    net2, extra2 = M.load_checkpoint(ckpt)
    a, b = net.state_dict(), net2.state_dict()
    assert a.keys() == b.keys() and all(torch.equal(a[k], b[k]) for k in a), "checkpoint state differs"
    assert net2.config == net.config
    assert extra2["step"] == 123 and extra2["nested"]["band"] == [0, 1] and extra2["tup"] == (1, "a")
    assert torch.equal(extra2["nested"]["arr"], torch.arange(3.0, dtype=torch.float64))
    assert isinstance(extra2["scalar"], float) and extra2["scalar"] == 0.5 and torch.equal(extra2["t"], torch.ones(2))
    try:
        M.save_checkpoint(os.path.join(TMP, "bad.pt"), net, {"obj": object()})
        raise AssertionError("save_checkpoint accepted an arbitrary object in extra")
    except TypeError:
        pass
    out = os.path.join(TMP, "cli.hwrl")
    onnx_out = os.path.join(TMP, "cli.onnx")
    cmd = [sys.executable, os.path.join(RL_DIR, "export.py"), "--ckpt", ckpt, "--out", out]
    have_onnx = _have_onnxruntime()
    if have_onnx:
        cmd += ["--onnx", onnx_out]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    assert proc.returncode == 0, f"export.py CLI failed: {proc.stderr[-2000:]}"
    direct = E.export_hwrl(net, os.path.join(TMP, "direct.hwrl"))
    with open(out, "rb") as f1, open(direct, "rb") as f2:
        assert f1.read() == f2.read(), "CLI .hwrl differs from export_hwrl()"
    if have_onnx:
        assert os.path.getsize(onnx_out) > 0
    log(f"  checkpoint save/load + export.py CLI (--ckpt/--out{'/--onnx' if have_onnx else ''}) byte-identical   ok")

    # the C++ loader must refuse what it cannot run
    ensure_harness()
    bad_layout = E.export_hwrl(net, os.path.join(TMP, "bad-layout.hwrl"), obs_layout_version=1)
    proc = run_harness(bad_layout, "--info", check=False)
    assert proc.returncode == 1 and "observation layout" in proc.stderr, proc.stderr
    t = E.load_hwrl(direct)
    t["meta.obs_dim"] = np.array([101.0], np.float32)
    t["enc1.weight"] = t["enc1.weight"][:, :101]
    E.write_hwrl(os.path.join(TMP, "bad-obs.hwrl"), t)
    proc = run_harness(os.path.join(TMP, "bad-obs.hwrl"), "--info", check=False)
    assert proc.returncode == 1 and "boss policy must match" in proc.stderr, proc.stderr
    t = E.load_hwrl(direct)
    del t["aux.action"]
    E.write_hwrl(os.path.join(TMP, "bad-missing.hwrl"), t)
    proc = run_harness(os.path.join(TMP, "bad-missing.hwrl"), "--info", check=False)
    assert proc.returncode == 1 and "aux.action" in proc.stderr, proc.stderr
    log("  C++ loader refuses: wrong obs layout version, wrong obs_dim, missing tensor                ok")


def _have_onnxruntime() -> bool:
    try:
        import onnxruntime  # noqa: F401
        return True
    except Exception:  # noqa: BLE001
        return False


def test_onnx(seed: int = 3) -> Optional[Dict[str, float]]:
    if not _have_onnxruntime():
        log("  onnxruntime not importable: ONNX check skipped")
        return None
    import onnxruntime as ort
    os.makedirs(TMP, exist_ok=True)
    gen = torch.Generator().manual_seed(seed)
    rng = np.random.default_rng(seed)
    worst: Dict[str, float] = {}
    for name, rec, enc, emb, hid in (VARIANTS[1], VARIANTS[2]):
        net = make_net(rec, enc, emb, hid, gen)
        path = E.export_onnx(net, os.path.join(TMP, f"{name}.onnx"))
        sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
        names = [i.name for i in sess.get_inputs()]
        assert names == list(E.ONNX_INPUTS), names
        for n in (1, 37):  # dynamic batch
            inp = make_inputs(n, hid, rng)
            feeds = {"obs": inp["obs"], "tokens": inp["tokens"].astype(np.int64), "mask": inp["mask"].astype(bool),
                     "h_in": inp["h_in"]}
            logits, value, h_out, aux_all = sess.run(list(E.ONNX_OUTPUTS), feeds)
            assert aux_all.shape == (n, NUM_ACTIONS, AUX) and logits.shape == (n, NUM_ACTIONS)
            with torch.no_grad():
                tl, tv, th = net.step(torch.from_numpy(feeds["obs"]), torch.from_numpy(feeds["tokens"]),
                                      torch.from_numpy(feeds["mask"]), torch.from_numpy(feeds["h_in"]))
                ta = net.aux_all(th)
            allowed = feeds["mask"]
            errs = {"logits": float(np.abs(logits - tl.numpy())[allowed].max()),
                    "value": float(np.abs(value - tv.numpy()).max()),
                    "h_out": float(np.abs(h_out - th.numpy()).max()),
                    "aux_all": float(np.abs(aux_all - ta.numpy()).max())}
            assert np.all(logits[~allowed] <= M.MASKED_LOGIT / 2), "ONNX: masked logits not masked"
            ok = max(errs.values()) <= TOL
            log(f"  ONNX {name:<18s} batch {n:<3d} logits {errs['logits']:.2e}  value {errs['value']:.2e}  "
                f"h_out {errs['h_out']:.2e}  aux_all {errs['aux_all']:.2e}  {'ok' if ok else 'FAIL'}")
            assert ok, f"ONNX {name} batch {n}: {errs}"
            for k, v in errs.items():
                worst[k] = max(worst.get(k, 0.0), v)
    return worst


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rebuild", action="store_true", help="rebuild the harness first")
    ap.add_argument("--no-cuda", action="store_true", help="skip the torch-on-GPU comparisons")
    ap.add_argument("--no-onnx", action="store_true")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args(argv)
    os.makedirs(TMP, exist_ok=True)
    t0 = time.time()
    failures = []
    ensure_harness(force=args.rebuild)
    tests = [("round trip", lambda: test_roundtrip(args.seed + 1)),
             ("parity vs C++", lambda: test_parity_harness(args.seed, use_cuda=not args.no_cuda))]
    if not args.no_onnx:
        tests.append(("ONNX vs torch", lambda: test_onnx(args.seed + 2)))
    summaries = {}
    for label, fn in tests:
        log(f"== {label}")
        try:
            summaries[label] = fn()
        except Exception as exc:  # noqa: BLE001 - report every test, then fail
            failures.append(label)
            log(f"  FAILED: {type(exc).__name__}: {exc}")
            traceback.print_exc()
    par = summaries.get("parity vs C++")
    if par:
        log("== max abs error per output (C++ vs torch, every mode and device)")
        for name, errs in par.items():
            log(f"  {name:<18s} " + "  ".join(f"{k} {v:.2e}" for k, v in errs.items()))
    log(f"== {'PASS' if not failures else 'FAIL: ' + ', '.join(failures)}  ({time.time() - t0:.1f} s, tolerance {TOL})")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
