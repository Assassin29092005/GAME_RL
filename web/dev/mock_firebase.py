#!/usr/bin/env python3
"""HellwalkerRL - a local stand-in for the Firebase REST subset in web/CONTRACT.md (stdlib only).

    python web/dev/mock_firebase.py [--port 8099] [--db web/dev/mock_db.json] [--indexes web/firebase/firestore.indexes.json]

Bases (as the contract's table):
    {mock}/identitytoolkit/v1/accounts:signUp?key=...          anonymous sign-up
    {mock}/securetoken/v1/token?key=...                        refresh token -> id token
    {mock}/firestore/v1/projects/{p}/databases/(default)/documents/...
        POST :commit (update + updateMask + updateTransforms REQUEST_TIME / increment, delete, currentDocument.exists)
        GET {doc} | GET {collection}?pageSize=&pageToken= | PATCH {doc}?updateMask.fieldPaths=... | DELETE {doc}
        POST :runQuery (from, where fieldFilter EQUAL [and AND of them], orderBy, limit)
    {mock}/__mock/health                                       {"ok": true} (for scripts)

The permission and schema checks are the ones in web/firebase/firestore.rules, written again in Python (the rules
section below follows that file function by function), so the end-to-end test (web/dev/e2e_test.py) exercises them.
A query that needs a composite index the indexes file does not declare fails with FAILED_PRECONDITION, as Firestore
does. One database for every project id (the id in the path is echoed, not used). Everything persists to --db (JSON).
Anything outside the subset answers 400 "mock: unsupported ..." so the game and the site cannot drift from the contract.
"""

import argparse
import base64
import copy
import datetime as dt
import json
import os
import re
import secrets
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TOKEN_SECONDS = 3600


# ---- errors ---------------------------------------------------------------------------------------------------------

class ApiError(Exception):
	def __init__(self, code, status, message):
		super().__init__(message)
		self.code, self.status, self.message = code, status, message

	def body(self):
		return {"error": {"code": self.code, "message": self.message, "status": self.status}}


def denied():
	return ApiError(403, "PERMISSION_DENIED", "Missing or insufficient permissions.")


def bad(message):
	return ApiError(400, "INVALID_ARGUMENT", message)


def unsupported(what):
	return ApiError(400, "INVALID_ARGUMENT", "mock: unsupported " + what + " (not in web/CONTRACT.md)")


# ---- times ----------------------------------------------------------------------------------------------------------

class Ts(str):
	"""A timestamp value (normalised RFC 3339 UTC, microseconds) - distinct from a string for the rules' `is timestamp`."""


def now_ts():
	return Ts(dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ"))


def norm_ts(s):
	if not isinstance(s, str):
		raise bad("timestampValue must be a string")
	m = re.match(r"^(\d{4}-\d{2}-\d{2})[Tt ](\d{2}:\d{2}:\d{2})(\.\d+)?([Zz]|[+-]\d{2}:\d{2})$", s)
	if not m:
		raise bad("Invalid value at timestampValue: " + s)
	frac = (m.group(3) or ".0")[1:7].ljust(6, "0")
	zone = m.group(4).upper()
	t = dt.datetime.fromisoformat(m.group(1) + "T" + m.group(2) + "." + frac + ("+00:00" if zone == "Z" else zone))
	return Ts(t.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ"))


# ---- Firestore typed values <-> Python values -----------------------------------------------------------------------

class Opaque:
	"""bytes / reference / geoPoint: kept, never matched by the rules' type checks."""

	def __init__(self, kind, raw):
		self.kind, self.raw = kind, raw

	def __eq__(self, other):
		return isinstance(other, Opaque) and (self.kind, json.dumps(self.raw, sort_keys=True)) == (other.kind, json.dumps(other.raw, sort_keys=True))


def check_typed(v, where="value"):
	"""Validates a typed JSON value (and normalises timestamps in place). Returns it."""
	if not isinstance(v, dict) or len(v) != 1:
		raise bad("Invalid " + where + ": a typed value needs exactly one of nullValue, booleanValue, integerValue, ...")
	(k, x), = v.items()
	if k == "nullValue":
		pass
	elif k == "booleanValue":
		if not isinstance(x, bool):
			raise bad("Invalid booleanValue")
	elif k == "integerValue":
		try:
			int(str(x))
		except ValueError:
			raise bad("Invalid integerValue: " + str(x))
		v[k] = str(int(str(x)))
	elif k == "doubleValue":
		if isinstance(x, bool) or not isinstance(x, (int, float, str)):
			raise bad("Invalid doubleValue")
		float(x)
	elif k == "timestampValue":
		v[k] = str(norm_ts(x))
	elif k == "stringValue":
		if not isinstance(x, str):
			raise bad("Invalid stringValue")
	elif k == "mapValue":
		if not isinstance(x, dict):
			raise bad("Invalid mapValue")
		for fk, fv in (x.get("fields") or {}).items():
			check_typed(fv, fk)
	elif k == "arrayValue":
		if not isinstance(x, dict):
			raise bad("Invalid arrayValue")
		for e in x.get("values") or []:
			if isinstance(e, dict) and "arrayValue" in e:
				raise bad("Cannot have an array of arrays")
			check_typed(e, where)
	elif k in ("bytesValue", "referenceValue", "geoPointValue"):
		pass
	else:
		raise bad("Invalid " + where + ": unknown value type " + k)
	return v


def decode(v):
	(k, x), = v.items()
	if k == "nullValue":
		return None
	if k == "booleanValue":
		return bool(x)
	if k == "integerValue":
		return int(x)
	if k == "doubleValue":
		return float(x)
	if k == "timestampValue":
		return Ts(x)
	if k == "stringValue":
		return x
	if k == "mapValue":
		return {fk: decode(fv) for fk, fv in (x.get("fields") or {}).items()}
	if k == "arrayValue":
		return [decode(e) for e in x.get("values") or []]
	return Opaque(k, x)


def decode_fields(fields):
	return None if fields is None else {k: decode(v) for k, v in fields.items()}


# Rules-style type tests (bool is not a number; a timestamp is not a string).
def is_int(v):
	return isinstance(v, int) and not isinstance(v, bool)


def is_num(v):
	return isinstance(v, (int, float)) and not isinstance(v, bool)


def is_str(v):
	return isinstance(v, str) and not isinstance(v, Ts)


def is_ts(v):
	return isinstance(v, Ts)


def is_map(v):
	return isinstance(v, dict)


def is_list(v):
	return isinstance(v, list)


def same(a, b):
	"""Typed equality, as the rules compare values (1 == 1.0, but true != 1 and a string != a timestamp)."""
	if is_num(a) and is_num(b):
		return a == b
	if isinstance(a, bool) or isinstance(b, bool):
		return isinstance(a, bool) and isinstance(b, bool) and a == b
	if is_ts(a) or is_ts(b):
		return is_ts(a) and is_ts(b) and str(a) == str(b)
	if is_str(a) or is_str(b):
		return is_str(a) and is_str(b) and a == b
	if a is None or b is None:
		return a is None and b is None
	if is_map(a) and is_map(b):
		return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
	if is_list(a) and is_list(b):
		return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
	return a == b


def num_is(v, n):
	return is_num(v) and v == n


# ---- field paths on typed documents ---------------------------------------------------------------------------------

def parse_path(fp):
	parts, i = [], 0
	while i < len(fp):
		if fp[i] == "`":
			j = i + 1
			seg = ""
			while j < len(fp) and fp[j] != "`":
				if fp[j] == "\\" and j + 1 < len(fp):
					j += 1
				seg += fp[j]
				j += 1
			if j >= len(fp):
				raise bad("Invalid field path: " + fp)
			parts.append(seg)
			i = j + 1
		else:
			j = fp.find(".", i)
			j = len(fp) if j < 0 else j
			seg = fp[i:j]
			if not re.match(r"^[A-Za-z_][A-Za-z_0-9]*$", seg):
				raise bad("Invalid field path: " + fp)
			parts.append(seg)
			i = j
		if i < len(fp):
			if fp[i] != ".":
				raise bad("Invalid field path: " + fp)
			i += 1
			if i >= len(fp):
				raise bad("Invalid field path: " + fp)
	if not parts:
		raise bad("Empty field path")
	return parts


def typed_get(fields, parts):
	cur = fields
	for n, p in enumerate(parts):
		if cur is None or p not in cur:
			return None
		v = cur[p]
		if n == len(parts) - 1:
			return v
		if "mapValue" not in v:
			return None
		cur = v["mapValue"].get("fields") or {}
	return None


def typed_set(fields, parts, value):
	cur = fields
	for p in parts[:-1]:
		v = cur.get(p)
		if not v or "mapValue" not in v:
			v = {"mapValue": {"fields": {}}}
			cur[p] = v
		v["mapValue"].setdefault("fields", {})
		cur = v["mapValue"]["fields"]
	cur[parts[-1]] = value


def typed_delete(fields, parts):
	cur = fields
	for p in parts[:-1]:
		v = cur.get(p)
		if not v or "mapValue" not in v:
			return
		cur = v["mapValue"].get("fields") or {}
	cur.pop(parts[-1], None)


def apply_transform(fields, t, now):
	if not isinstance(t, dict) or "fieldPath" not in t:
		raise bad("A field transform needs a fieldPath")
	parts = parse_path(t["fieldPath"])
	kinds = [k for k in t if k != "fieldPath"]
	if len(kinds) != 1:
		raise bad("A field transform needs exactly one transform type")
	kind = kinds[0]
	if kind == "setToServerValue":
		if t[kind] != "REQUEST_TIME":
			raise bad("setToServerValue must be REQUEST_TIME")
		result = {"timestampValue": str(now)}
	elif kind == "increment":
		inc = check_typed(copy.deepcopy(t[kind]), "increment")
		if "integerValue" not in inc and "doubleValue" not in inc:
			raise bad("increment needs an integerValue or a doubleValue")
		cur = typed_get(fields, parts)
		if cur is not None and ("integerValue" in cur or "doubleValue" in cur):
			if "integerValue" in cur and "integerValue" in inc:
				result = {"integerValue": str(int(cur["integerValue"]) + int(inc["integerValue"]))}
			else:
				a = float(cur.get("integerValue", cur.get("doubleValue")))
				b = float(inc.get("integerValue", inc.get("doubleValue")))
				result = {"doubleValue": a + b}
		else:
			result = inc
	else:
		raise unsupported("field transform " + kind)
	typed_set(fields, parts, result)
	return copy.deepcopy(result)


# ---- the rules (web/firebase/firestore.rules, function by function) -------------------------------------------------

ANSWER_KEYS = ("parry", "block", "stepL", "stepR", "stepB", "stepF", "attack", "none")
CLASS_KEYS = ("fast", "heavy", "feint", "killer")
DIFFICULTIES = ("Easy", "Normal", "Hard", "Hellwalker", "Adaptive")
TOTAL_KEYS = ("fights", "wins", "losses", "timeouts", "seconds", "dmgDealt", "dmgTaken", "playerSwings", "playerHits",
	"keeperSwings", "keeperHits", "keeperBlocked", "keeperParried", "keeperWhiffed", "parryAttempts", "dodges",
	"readsLanded", "predictions", "predictionsCorrect", "confident", "confidentCorrect")
PLAYER_KEYS = ("v", "nickname", "createdAt", "lastSeen", "gameVersion", "difficulty", "totals", "keepers", "answers",
	"expected", "survey", "resets", "resetAt")
FIGHT_KEYS_V1 = ("v", "player", "at", "clientTime", "session", "fightInSession", "gameVersion", "mode", "playMode", "brain",
	"keeper", "keeperName", "difficulty", "skill", "adaptive", "result", "seconds", "playerHealth", "keeperHealth",
	"dmgDealt", "dmgTaken", "playerSwings", "playerHits", "keeperSwings", "keeperHits", "keeperBlocked", "keeperParried",
	"keeperWhiffed", "parryAttempts", "dodges", "guardBreaks", "keeperExposed", "readsLanded", "predictions",
	"predictionsCorrect", "confident", "confidentCorrect", "expected", "answers")
FIGHT_KEYS_V2 = ("v", "player", "at", "clientTime", "session", "fightInSession", "gameVersion", "mode", "playMode", "brain",
	"keeper", "keeperName", "difficulty", "skill", "adaptive", "result", "seconds", "playerHealth", "keeperHealth",
	"dmgDealt", "dmgTaken", "playerSwings", "playerHits", "keeperSwings", "keeperHits", "keeperBlocked", "keeperParried",
	"keeperWhiffed", "parryAttempts", "dodges", "guardBreaks", "keeperExposed", "readsLanded", "predictions",
	"predictionsCorrect", "confident", "confidentCorrect", "expected", "answers", "assist", "slowmoScale",
	"keeperDamageScale", "parryWindowFrames", "insight")
ASSISTS = ("off", "ring", "ring+slowmo")
SURVEY_KEYS = ("feltRead", "fair", "difficulty", "fun", "playAgain", "noticedAdapting", "at")
HEX32 = re.compile(r"^[0-9a-f]{32}$")


def has_only(m, keys):
	return set(m.keys()) <= set(keys)


def has_all(m, keys):
	return set(keys) <= set(m.keys())


def count(v, hi):
	return is_int(v) and 0 <= v <= hi


def amount(v, hi):
	return is_num(v) and 0 <= v <= hi


def frac(v):
	return is_num(v) and 0 <= v <= 1


def text(v, lo, hi):
	return is_str(v) and lo <= len(v) <= hi


def nickname(v):
	return is_str(v) and 1 <= len(v) <= 24 and len(v.strip()) >= 1 and re.search(r"[\x00-\x1f\x7f]", v) is None


def difficulty(v):
	return is_str(v) and v in DIFFICULTIES


def answer_counts(m, hi):
	return is_map(m) and has_only(m, ANSWER_KEYS) and all(count(m.get(k, 0), hi) for k in ANSWER_KEYS)


def answers(a, hi):
	return is_map(a) and has_only(a, CLASS_KEYS) and all(answer_counts(a.get(c, {}), hi) for c in CLASS_KEYS)


def expected_row(e):
	return (is_map(e) and set(e.keys()) == {"attack", "answer", "p"}
		and text(e["attack"], 1, 32) and text(e["answer"], 1, 32) and frac(e["p"]))


def expected_list(x):
	return is_list(x) and len(x) <= 8 and all(expected_row(e) for e in x)


def totals(t):
	n = 100000000
	if not (is_map(t) and has_only(t, TOTAL_KEYS)):
		return False
	g = lambda k: t.get(k, 0)
	ints = [k for k in TOTAL_KEYS if k not in ("seconds", "dmgDealt", "dmgTaken")]
	return (all(count(g(k), n) for k in ints)
		and amount(g("seconds"), 10000000000) and amount(g("dmgDealt"), 1000000000000) and amount(g("dmgTaken"), 1000000000000)
		and g("wins") <= g("fights") and g("predictionsCorrect") <= g("predictions") and g("confidentCorrect") <= g("confident"))


def keeper_record(k):
	return (is_map(k) and has_only(k, ("fights", "wins")) and count(k.get("fights", 0), 100000000)
		and count(k.get("wins", 0), 100000000) and k.get("wins", 0) <= k.get("fights", 0))


def keepers(k):
	return is_map(k) and has_only(k, ("warden", "sage", "returned")) and all(keeper_record(k.get(x, {})) for x in ("warden", "sage", "returned"))


def likert(v):
	return is_int(v) and 1 <= v <= 5


def survey(s, now):
	return (is_map(s) and has_only(s, SURVEY_KEYS) and has_all(s, SURVEY_KEYS)
		and all(likert(s[k]) for k in ("feltRead", "fair", "difficulty", "fun", "playAgain"))
		and isinstance(s["noticedAdapting"], bool) and is_ts(s["at"]) and s["at"] == now)


def all_zero(m):
	return all(is_num(v) and v == 0 for v in m.values())


def player_create_ok(d, now):
	return (has_only(d, PLAYER_KEYS) and has_all(d, ("v", "nickname", "createdAt"))
		and num_is(d["v"], 1) and nickname(d["nickname"]) and is_ts(d["createdAt"]) and d["createdAt"] == now
		and ("lastSeen" not in d or (is_ts(d["lastSeen"]) and d["lastSeen"] == now))
		and ("gameVersion" not in d or text(d["gameVersion"], 0, 32))
		and ("difficulty" not in d or difficulty(d["difficulty"]))
		and ("totals" not in d or (totals(d["totals"]) and all_zero(d["totals"])))
		and ("keepers" not in d or keepers(d["keepers"]))
		and ("answers" not in d or answers(d["answers"], 0))
		and ("expected" not in d or expected_list(d["expected"]))
		and "survey" not in d
		and ("resets" not in d or num_is(d["resets"], 0))
		and ("resetAt" not in d or d["resetAt"] is None))


def affected_keys(d, old):
	return {k for k in set(d) | set(old) if k not in d or k not in old or not same(d[k], old[k])}


def player_update_ok(d, old, now):
	changed = affected_keys(d, old)
	is_reset = "resets" in changed
	old_fights = (old.get("totals") or {}).get("fights", 0) if is_map(old.get("totals", {})) else None

	def grew(k, hi):
		was = (old.get("totals") or {}).get(k, 0) if is_map(old.get("totals", {})) else 0
		delta = d["totals"].get(k, 0) - was
		return is_num(delta) and 0 <= delta <= hi

	def totals_ok():
		if not totals(d["totals"]):
			return False
		if is_reset:
			return True
		f = d["totals"].get("fights", 0)
		c = 1000000
		# One fight's worth per write (firestore.rules totalsGrowthOk).
		return (is_num(old_fights) and old_fights <= f <= old_fights + 1
			and grew("wins", 1) and grew("losses", 1) and grew("timeouts", 1)
			and grew("seconds", 86400) and grew("dmgDealt", 10000000) and grew("dmgTaken", 10000000)
			and all(grew(k, c) for k in ("playerSwings", "playerHits", "keeperSwings", "keeperHits", "keeperBlocked",
				"keeperParried", "keeperWhiffed", "parryAttempts", "dodges", "readsLanded", "predictions",
				"predictionsCorrect", "confident", "confidentCorrect")))

	def keepers_ok():
		if not keepers(d["keepers"]):
			return False
		if is_reset:
			return True
		was_all = old.get("keepers") if is_map(old.get("keepers", {})) else {}
		for k in ("warden", "sage", "returned"):
			now_k, was_k = d["keepers"].get(k, {}), (was_all or {}).get(k, {})
			for f in ("fights", "wins"):
				delta = now_k.get(f, 0) - was_k.get(f, 0)
				if not (is_num(delta) and 0 <= delta <= 1):
					return False
		return True

	return (has_only(d, PLAYER_KEYS) and has_all(d, ("v", "nickname", "createdAt"))
		and "createdAt" not in changed
		and ("v" not in changed or num_is(d["v"], 1))
		and ("nickname" not in changed or nickname(d["nickname"]))
		and ("lastSeen" not in changed or (is_ts(d["lastSeen"]) and d["lastSeen"] == now))
		and ("gameVersion" not in changed or text(d["gameVersion"], 0, 32))
		and ("difficulty" not in changed or difficulty(d["difficulty"]))
		and ("totals" not in changed or totals_ok())
		and ("keepers" not in changed or keepers_ok())
		and ("answers" not in changed or answers(d["answers"], 100000000))
		and ("expected" not in changed or expected_list(d["expected"]))
		and ("survey" not in changed or survey(d["survey"], now))
		and (not is_reset or (is_num(d["resets"]) and d["resets"] == old.get("resets", 0) + 1
			and is_ts(d.get("resetAt")) and d["resetAt"] == now
			and all_zero(d.get("totals", {})) and len(d.get("expected", [])) == 0))
		and ("resetAt" not in changed or (is_reset and is_ts(d["resetAt"]) and d["resetAt"] == now)))


def assist_ok(f):
	"""The v2 fields: assist, slow motion (only with the ring), the keeper's damage scale, the parry window, insight."""
	return (is_str(f["assist"]) and f["assist"] in ASSISTS
		and is_num(f["slowmoScale"]) and 0 < f["slowmoScale"] <= 1
		and (f["assist"] == "ring+slowmo" or f["slowmoScale"] == 1)
		and is_num(f["keeperDamageScale"]) and 0 < f["keeperDamageScale"] <= 2
		and is_int(f["parryWindowFrames"]) and 1 <= f["parryWindowFrames"] <= 60
		and frac(f["insight"]))


def fight_v1(f):
	"""v1 (games 1.0.0 - 1.3.0): exactly the 39 v1 keys; accepted forever (offline-queued bodies)."""
	return has_only(f, FIGHT_KEYS_V1) and has_all(f, FIGHT_KEYS_V1) and num_is(f["v"], 1)


def fight_v2(f):
	"""v2 (games 1.4.0+): the v1 keys and five more, all required."""
	return has_only(f, FIGHT_KEYS_V2) and has_all(f, FIGHT_KEYS_V2) and num_is(f["v"], 2) and assist_ok(f)


def fight_ok(f, now):
	c = 1000000
	cnt = ("playerSwings", "playerHits", "keeperSwings", "keeperHits", "keeperBlocked", "keeperParried", "keeperWhiffed",
		"parryAttempts", "dodges", "guardBreaks", "keeperExposed", "readsLanded", "predictions", "predictionsCorrect",
		"confident", "confidentCorrect")
	return ((fight_v1(f) or fight_v2(f))
		and is_ts(f["at"]) and f["at"] == now
		and is_ts(f["clientTime"])
		and is_str(f["session"]) and HEX32.match(f["session"]) is not None
		and is_int(f["fightInSession"]) and 1 <= f["fightInSession"] <= c
		and text(f["gameVersion"], 1, 32)
		and is_str(f["mode"]) and f["mode"] in ("openworld", "arena")
		and is_str(f["playMode"]) and f["playMode"] in ("pathbreaker", "hellwalker", "66days", "arena")
		and is_str(f["brain"]) and f["brain"] in ("rl", "script")
		and is_num(f["keeper"]) and f["keeper"] in (0, 1, 2)
		and text(f["keeperName"], 0, 64)
		and difficulty(f["difficulty"])
		and frac(f["skill"])
		and isinstance(f["adaptive"], bool)
		and is_str(f["result"]) and f["result"] in ("win", "loss", "timeout", "quit")
		and amount(f["seconds"], 86400)
		and frac(f["playerHealth"]) and frac(f["keeperHealth"])
		and amount(f["dmgDealt"], 10000000) and amount(f["dmgTaken"], 10000000)
		and all(count(f[k], c) for k in cnt)
		and f["predictionsCorrect"] <= f["predictions"] and f["confidentCorrect"] <= f["confident"]
		and expected_list(f["expected"])
		and answers(f["answers"], c))


def survey_doc_ok(d, now):
	return (has_only(d, ("v", "comment", "at")) and has_all(d, ("v", "comment", "at")) and num_is(d["v"], 1)
		and text(d["comment"], 0, 500) and is_ts(d["at"]) and d["at"] == now)


def allowed(op, path, uid, before, after, now):
	"""op: get | list | create | update | delete. before / after: decoded data (None = no document)."""
	segs = path.split("/")
	try:
		if len(segs) == 1:  # a collection (list / query)
			return op == "list" and segs[0] in ("players", "fights")
		if len(segs) != 2:
			return False
		coll, doc_id = segs
		if coll == "players":
			if op in ("get", "list"):
				return True
			if op == "create":
				return uid == doc_id and player_create_ok(after, now)
			if op == "update":
				return uid == doc_id and player_update_ok(after, before, now)
			return False
		if coll == "fights":
			if op in ("get", "list"):
				return True
			if op == "create":
				return HEX32.match(doc_id) is not None and uid is not None and uid == after["player"] and fight_ok(after, now)
			if op == "update":
				return False
			if op == "delete":
				return uid is not None and uid == before["player"]  # before None -> error -> denied, as the rules
			return False
		if coll == "surveys":
			if op in ("get", "delete"):
				return uid == doc_id and uid is not None
			if op in ("create", "update"):
				return uid == doc_id and uid is not None and survey_doc_ok(after, now)
			return False
		return False
	except (KeyError, TypeError, AttributeError, ValueError):
		return False  # a rules evaluation error denies


# ---- the store ------------------------------------------------------------------------------------------------------

class Store:
	def __init__(self, path):
		self.path = path
		self.lock = threading.RLock()
		self.data = {"version": 1, "users": {}, "refreshTokens": {}, "idTokens": {}, "docs": {}}
		if path and os.path.exists(path):
			with open(path, "r", encoding="utf-8") as f:
				loaded = json.load(f)
			self.data.update(loaded)

	def save(self):
		if not self.path:
			return
		now = time.time()
		self.data["idTokens"] = {k: v for k, v in self.data["idTokens"].items() if v["exp"] > now}
		tmp = self.path + ".tmp"
		with open(tmp, "w", encoding="utf-8") as f:
			json.dump(self.data, f, indent=1, sort_keys=True)
		os.replace(tmp, self.path)

	def new_user(self):
		uid = "".join(secrets.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") for _ in range(28))
		refresh = "AMf-mock-" + secrets.token_urlsafe(48)
		self.data["users"][uid] = {"createdAt": str(now_ts()), "refreshToken": refresh}
		self.data["refreshTokens"][refresh] = uid
		return uid, refresh

	def new_id_token(self, uid):
		tok = "mock-id-" + secrets.token_urlsafe(32)
		self.data["idTokens"][tok] = {"uid": uid, "exp": time.time() + TOKEN_SECONDS}
		return tok

	def uid_for(self, id_token):
		e = self.data["idTokens"].get(id_token)
		if not e or e["exp"] < time.time():
			return None
		return e["uid"]


# ---- the server -----------------------------------------------------------------------------------------------------

FS_PREFIX = re.compile(r"^/firestore/v1/projects/([^/]+)/databases/\(default\)/documents(.*)$")
NAME_PREFIX = re.compile(r"^projects/([^/]+)/databases/\(default\)/documents/(.+)$")


def type_rank(v):
	if v is None:
		return (0, 0)
	if isinstance(v, bool):
		return (1, int(v))
	if is_num(v):
		return (2, v)
	if is_ts(v):
		return (3, str(v))
	if is_str(v):
		return (4, v)
	if is_list(v):
		return (8, 0)
	if is_map(v):
		return (9, 0)
	return (7, 0)


class Handler(BaseHTTPRequestHandler):
	server_version = "HellwalkerMockFirebase/1"
	store = None
	indexes = []
	quiet = False

	# ---- plumbing ----
	def log_message(self, fmt, *args):
		if not self.quiet:
			super().log_message(fmt, *args)

	def cors(self):
		self.send_header("Access-Control-Allow-Origin", self.headers.get("Origin") or "*")
		self.send_header("Vary", "Origin")
		self.send_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS")
		self.send_header("Access-Control-Allow-Headers", "Authorization, Content-Type, X-Goog-Api-Client, X-Client-Version")
		self.send_header("Access-Control-Max-Age", "600")

	def reply(self, code, obj):
		raw = json.dumps(obj).encode("utf-8")
		self.send_response(code)
		self.cors()
		self.send_header("Content-Type", "application/json; charset=UTF-8")
		self.send_header("Content-Length", str(len(raw)))
		self.send_header("Cache-Control", "no-store")
		self.end_headers()
		self.wfile.write(raw)

	def do_OPTIONS(self):
		self.send_response(204)
		self.cors()
		self.send_header("Content-Length", "0")
		self.end_headers()

	def do_GET(self):
		self.route("GET")

	def do_POST(self):
		self.route("POST")

	def do_PATCH(self):
		self.route("PATCH")

	def do_DELETE(self):
		self.route("DELETE")

	def raw_body(self):
		n = int(self.headers.get("Content-Length") or 0)
		return self.rfile.read(n) if n > 0 else b""

	def json_body(self):
		raw = self.raw_body()
		if not raw:
			return {}
		try:
			body = json.loads(raw.decode("utf-8"))
		except ValueError:
			raise bad("Invalid JSON payload received.")
		if not isinstance(body, dict):
			raise bad("Invalid JSON payload received.")
		return body

	def auth_uid(self):
		h = self.headers.get("Authorization")
		if not h:
			return None
		m = re.match(r"^Bearer\s+(\S+)$", h)
		uid = self.store.uid_for(m.group(1)) if m else None
		if uid is None:
			raise ApiError(401, "UNAUTHENTICATED", "Request had invalid authentication credentials. Expected OAuth 2 access "
				"token, login cookie or other valid authentication credential.")
		return uid

	def route(self, method):
		url = urllib.parse.urlsplit(self.path)
		query = urllib.parse.parse_qs(url.query, keep_blank_values=True)
		path = urllib.parse.unquote(url.path)
		is_query = False
		try:
			with self.store.lock:
				if path == "/__mock/health" and method == "GET":
					return self.reply(200, {"ok": True, "docs": len(self.store.data["docs"])})
				if path == "/identitytoolkit/v1/accounts:signUp" and method == "POST":
					return self.sign_up(query)
				if path == "/securetoken/v1/token" and method == "POST":
					return self.refresh(query)
				m = FS_PREFIX.match(path)
				if not m:
					raise ApiError(404, "NOT_FOUND", "mock: no such endpoint " + method + " " + path)
				project, rest = m.group(1), m.group(2)
				if rest == ":commit" and method == "POST":
					return self.commit(project)
				if rest.endswith(":runQuery") and method == "POST":
					is_query = True
					return self.run_query(project, rest[: -len(":runQuery")].lstrip("/"))
				doc_path = rest.lstrip("/")
				if not doc_path or not rest.startswith("/") or any(s == "" for s in doc_path.split("/")):
					raise unsupported(method + " " + rest)
				even = len(doc_path.split("/")) % 2 == 0
				if method == "GET":
					return self.get_doc(project, doc_path) if even else self.list_docs(project, doc_path, query)
				if method == "PATCH" and even:
					return self.patch_doc(project, doc_path, query)
				if method == "DELETE" and even:
					return self.delete_doc(project, doc_path, query)
				raise unsupported(method + " " + rest)
		except ApiError as e:
			return self.reply(e.code, [e.body()] if is_query else e.body())
		except Exception as e:  # a mock bug: say so loudly
			return self.reply(500, {"error": {"code": 500, "message": "mock crashed: %r" % e, "status": "INTERNAL"}})

	# ---- auth ----
	def require_key(self, query):
		if not (query.get("key") or [""])[0]:
			raise ApiError(400, "INVALID_ARGUMENT", "API key not valid. Please pass a valid API key.")

	def sign_up(self, query):
		self.require_key(query)
		body = self.json_body()
		if set(body.keys()) - {"returnSecureToken"}:
			raise unsupported("signUp fields " + ",".join(sorted(set(body.keys()) - {"returnSecureToken"})))
		uid, refresh = self.store.new_user()
		tok = self.store.new_id_token(uid)
		self.store.save()
		self.reply(200, {"kind": "identitytoolkit#SignupNewUserResponse", "idToken": tok, "refreshToken": refresh,
			"expiresIn": str(TOKEN_SECONDS), "localId": uid})

	def refresh(self, query):
		self.require_key(query)
		raw = self.raw_body().decode("utf-8")
		ctype = (self.headers.get("Content-Type") or "").lower()
		if "json" in ctype:
			try:
				form = json.loads(raw or "{}")
			except ValueError:
				raise bad("Invalid JSON payload received.")
		else:
			form = {k: v[0] for k, v in urllib.parse.parse_qs(raw, keep_blank_values=True).items()}
		if form.get("grant_type") != "refresh_token":
			raise ApiError(400, "INVALID_ARGUMENT", "INVALID_GRANT_TYPE")
		rt = form.get("refresh_token") or ""
		if not rt:
			raise ApiError(400, "INVALID_ARGUMENT", "MISSING_REFRESH_TOKEN")
		uid = self.store.data["refreshTokens"].get(rt)
		if uid is None:
			raise ApiError(400, "INVALID_ARGUMENT", "INVALID_REFRESH_TOKEN")
		tok = self.store.new_id_token(uid)
		self.store.save()
		self.reply(200, {"access_token": tok, "expires_in": str(TOKEN_SECONDS), "token_type": "Bearer",
			"refresh_token": rt, "id_token": tok, "user_id": uid, "project_id": "mock"})

	# ---- documents ----
	def doc_json(self, project, path, doc):
		out = {"name": "projects/%s/databases/(default)/documents/%s" % (project, path), "createTime": doc["createTime"],
			"updateTime": doc["updateTime"]}
		if doc["fields"]:
			out["fields"] = doc["fields"]
		return out

	def name_to_path(self, name):
		m = NAME_PREFIX.match(name or "")
		if not m:
			raise bad("Invalid document name: %r" % name)
		path = m.group(2)
		segs = path.split("/")
		if len(segs) % 2 != 0 or any(s == "" for s in segs):
			raise bad("Document name %r is not a document" % name)
		return path

	def check_precondition(self, pre, path, before, project):
		if not pre:
			return
		if set(pre.keys()) != {"exists"}:
			raise unsupported("precondition " + ",".join(pre.keys()))
		if pre["exists"] is False and before is not None:
			raise ApiError(409, "ALREADY_EXISTS", "Document already exists: projects/%s/databases/(default)/documents/%s" % (project, path))
		if pre["exists"] is True and before is None:
			raise ApiError(404, "NOT_FOUND", "No document to update: projects/%s/databases/(default)/documents/%s" % (project, path))

	def stage_update(self, project, path, before, fields_in, mask, transforms, uid, now):
		"""Applies one update (+ transforms) to `before` (a stored doc or None); checks the rules; returns (doc, results)."""
		if not isinstance(fields_in, dict):
			raise bad("Invalid fields")
		incoming = copy.deepcopy(fields_in)
		for k, v in incoming.items():
			check_typed(v, k)
		if mask is None:
			fields = incoming
		else:
			fields = copy.deepcopy(before["fields"]) if before else {}
			for fp in mask:
				parts = parse_path(fp)
				v = typed_get(incoming, parts)
				if v is None:
					typed_delete(fields, parts)
				else:
					typed_set(fields, parts, copy.deepcopy(v))
		results = [apply_transform(fields, t, now) for t in transforms or []]
		op = "create" if before is None else "update"
		if not allowed(op, path, uid, decode_fields(before["fields"]) if before else None, decode_fields(fields), now):
			raise denied()
		doc = {"fields": fields, "createTime": before["createTime"] if before else str(now), "updateTime": str(now)}
		return doc, results

	def commit(self, project):
		body = self.json_body()
		if set(body.keys()) - {"writes"}:
			raise unsupported("commit fields " + ",".join(sorted(set(body.keys()) - {"writes"})))
		writes = body.get("writes") or []
		if not isinstance(writes, list):
			raise bad("writes must be a list")
		if len(writes) > 500:
			raise bad("A commit holds at most 500 writes")
		uid = self.auth_uid()
		now = now_ts()
		docs = self.store.data["docs"]
		staged, results, seen = {}, [], set()
		for w in writes:
			if not isinstance(w, dict):
				raise bad("Invalid write")
			extra = set(w.keys()) - {"update", "delete", "updateMask", "updateTransforms", "currentDocument"}
			if extra:
				raise unsupported("write fields " + ",".join(sorted(extra)))
			if ("update" in w) == ("delete" in w):
				raise bad("A write needs exactly one of update or delete")
			path = self.name_to_path(w["update"].get("name") if "update" in w else w["delete"])
			if path in seen:
				raise bad("A commit may write each document once: " + path)
			seen.add(path)
			before = docs.get(path)
			self.check_precondition(w.get("currentDocument"), path, before, project)
			if "update" in w:
				mask = None
				if "updateMask" in w:
					mask = (w["updateMask"] or {}).get("fieldPaths") or []
				doc, res = self.stage_update(project, path, before, w["update"].get("fields") or {}, mask,
					w.get("updateTransforms"), uid, now)
				staged[path] = doc
				results.append({"updateTime": str(now), "transformResults": res} if res else {"updateTime": str(now)})
			else:
				if w.get("updateMask") or w.get("updateTransforms"):
					raise bad("A delete takes no mask or transforms")
				if not allowed("delete", path, uid, decode_fields(before["fields"]) if before else None, None, now):
					raise denied()
				staged[path] = None
				results.append({"updateTime": str(now)})
		for path, doc in staged.items():  # all or nothing
			if doc is None:
				docs.pop(path, None)
			else:
				docs[path] = doc
		self.store.save()
		self.reply(200, {"writeResults": results, "commitTime": str(now)})

	def get_doc(self, project, path):
		uid = self.auth_uid()
		doc = self.store.data["docs"].get(path)
		if not allowed("get", path, uid, decode_fields(doc["fields"]) if doc else None, None, now_ts()):
			raise denied()
		if doc is None:
			raise ApiError(404, "NOT_FOUND", 'Document "projects/%s/databases/(default)/documents/%s" not found.' % (project, path))
		self.reply(200, self.doc_json(project, path, doc))

	def patch_doc(self, project, path, query):
		extra = set(query.keys()) - {"updateMask.fieldPaths", "currentDocument.exists", "key"}
		if extra:
			raise unsupported("PATCH parameters " + ",".join(sorted(extra)))
		body = self.json_body()
		if set(body.keys()) - {"fields", "name"}:
			raise unsupported("PATCH body fields " + ",".join(sorted(set(body.keys()) - {"fields", "name"})))
		uid = self.auth_uid()
		now = now_ts()
		docs = self.store.data["docs"]
		before = docs.get(path)
		if "currentDocument.exists" in query:
			self.check_precondition({"exists": query["currentDocument.exists"][0] == "true"}, path, before, project)
		mask = query.get("updateMask.fieldPaths")
		doc, _ = self.stage_update(project, path, before, body.get("fields") or {}, mask, None, uid, now)
		docs[path] = doc
		self.store.save()
		self.reply(200, self.doc_json(project, path, doc))

	def delete_doc(self, project, path, query):
		extra = set(query.keys()) - {"currentDocument.exists", "key"}
		if extra:
			raise unsupported("DELETE parameters " + ",".join(sorted(extra)))
		uid = self.auth_uid()
		docs = self.store.data["docs"]
		before = docs.get(path)
		if "currentDocument.exists" in query:
			self.check_precondition({"exists": query["currentDocument.exists"][0] == "true"}, path, before, project)
		if not allowed("delete", path, uid, decode_fields(before["fields"]) if before else None, None, now_ts()):
			raise denied()
		docs.pop(path, None)
		self.store.save()
		self.reply(200, {})

	def collection_docs(self, coll_path):
		depth = len(coll_path.split("/"))
		prefix = coll_path + "/"
		return sorted((p, d) for p, d in self.store.data["docs"].items()
			if p.startswith(prefix) and len(p.split("/")) == depth + 1)

	def list_docs(self, project, coll_path, query):
		extra = set(query.keys()) - {"pageSize", "pageToken", "key"}
		if extra:
			raise unsupported("list parameters " + ",".join(sorted(extra)))
		uid = self.auth_uid()
		if not allowed("list", coll_path, uid, None, None, now_ts()):
			raise denied()
		try:
			size = int((query.get("pageSize") or ["20"])[0] or 20)
		except ValueError:
			raise bad("Invalid pageSize")
		size = max(1, min(size, 300))
		token = (query.get("pageToken") or [""])[0]
		after = ""
		if token:
			try:
				after = base64.urlsafe_b64decode(token.encode("ascii")).decode("utf-8")
			except Exception:
				raise bad("Invalid pageToken")
		items = [(p, d) for p, d in self.collection_docs(coll_path) if p > after]
		page = items[:size]
		out = {}
		if page:
			out["documents"] = [self.doc_json(project, p, d) for p, d in page]
		if len(items) > size:
			out["nextPageToken"] = base64.urlsafe_b64encode(page[-1][0].encode("utf-8")).decode("ascii")
		self.reply(200, out)

	def run_query(self, project, parent):
		body = self.json_body()
		if set(body.keys()) - {"structuredQuery"}:
			raise unsupported("runQuery fields " + ",".join(sorted(set(body.keys()) - {"structuredQuery"})))
		q = body.get("structuredQuery")
		if not isinstance(q, dict):
			raise bad("runQuery needs a structuredQuery")
		extra = set(q.keys()) - {"from", "where", "orderBy", "limit"}
		if extra:
			raise unsupported("structuredQuery fields " + ",".join(sorted(extra)))
		froms = q.get("from") or []
		if len(froms) != 1 or not isinstance(froms[0], dict) or set(froms[0].keys()) != {"collectionId"}:
			raise unsupported("from (one collectionId, no allDescendants)")
		coll = froms[0]["collectionId"]
		coll_path = (parent + "/" + coll) if parent else coll
		uid = self.auth_uid()
		if not allowed("list", coll_path, uid, None, None, now_ts()):
			raise denied()
		# filters: a fieldFilter EQUAL, or an AND of them
		filters = []
		where = q.get("where")
		if where is not None:
			if set(where.keys()) == {"fieldFilter"}:
				flist = [where]
			elif set(where.keys()) == {"compositeFilter"} and where["compositeFilter"].get("op") == "AND":
				flist = where["compositeFilter"].get("filters") or []
			else:
				raise unsupported("where clause (only fieldFilter EQUAL)")
			for f in flist:
				ff = f.get("fieldFilter") if isinstance(f, dict) else None
				if not ff or ff.get("op") != "EQUAL":
					raise unsupported("filter op %r (only EQUAL)" % (ff or {}).get("op"))
				filters.append((parse_path(ff["field"]["fieldPath"]), ff["field"]["fieldPath"], decode(check_typed(copy.deepcopy(ff["value"])))))
		orders = []
		for o in q.get("orderBy") or []:
			d = o.get("direction", "ASCENDING")
			if d not in ("ASCENDING", "DESCENDING"):
				raise bad("Invalid direction " + str(d))
			orders.append((parse_path(o["field"]["fieldPath"]), o["field"]["fieldPath"], d))
		limit = q.get("limit")
		if isinstance(limit, dict):
			limit = limit.get("value")
		if limit is not None:
			try:
				limit = int(limit)
			except (TypeError, ValueError):
				raise bad("Invalid limit")
			if limit < 0:
				raise bad("Invalid limit")
		self.require_index(coll, [f[1] for f in filters], [(o[1], o[2]) for o in orders])
		rows = []
		for p, d in self.collection_docs(coll_path):
			data = decode_fields(d["fields"])
			ok = True
			for parts, _, value in filters:
				v = typed_get(d["fields"], parts)
				if v is None or not same(decode(v), value):
					ok = False
					break
			if not ok:
				continue
			keys = []
			for parts, _, direction in orders:
				v = typed_get(d["fields"], parts)
				if v is None:
					ok = False  # Firestore leaves out documents without the order field
					break
				keys.append((type_rank(decode(v)), direction))
			if ok:
				rows.append((p, d, keys, data))
		# sort by each order key (stable, last key first), the name as the final tie-break
		last_dir = orders[-1][2] if orders else "ASCENDING"
		rows.sort(key=lambda r: r[0], reverse=(last_dir == "DESCENDING"))
		for i in range(len(orders) - 1, -1, -1):
			rows.sort(key=lambda r: r[2][i][0], reverse=(orders[i][2] == "DESCENDING"))
		if limit is not None:
			rows = rows[:limit]
		now = str(now_ts())
		out = [{"document": self.doc_json(project, p, d), "readTime": now} for p, d, _, _ in rows]
		self.reply(200, out if out else [{"readTime": now}])

	def require_index(self, coll, eq_fields, orders):
		"""Firestore serves equality-only queries and single-field orders by itself; an equality plus an order on another
		field, or several orders, need a composite index (web/firebase/firestore.indexes.json)."""
		eq = list(dict.fromkeys(eq_fields))
		order = [(f, d) for f, d in orders if f not in eq]
		if not order or (not eq and len(order) == 1):
			return
		for ix in self.indexes:
			if ix.get("collectionGroup") != coll or ix.get("queryScope", "COLLECTION") != "COLLECTION":
				continue
			fields = [(f.get("fieldPath"), f.get("order")) for f in ix.get("fields", [])]
			head, tail = fields[: len(eq)], fields[len(eq):]
			if sorted(f for f, _ in head) == sorted(eq) and tail == order:
				return
		raise ApiError(400, "FAILED_PRECONDITION", "The query requires an index (mock: add it to "
			"web/firebase/firestore.indexes.json): collection %s, equality on %s, order by %s" % (coll, eq, order))


def main():
	ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
	ap.add_argument("--host", default="127.0.0.1")
	ap.add_argument("--port", type=int, default=8099)
	ap.add_argument("--db", default=os.path.join(HERE, "mock_db.json"), help="JSON file the data persists to")
	ap.add_argument("--indexes", default=os.path.join(HERE, "..", "firebase", "firestore.indexes.json"))
	ap.add_argument("--quiet", action="store_true", help="no request log")
	args = ap.parse_args()
	Handler.store = Store(args.db)
	with open(args.indexes, "r", encoding="utf-8") as f:
		Handler.indexes = json.load(f).get("indexes", [])
	Handler.quiet = args.quiet
	srv = ThreadingHTTPServer((args.host, args.port), Handler)
	print("mock Firebase on http://%s:%d  (db %s, %d documents)" % (args.host, args.port, os.path.abspath(args.db),
		len(Handler.store.data["docs"])), flush=True)
	try:
		srv.serve_forever()
	except KeyboardInterrupt:
		pass


if __name__ == "__main__":
	main()
