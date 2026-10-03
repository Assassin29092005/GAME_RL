#!/usr/bin/env python3
"""HellwalkerRL - end-to-end test of the telemetry contract (web/CONTRACT.md) against the mock (stdlib only).

    python web/dev/e2e_test.py                       # against a running mock on 127.0.0.1:8099 (starts its own if none)
    python web/dev/e2e_test.py --spawn               # always a private mock on a free port with a throwaway database
    python web/dev/e2e_test.py --seed 0              # no extra sample players

It plays both sides exactly as the contract writes them:
  the game  - anonymous signUp, create players/{uid} once, one documents:commit per fight (create the fight with
              currentDocument.exists=false + update the player with updateMask and REQUEST_TIME / increment transforms),
              an idempotent retry that must answer 409 and change nothing;
  the site  - the profile (players/{uid} + runQuery fights where player == uid order by at desc), the leaderboard
              (runQuery players order by totals.* desc) and the collection list (pageSize / pageToken), linking a browser
              with the refresh token, nickname, survey, and the reset (fights gone, totals zero, nickname and survey kept);
  attackers - another uid deleting / overwriting, malformed fights, negative and inflated counts, private reads.
Then it leaves --seed sample players with fights in the database (for looking at the site in mock mode) and prints a
link that opens one of them as "my stats". Exit code 0 = every check passed.
"""

import argparse
import json
import os
import random
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
API_KEY = "mock-api-key"

ATTACKS = ["fastSlash", "sweepLeft", "sweepRight", "heavyCleave", "delayedHeavy", "feint", "grab", "killer"]
CLASSES = ["fast", "heavy", "feint", "killer"]
ANSWERS = ["parry", "block", "stepL", "stepR", "stepB", "stepF", "attack", "none"]
KEEPERS = [("warden", "The Ninefold Warden"), ("sage", "The Monkey Sage"), ("returned", "The Warden, Returned")]
INT_TOTALS = ["playerSwings", "playerHits", "keeperSwings", "keeperHits", "keeperBlocked", "keeperParried",
	"keeperWhiffed", "parryAttempts", "dodges", "readsLanded", "predictions", "predictionsCorrect", "confident",
	"confidentCorrect"]
ADJ = ["Ashen", "Hollow", "Crimson", "Silent", "Ember", "Grave", "Pale", "Iron", "Wandering", "Broken", "Gilded", "Restless"]
NOUN = ["Wanderer", "Pilgrim", "Blade", "Lantern", "Moth", "Shade", "Bellringer", "Exile", "Thorn", "Raven", "Vigil", "Ember"]


# ---- typed values -----------------------------------------------------------------------------------------------------

def I(n):
	return {"integerValue": str(int(n))}


def D(x):
	return {"doubleValue": float(x)}


def S(s):
	return {"stringValue": s}


def B(b):
	return {"booleanValue": bool(b)}


def T(s):
	return {"timestampValue": s}


def M(d):
	return {"mapValue": {"fields": d}}


def A(xs):
	return {"arrayValue": {"values": xs}} if xs else {"arrayValue": {}}


def plain(v):
	(k, x), = v.items()
	if k == "integerValue":
		return int(x)
	if k == "doubleValue":
		return float(x)
	if k == "mapValue":
		return {a: plain(b) for a, b in (x.get("fields") or {}).items()}
	if k == "arrayValue":
		return [plain(e) for e in x.get("values") or []]
	if k == "nullValue":
		return None
	return x


def plain_doc(doc):
	return {k: plain(v) for k, v in (doc.get("fields") or {}).items()}


# ---- HTTP -------------------------------------------------------------------------------------------------------------

class Client:
	def __init__(self, base, project):
		self.base = base.rstrip("/")
		self.project = project
		self.docs = "%s/firestore/v1/projects/%s/databases/(default)/documents" % (self.base, project)
		self.root = "projects/%s/databases/(default)/documents" % project

	def call(self, method, url, body=None, token=None, form=None):
		data, headers = None, {}
		if form is not None:
			data = urllib.parse.urlencode(form).encode("utf-8")
			headers["Content-Type"] = "application/x-www-form-urlencoded"
		elif body is not None:
			data = json.dumps(body).encode("utf-8")
			headers["Content-Type"] = "application/json"
		if token:
			headers["Authorization"] = "Bearer " + token
		req = urllib.request.Request(url, data=data, method=method, headers=headers)
		try:
			with urllib.request.urlopen(req, timeout=20) as r:
				return r.status, json.loads(r.read().decode("utf-8") or "null")
		except urllib.error.HTTPError as e:
			raw = e.read().decode("utf-8")
			try:
				return e.code, json.loads(raw)
			except ValueError:
				return e.code, raw

	def name(self, path):
		return "%s/%s" % (self.root, path)

	def sign_up(self, key=API_KEY):
		return self.call("POST", "%s/identitytoolkit/v1/accounts:signUp?key=%s" % (self.base, key), {"returnSecureToken": True})

	def refresh(self, refresh_token):
		return self.call("POST", "%s/securetoken/v1/token?key=%s" % (self.base, API_KEY),
			form={"grant_type": "refresh_token", "refresh_token": refresh_token})

	def commit(self, writes, token=None):
		return self.call("POST", self.docs + ":commit", {"writes": writes}, token)

	def get(self, path, token=None):
		return self.call("GET", "%s/%s" % (self.docs, path), token=token)

	def patch(self, path, fields, mask, token=None):
		q = "&".join("updateMask.fieldPaths=" + urllib.parse.quote(m) for m in mask)
		return self.call("PATCH", "%s/%s?%s" % (self.docs, path, q), {"fields": fields}, token)

	def delete(self, path, token=None):
		return self.call("DELETE", "%s/%s" % (self.docs, path), token=token)

	def query(self, coll, where=None, order=None, limit=None, token=None):
		q = {"from": [{"collectionId": coll}]}
		if where:
			q["where"] = {"fieldFilter": {"field": {"fieldPath": where[0]}, "op": "EQUAL", "value": where[1]}}
		if order:
			q["orderBy"] = [{"field": {"fieldPath": order[0]}, "direction": order[1]}]
		if limit:
			q["limit"] = limit
		st, rows = self.call("POST", self.docs + ":runQuery", {"structuredQuery": q}, token)
		if st != 200:
			return st, rows
		return st, [r["document"] for r in rows if "document" in r]

	def list(self, coll, page_size, page_token=None):
		url = "%s/%s?pageSize=%d" % (self.docs, coll, page_size)
		if page_token:
			url += "&pageToken=" + urllib.parse.quote(page_token)
		return self.call("GET", url)


# ---- the game's side --------------------------------------------------------------------------------------------------

class Game:
	"""One install: anonymous identity, a player document, one commit per fight."""

	def __init__(self, c, rng, nickname=None):
		self.c, self.rng = c, rng
		st, r = c.sign_up()
		assert st == 200, (st, r)
		self.uid, self.token, self.refresh_token = r["localId"], r["idToken"], r["refreshToken"]
		self.session = "%032x" % rng.getrandbits(128)
		self.fight_no = 0
		self.nickname = nickname or "%s %s %04d" % (rng.choice(ADJ), rng.choice(NOUN), rng.randrange(10000))
		# a playing style: how well it parries and hits, and its habits per swing class
		self.parry_skill = rng.uniform(0.15, 0.85)
		self.hit_skill = rng.uniform(0.3, 0.8)
		self.win_skill = rng.uniform(0.05, 0.7)
		self.habits = {c_: rng.choice(ANSWERS[:6]) for c_ in CLASSES}

	def create_player(self):
		zeros = {k: I(0) for k in ["fights", "wins", "losses", "timeouts"] + INT_TOTALS}
		zeros.update({"seconds": D(0), "dmgDealt": D(0), "dmgTaken": D(0)})
		fields = {
			"v": I(1), "nickname": S(self.nickname), "gameVersion": S("1.2.0"), "difficulty": S("Normal"),
			"totals": M(zeros),
			"keepers": M({k: M({"fights": I(0), "wins": I(0)}) for k, _ in KEEPERS}),
			"answers": M({}), "expected": A([]), "resets": I(0), "resetAt": {"nullValue": None},
		}
		return self.c.commit([{
			"update": {"name": self.c.name("players/" + self.uid), "fields": fields},
			"currentDocument": {"exists": False},
			"updateTransforms": [{"fieldPath": "createdAt", "setToServerValue": "REQUEST_TIME"}],
		}], self.token)

	def make_fight(self, keeper=None, result=None):
		r = self.rng
		self.fight_no += 1
		keeper = r.randrange(3) if keeper is None else keeper
		result = result or ("win" if r.random() < self.win_skill else r.choice(["loss", "loss", "loss", "timeout", "quit"]))
		seconds = r.uniform(18, 95)
		ps = r.randrange(10, 45)
		ks = r.randrange(15, 60)
		pa = r.randrange(3, 16)
		kp = min(pa, int(round(pa * self.parry_skill * r.uniform(0.7, 1.1))))
		kb = r.randrange(0, max(1, ks // 4))
		kw = r.randrange(0, max(1, ks // 4))
		kh = max(0, min(ks - kp - kb - kw, int(ks * r.uniform(0.2, 0.5))))
		preds = r.randrange(10, 40)
		pc = int(preds * r.uniform(0.25, 0.75))
		conf = r.randrange(0, preds + 1)
		cc = min(conf, int(conf * r.uniform(0.4, 0.9)))
		answers = {}
		for cl in CLASSES:
			counts = {}
			n = r.randrange(0, 8)
			for _ in range(n):
				a = self.habits[cl] if r.random() < 0.6 else r.choice(ANSWERS)
				counts[a] = counts.get(a, 0) + 1
			if counts:
				answers[cl] = counts
		expected = []
		for atk in ATTACKS:
			cl = {"fastSlash": "fast", "sweepLeft": "fast", "sweepRight": "fast", "heavyCleave": "heavy",
				"delayedHeavy": "heavy", "feint": "feint", "grab": "heavy", "killer": "killer"}[atk]
			expected.append({"attack": atk, "answer": self.habits[cl], "p": round(r.uniform(0.3, 0.9), 3)})
		won = result == "win"
		f = {
			"v": 1, "player": self.uid, "clientTime": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
			"session": self.session, "fightInSession": self.fight_no, "gameVersion": "1.2.0",
			"mode": "openworld", "playMode": "hellwalker", "brain": "rl", "keeper": keeper, "keeperName": KEEPERS[keeper][1],
			"difficulty": "Normal", "skill": 0.4, "adaptive": False, "result": result, "seconds": seconds,
			"playerHealth": r.uniform(0.05, 0.8) if won else 0.0, "keeperHealth": 0.0 if won else r.uniform(0.1, 0.9),
			"dmgDealt": r.uniform(300, 1100) if not won else 1100.0, "dmgTaken": r.uniform(80, 360),
			"playerSwings": ps, "playerHits": int(ps * self.hit_skill * r.uniform(0.6, 1.0)),
			"keeperSwings": ks, "keeperHits": kh, "keeperBlocked": kb, "keeperParried": kp, "keeperWhiffed": kw,
			"parryAttempts": pa, "dodges": r.randrange(0, 20), "guardBreaks": r.randrange(0, 3),
			"keeperExposed": r.randrange(0, 3), "readsLanded": r.randrange(0, 5),
			"predictions": preds, "predictionsCorrect": pc, "confident": conf, "confidentCorrect": cc,
			"expected": expected, "answers": answers,
		}
		return "%032x" % r.getrandbits(128), f

	def fight_fields(self, f):
		ints = {"v", "fightInSession", "keeper", "playerSwings", "playerHits", "keeperSwings", "keeperHits",
			"keeperBlocked", "keeperParried", "keeperWhiffed", "parryAttempts", "dodges", "guardBreaks", "keeperExposed",
			"readsLanded", "predictions", "predictionsCorrect", "confident", "confidentCorrect"}
		out = {}
		for k, v in f.items():
			if k == "clientTime":
				out[k] = T(v)
			elif k == "expected":
				out[k] = A([M({"attack": S(e["attack"]), "answer": S(e["answer"]), "p": D(e["p"])}) for e in v])
			elif k == "answers":
				out[k] = M({cl: M({a: I(n) for a, n in m.items()}) for cl, m in v.items()})
			elif isinstance(v, bool):
				out[k] = B(v)
			elif k in ints:
				out[k] = I(v)
			elif isinstance(v, (int, float)):
				out[k] = D(v)
			else:
				out[k] = S(v)
		return out

	def fight_writes(self, fight_id, f, fields=None):
		"""The contract's commit: (1) create fights/{id}, (2) update players/{uid} with mask + transforms."""
		inc = [("totals.fights", I(1))]
		if f["result"] in ("win", "loss", "timeout"):
			inc.append(("totals." + {"win": "wins", "loss": "losses", "timeout": "timeouts"}[f["result"]], I(1)))
		for k in ("seconds", "dmgDealt", "dmgTaken"):
			inc.append(("totals." + k, D(f[k])))
		for k in INT_TOTALS:
			if f[k]:
				inc.append(("totals." + k, I(f[k])))
		kname = KEEPERS[f["keeper"]][0]
		inc.append(("keepers.%s.fights" % kname, I(1)))
		if f["result"] == "win":
			inc.append(("keepers.%s.wins" % kname, I(1)))
		for cl, m in f["answers"].items():
			for a, n in m.items():
				inc.append(("answers.%s.%s" % (cl, a), I(n)))
		fight_fields = fields if fields is not None else self.fight_fields(f)
		return [
			{"update": {"name": self.c.name("fights/" + fight_id), "fields": fight_fields},
				"currentDocument": {"exists": False},
				"updateTransforms": [{"fieldPath": "at", "setToServerValue": "REQUEST_TIME"}]},
			{"update": {"name": self.c.name("players/" + self.uid), "fields": {
				"v": I(1), "gameVersion": S(f["gameVersion"]), "difficulty": S(f["difficulty"]),
				"expected": fight_fields["expected"]}},
				"updateMask": {"fieldPaths": ["v", "gameVersion", "difficulty", "expected"]},
				"updateTransforms": [{"fieldPath": "lastSeen", "setToServerValue": "REQUEST_TIME"}]
					+ [{"fieldPath": p, "increment": v} for p, v in inc]},
		]

	def upload(self, fight_id, f):
		return self.c.commit(self.fight_writes(fight_id, f), self.token)


# ---- the checks -------------------------------------------------------------------------------------------------------

class Checks:
	def __init__(self):
		self.passed, self.failed = 0, []

	def ok(self, name, cond, detail=""):
		if cond:
			self.passed += 1
			print("  PASS  " + name)
		else:
			self.failed.append(name)
			print("  FAIL  " + name + ("   -> " + str(detail)[:400] if detail else ""))
		return cond

	def status(self, name, got, want):
		st, body = got
		msg = ""
		if isinstance(body, dict) and "error" in body:
			msg = body["error"].get("status", "")
		elif isinstance(body, list) and body and isinstance(body[0], dict) and "error" in body[0]:
			msg = body[0]["error"].get("status", "")
		return self.ok("%s  (HTTP %d %s)" % (name, st, msg), st == want, body)


def zero_answers():
	return M({cl: M({a: I(0) for a in ANSWERS}) for cl in CLASSES})


def reset_writes(c, uid):
	"""The site's reset, step 2 (contract): totals, keepers and answers to zero, expected [], resets + 1, resetAt now."""
	zeros = {k: I(0) for k in ["fights", "wins", "losses", "timeouts"] + INT_TOTALS}
	zeros.update({"seconds": D(0), "dmgDealt": D(0), "dmgTaken": D(0)})
	return [{"update": {"name": c.name("players/" + uid), "fields": {
		"totals": M(zeros), "keepers": M({k: M({"fights": I(0), "wins": I(0)}) for k, _ in KEEPERS}),
		"answers": zero_answers(), "expected": A([])}},
		"updateMask": {"fieldPaths": ["totals", "keepers", "answers", "expected"]},
		"currentDocument": {"exists": True},
		"updateTransforms": [{"fieldPath": "resets", "increment": I(1)},
			{"fieldPath": "resetAt", "setToServerValue": "REQUEST_TIME"}]}]


def survey_writes(c, uid, likert, noticed, comment):
	fields = {k: I(v) for k, v in likert.items()}
	fields["noticedAdapting"] = B(noticed)
	return [
		{"update": {"name": c.name("players/" + uid), "fields": {"survey": M(fields)}},
			"updateMask": {"fieldPaths": ["survey.feltRead", "survey.fair", "survey.difficulty", "survey.fun",
				"survey.playAgain", "survey.noticedAdapting"]},
			"currentDocument": {"exists": True},
			"updateTransforms": [{"fieldPath": "survey.at", "setToServerValue": "REQUEST_TIME"}]},
		{"update": {"name": c.name("surveys/" + uid), "fields": {"v": I(1), "comment": S(comment)}},
			"updateTransforms": [{"fieldPath": "at", "setToServerValue": "REQUEST_TIME"}]},
	]


def run(c, seed_players, site_url):
	ck = Checks()
	rng = random.Random(29092005)

	print("\n[identity]")
	st, r = c.sign_up()
	ck.ok("signUp returns idToken, refreshToken, localId, expiresIn", st == 200 and all(k in r for k in ("idToken", "refreshToken", "localId", "expiresIn")), r)
	ck.status("signUp without an API key is refused", c.sign_up(key=""), 400)

	print("\n[the game: player A]")
	a = Game(c, rng, nickname="Ashen Wanderer 4821")
	ck.status("create players/A (exists=false, createdAt = REQUEST_TIME)", a.create_player(), 200)
	st, doc = c.get("players/" + a.uid)
	pa = plain_doc(doc) if st == 200 else {}
	ck.ok("players/A is publicly readable with totals 0 and a server createdAt",
		st == 200 and pa.get("nickname") == a.nickname and pa["totals"]["fights"] == 0 and "createdAt" in pa, (st, doc))
	ck.status("creating players/A twice answers 409", a.create_player(), 409)

	fights = []
	for i, res in enumerate(["win", "loss", "win"]):
		fid, f = a.make_fight(keeper=i, result=res)
		fights.append((fid, f))
		ck.status("fight %d commit (fight create + player increments)" % (i + 1), a.upload(fid, f), 200)
	ck.status("retrying fight 3 (same fightId) answers 409 ALREADY_EXISTS", a.upload(*fights[2]), 409)

	st, doc = c.get("players/" + a.uid)
	pa = plain_doc(doc)
	t = pa["totals"]
	ck.ok("totals counted each fight once (fights 3, wins 2, losses 1) - the failed retry changed nothing",
		t["fights"] == 3 and t["wins"] == 2 and t["losses"] == 1, t)
	ck.ok("totals.parryAttempts / keeperParried / predictions are the fights' sums",
		t["parryAttempts"] == sum(f["parryAttempts"] for _, f in fights)
		and t["keeperParried"] == sum(f["keeperParried"] for _, f in fights)
		and t["predictions"] == sum(f["predictions"] for _, f in fights), t)
	ck.ok("totals.seconds is a double sum", abs(t["seconds"] - sum(f["seconds"] for _, f in fights)) < 1e-6, t["seconds"])
	ck.ok("keepers: one fight each, wins on the Warden and the Returned",
		pa["keepers"]["warden"] == {"fights": 1, "wins": 1} and pa["keepers"]["sage"] == {"fights": 1, "wins": 0}
		and pa["keepers"]["returned"] == {"fights": 1, "wins": 1}, pa["keepers"])
	want_ans = {}
	for _, f in fights:
		for cl, m in f["answers"].items():
			for k, n in m.items():
				want_ans.setdefault(cl, {})[k] = want_ans.get(cl, {}).get(k, 0) + n
	ck.ok("answers accumulate per class and answer", pa["answers"] == want_ans, (pa["answers"], want_ans))
	ck.ok("expected = the latest fight's 8 rows", pa["expected"] == fights[2][1]["expected"], pa["expected"])
	ck.ok("lastSeen set by the server", "lastSeen" in pa, pa.keys())

	print("\n[the site: profile, leaderboard, list]")
	st, docs = c.query("fights", where=("player", S(a.uid)), order=("at", "DESCENDING"), limit=20)
	ats = [plain_doc(d)["at"] for d in docs] if st == 200 else []
	ck.ok("profile query: fights where player == A order by at desc -> 3, newest first",
		st == 200 and len(docs) == 3 and ats == sorted(ats, reverse=True), (st, docs))
	ck.status("a query needing an index that firestore.indexes.json lacks fails", c.query("fights", where=("player", S(a.uid)), order=("seconds", "DESCENDING")), 400)
	st, r = c.call("POST", c.docs + ":runQuery", {"structuredQuery": {"from": [{"collectionId": "fights"}], "where": {
		"fieldFilter": {"field": {"fieldPath": "seconds"}, "op": "GREATER_THAN", "value": D(1)}}}})
	ck.ok("filters outside the contract (GREATER_THAN) are refused by the mock", st == 400, r)

	b = Game(c, rng, nickname="Hollow Pilgrim 0007")
	ck.status("create players/B", b.create_player(), 200)
	for i in range(4):
		ck.status("B fight %d" % (i + 1), b.upload(*b.make_fight(result="win")), 200)

	st, docs = c.query("players", order=("totals.wins", "DESCENDING"), limit=50)
	wins = [plain_doc(d)["totals"]["wins"] for d in docs] if st == 200 else []
	names = [d["name"].rsplit("/", 1)[1] for d in docs] if st == 200 else []
	ck.ok("leaderboard query: players order by totals.wins desc", st == 200 and wins == sorted(wins, reverse=True)
		and a.uid in names and b.uid in names and names.index(b.uid) < names.index(a.uid), (st, wins))
	seen, token, pages = [], None, 0
	while True:
		st, r = c.list("players", 1, token)
		if st != 200:
			break
		seen += [d["name"] for d in r.get("documents", [])]
		pages += 1
		token = r.get("nextPageToken")
		if not token:
			break
	ck.ok("collection list pages through players (pageSize 1, pageToken) without repeats",
		st == 200 and len(seen) == len(set(seen)) and c.name("players/" + a.uid) in seen and pages >= 2, (st, pages))

	print("\n[the site: linking a browser]")
	st, r = c.refresh(a.refresh_token)
	ck.ok("securetoken exchanges the game's refresh token -> id_token for A", st == 200 and r.get("user_id") == a.uid and r.get("id_token"), r)
	site = r.get("id_token")
	ck.status("a wrong refresh token is refused", c.refresh("not-a-token"), 400)
	ck.status("linked browser renames A (PATCH updateMask nickname)", c.patch("players/" + a.uid, {"nickname": S("Ember Vigil 1234")}, ["nickname"], site), 200)
	ck.ok("the new nickname shows", plain_doc(c.get("players/" + a.uid)[1])["nickname"] == "Ember Vigil 1234")
	ck.status("nickname of 25 characters is refused", c.patch("players/" + a.uid, {"nickname": S("x" * 25)}, ["nickname"], site), 403)
	ck.status("empty / blank nickname is refused", c.patch("players/" + a.uid, {"nickname": S("   ")}, ["nickname"], site), 403)
	ck.status("nickname with a control character is refused", c.patch("players/" + a.uid, {"nickname": S("bad\nname")}, ["nickname"], site), 403)

	print("\n[the site: survey]")
	lik = {"feltRead": 4, "fair": 3, "difficulty": 5, "fun": 4, "playAgain": 5}
	ck.status("survey commit (players/A.survey + surveys/A)", c.commit(survey_writes(c, a.uid, lik, True, "It punished my parries."), site), 200)
	sv = plain_doc(c.get("players/" + a.uid)[1]).get("survey", {})
	ck.ok("survey Likert answers are public on players/A", all(sv.get(k) == v for k, v in lik.items()) and sv.get("noticedAdapting") is True and "at" in sv, sv)
	st, r = c.get("surveys/" + a.uid, site)
	ck.ok("A reads its own private comment", st == 200 and plain_doc(r).get("comment") == "It punished my parries.", r)
	ck.status("B cannot read A's comment", c.get("surveys/" + a.uid, b.token), 403)
	ck.status("nobody signed out can read A's comment", c.get("surveys/" + a.uid), 403)
	ck.status("listing surveys is refused", c.list("surveys", 10), 403)
	bad = dict(lik, fun=6)
	ck.status("a Likert answer of 6 is refused", c.commit(survey_writes(c, a.uid, bad, False, ""), site), 403)
	ck.status("a 501-character comment is refused", c.commit(survey_writes(c, a.uid, lik, False, "x" * 501), site), 403)

	print("\n[attacks]")
	ck.status("B deleting A's fight", c.delete("fights/" + fights[0][0], b.token), 403)
	ck.status("signed-out delete of A's fight", c.delete("fights/" + fights[0][0]), 403)
	ck.status("B overwriting A's nickname", c.patch("players/" + a.uid, {"nickname": S("pwned")}, ["nickname"], b.token), 403)
	ck.status("signed-out write to A", c.patch("players/" + a.uid, {"nickname": S("pwned")}, ["nickname"]), 403)
	fid, f = b.make_fight()
	f["player"] = a.uid
	ck.status("B uploading a fight in A's name", c.commit(b.fight_writes(fid, f)[:1], b.token), 403)
	ck.status("A editing its own fight (fights are immutable)", c.patch("fights/" + fights[0][0], {"result": S("win")}, ["result"], a.token), 403)
	ck.status("A deleting its player document", c.delete("players/" + a.uid, a.token), 403)
	ck.status("garbage bearer token", c.get("players/" + a.uid, "nonsense"), 401)

	def malformed(name, mutate, fight_id=None):
		fid, f = a.make_fight()
		fields = a.fight_fields(f)
		mutate(fields)
		st_body = c.commit(a.fight_writes(fight_id or fid, f, fields), a.token)
		a.fight_no -= 1
		ck.status("malformed fight: " + name, st_body, 403)

	malformed("a field missing", lambda x: x.pop("dodges"))
	malformed("an extra field", lambda x: x.__setitem__("cheat", B(True)))
	malformed("wrong type (keeper as a string)", lambda x: x.__setitem__("keeper", S("zero")))
	malformed("negative count", lambda x: x.__setitem__("playerHits", I(-1)))
	malformed("health above 1", lambda x: x.__setitem__("playerHealth", D(1.5)))
	malformed("unknown result", lambda x: x.__setitem__("result", S("draw")))
	malformed("more correct reads than reads", lambda x: x.__setitem__("predictionsCorrect", I(10 ** 5)))
	malformed("unknown answer key", lambda x: x.__setitem__("answers", M({"fast": M({"teleport": I(1)})})))
	malformed("nine expected rows", lambda x: x.__setitem__("expected", A([M({"attack": S("x"), "answer": S("y"), "p": D(0.5)})] * 9)))
	malformed("fight id not 32 hex", lambda x: None, fight_id="NOT-HEX")

	fid, f = a.make_fight()
	w = a.fight_writes(fid, f)
	w[0]["updateTransforms"] = []
	w[0]["update"]["fields"]["at"] = T("2020-01-01T00:00:00Z")
	ck.status("fight with a client-chosen `at` (not REQUEST_TIME)", c.commit(w, a.token), 403)
	fid, f = a.make_fight()
	w = a.fight_writes(fid, f)
	w[1]["updateTransforms"].append({"fieldPath": "totals.dodges", "increment": I(-10 ** 6)})
	ck.status("player update driving a total below zero (commit is atomic: the fight is not stored either)", c.commit(w, a.token), 403)
	ck.status("... and that fight really is absent", c.get("fights/" + fid), 404)
	fid, f = a.make_fight()
	w = a.fight_writes(fid, f)
	w[1]["updateTransforms"] = [x if x["fieldPath"] != "totals.fights" else {"fieldPath": "totals.fights", "increment": I(5)} for x in w[1]["updateTransforms"]]
	ck.status("inflating totals.fights by 5 in one commit", c.commit(w, a.token), 403)
	fid, f = a.make_fight()
	w = a.fight_writes(fid, f)
	w[1]["updateTransforms"].append({"fieldPath": "totals.keeperParried", "increment": I(2 * 10 ** 6)})
	ck.status("inflating another total (parries +2,000,000) in one commit", c.commit(w, a.token), 403)
	fid, f = a.make_fight()
	w = a.fight_writes(fid, f)
	w[1]["updateTransforms"].append({"fieldPath": "keepers.sage.wins", "increment": I(3)})
	ck.status("inflating a keeper's wins by 3 in one commit", c.commit(w, a.token), 403)
	ck.status("changing createdAt", c.patch("players/" + a.uid, {"createdAt": T("2020-01-01T00:00:00Z")}, ["createdAt"], a.token), 403)
	ck.status("bumping resets without the reset (totals kept)", c.commit([{"update": {"name": c.name("players/" + a.uid), "fields": {}},
		"updateMask": {"fieldPaths": []}, "updateTransforms": [{"fieldPath": "resets", "increment": I(1)},
		{"fieldPath": "resetAt", "setToServerValue": "REQUEST_TIME"}]}], a.token), 403)
	ck.status("B creating a player document under another uid", c.commit([{"update": {"name": c.name("players/" + a.uid + "x"),
		"fields": {"v": I(1), "nickname": S("x")}}, "currentDocument": {"exists": False},
		"updateTransforms": [{"fieldPath": "createdAt", "setToServerValue": "REQUEST_TIME"}]}], b.token), 403)
	ck.status("writing outside the contract's collections", c.patch("admins/" + a.uid, {"x": I(1)}, ["x"], a.token), 403)
	st, doc = c.get("players/" + a.uid)
	ck.ok("after all attacks A still has 3 fights and its nickname", plain_doc(doc)["totals"]["fights"] == 3
		and plain_doc(doc)["nickname"] == "Ember Vigil 1234", plain_doc(doc)["totals"])

	print("\n[the site: reset]")
	before = plain_doc(c.get("players/" + a.uid)[1])
	deleted = 0
	while True:
		st, docs = c.query("fights", where=("player", S(a.uid)), limit=300)
		if st != 200 or not docs:
			break
		st2, r = c.commit([{"delete": d["name"]} for d in docs], site)
		if not ck.ok("delete a page of A's fights (%d) as the linked browser" % len(docs), st2 == 200, r):
			break
		deleted += len(docs)
	ck.ok("all 3 of A's fights deleted", deleted == 3, deleted)
	ck.status("reset: player totals / keepers / answers to zero, expected [], resets+1, resetAt", c.commit(reset_writes(c, a.uid), site), 200)
	st, docs = c.query("fights", where=("player", S(a.uid)), order=("at", "DESCENDING"), limit=20)
	ck.ok("after the reset A has no fights", st == 200 and docs == [], docs)
	after = plain_doc(c.get("players/" + a.uid)[1])
	ck.ok("after the reset every total is zero", all(v == 0 for v in after["totals"].values()), after["totals"])
	ck.ok("... keepers and answers zero, expected empty", all(v == {"fights": 0, "wins": 0} for v in after["keepers"].values())
		and all(all(n == 0 for n in m.values()) for m in after["answers"].values()) and after["expected"] == [], after)
	ck.ok("... resets 1 and resetAt set", after["resets"] == 1 and after.get("resetAt"), (after.get("resets"), after.get("resetAt")))
	ck.ok("... nickname, createdAt and survey kept", after["nickname"] == before["nickname"] and after["createdAt"] == before["createdAt"]
		and after.get("survey") == before.get("survey"), after)
	ck.ok("... the private comment kept", c.get("surveys/" + a.uid, site)[0] == 200)
	ck.status("B cannot reset A", c.commit(reset_writes(c, a.uid), b.token), 403)
	fid, f = a.make_fight(result="win")
	ck.status("the game keeps uploading after a reset (fights 0 -> 1)", a.upload(fid, f), 200)
	ck.ok("... and totals start again from zero", plain_doc(c.get("players/" + a.uid)[1])["totals"]["fights"] == 1)

	link = None
	if seed_players:
		print("\n[seed: %d sample players for the site in mock mode]" % seed_players)
		games = []
		for i in range(seed_players):
			g = Game(c, rng)
			g.create_player()
			n = rng.randrange(3, 14)
			ok = 0
			for _ in range(n):
				st, _ = g.upload(*g.make_fight())
				ok += st == 200
			games.append(g)
			print("  %-26s %2d fights  uid %s" % (g.nickname, ok, g.uid))
		lucky = games[0]
		lik = {"feltRead": 4, "fair": 4, "difficulty": 4, "fun": 5, "playAgain": 4}
		c.commit(survey_writes(c, lucky.uid, lik, True, "Seeded by e2e_test.py"), lucky.token)
		link = "%s/?mock=%s#/me?t=%s" % (site_url.rstrip("/"), urllib.parse.quote(c.base, safe=":/"), urllib.parse.quote(lucky.refresh_token))
		print("\n  profile of a seeded player:  %s/?mock=%s#/p/%s" % (site_url.rstrip("/"), c.base, b.uid))
		print("  open as 'my stats' (linked): " + link)

	print("\n%d passed, %d failed" % (ck.passed, len(ck.failed)))
	for name in ck.failed:
		print("  failed: " + name)
	return 0 if not ck.failed else 1


def free_port():
	s = socket.socket()
	s.bind(("127.0.0.1", 0))
	port = s.getsockname()[1]
	s.close()
	return port


def healthy(url):
	try:
		with urllib.request.urlopen(url.rstrip("/") + "/__mock/health", timeout=2) as r:
			return r.status == 200
	except Exception:
		return False


def main():
	ap = argparse.ArgumentParser(description="HellwalkerRL telemetry end-to-end test against the mock")
	ap.add_argument("--url", default="http://127.0.0.1:8099", help="a running mock_firebase.py")
	ap.add_argument("--project", default="hellwalker-mock")
	ap.add_argument("--spawn", action="store_true", help="start a private mock (free port, throwaway database)")
	ap.add_argument("--seed", type=int, default=8, help="sample players left in the database afterwards")
	ap.add_argument("--site", default="http://127.0.0.1:8000", help="where web/ is served (only for the printed links)")
	args = ap.parse_args()
	proc, tmp = None, None
	url = args.url
	if args.spawn or not healthy(url):
		if not args.spawn:
			print("no mock at %s - starting a private one" % url)
		port = free_port()
		url = "http://127.0.0.1:%d" % port
		tmp_dir = os.environ.get("TMP") or tempfile.gettempdir()
		tmp = os.path.join(tmp_dir, "hw_e2e_%d.json" % port)
		proc = subprocess.Popen([sys.executable, os.path.join(HERE, "mock_firebase.py"), "--port", str(port), "--db", tmp, "--quiet"])
		for _ in range(100):
			if healthy(url):
				break
			time.sleep(0.05)
	try:
		print("mock: " + url)
		return run(Client(url, args.project), args.seed, args.site)
	finally:
		if proc:
			proc.terminate()
			proc.wait(5)
			for p in (tmp, tmp + ".tmp"):
				if p and os.path.exists(p):
					os.remove(p)


if __name__ == "__main__":
	sys.exit(main())
