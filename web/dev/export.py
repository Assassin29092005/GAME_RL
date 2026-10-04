#!/usr/bin/env python3
"""HellwalkerRL - research export: every player (with the survey's numbers) and every fight to CSV (stdlib only).

    python web/dev/export.py                                  # the live project in web/config.js
    python web/dev/export.py --project my-project --api-key AIza...
    python web/dev/export.py --mock http://127.0.0.1:8099     # the local mock (web/dev/mock_firebase.py)
    python web/dev/export.py --out D:/study/export-2026-10-03

Reads over the public Firestore REST API (the collection list, pageSize / pageToken - no sign-in needed: players and
fights are public, web/firebase/firestore.rules). Writes players.csv and fights.csv (UTF-8 with a BOM, so Excel opens
them right). fights.csv has one column set for both fight schemas: a v1 fight (games before 1.4.0) fills the v2 columns
with what it implied (assist off, slowmoScale 1, keeperDamageScale 1, parryWindowFrames 12) and leaves insight blank;
the `v` column tells them apart. The survey's free-text comments are private (surveys/{uid}): read them in the Firebase console
(Firestore -> surveys), or export them with a service account - they are not in these files.
"""

import argparse
import csv
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_JS = os.path.join(HERE, "..", "config.js")

TOTALS = ["fights", "wins", "losses", "timeouts", "seconds", "dmgDealt", "dmgTaken", "playerSwings", "playerHits",
	"keeperSwings", "keeperHits", "keeperBlocked", "keeperParried", "keeperWhiffed", "parryAttempts", "dodges",
	"readsLanded", "predictions", "predictionsCorrect", "confident", "confidentCorrect"]
KEEPERS = ["warden", "sage", "returned"]
CLASSES = ["fast", "heavy", "feint", "killer"]
ANSWERS = ["parry", "block", "stepL", "stepR", "stepB", "stepF", "attack", "none"]
SURVEY = ["feltRead", "fair", "difficulty", "fun", "playAgain", "noticedAdapting", "at"]
FIGHT_SCALARS = ["v", "player", "at", "clientTime", "session", "fightInSession", "gameVersion", "mode", "playMode", "brain",
	"keeper", "keeperName", "difficulty", "skill", "adaptive", "result", "seconds", "playerHealth", "keeperHealth",
	"dmgDealt", "dmgTaken", "playerSwings", "playerHits", "keeperSwings", "keeperHits", "keeperBlocked", "keeperParried",
	"keeperWhiffed", "parryAttempts", "dodges", "guardBreaks", "keeperExposed", "readsLanded", "predictions",
	"predictionsCorrect", "confident", "confidentCorrect"]
# Contract v2 (game 1.4.0+). A v1 fight (games before 1.4.0) had no assist, no damage scale and the 12-frame parry
# window, so its row reads off / 1.0 / 1.0 / 12; its insight is unknown (blank), not 0.
FIGHT_V2 = ["assist", "slowmoScale", "keeperDamageScale", "parryWindowFrames", "insight"]
V1_DEFAULTS = {"assist": "off", "slowmoScale": 1.0, "keeperDamageScale": 1.0, "parryWindowFrames": 12, "insight": ""}


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
	return x  # strings, booleans, timestamps (RFC 3339 text)


def read_config():
	try:
		with open(CONFIG_JS, "r", encoding="utf-8") as f:
			text = f.read()
	except OSError:
		return "", ""
	get = lambda name: (re.search(name + r'\s*:\s*"([^"]*)"', text) or [None, ""])[1]
	return get("apiKey"), get("projectId")


def list_collection(base, coll, api_key, page_size):
	docs, token = [], None
	while True:
		q = {"pageSize": str(page_size)}
		if token:
			q["pageToken"] = token
		if api_key:
			q["key"] = api_key
		url = "%s/%s?%s" % (base, coll, urllib.parse.urlencode(q))
		try:
			with urllib.request.urlopen(url, timeout=60) as r:
				page = json.loads(r.read().decode("utf-8") or "{}")
		except urllib.error.HTTPError as e:
			sys.exit("reading %s failed: HTTP %d %s" % (coll, e.code, e.read().decode("utf-8", "replace")[:400]))
		except urllib.error.URLError as e:
			sys.exit("reading %s failed: %s" % (coll, e.reason))
		for d in page.get("documents", []):
			data = {k: plain(v) for k, v in (d.get("fields") or {}).items()}
			data["_id"] = d["name"].rsplit("/", 1)[1]
			docs.append(data)
		token = page.get("nextPageToken")
		print("  %s: %d" % (coll, len(docs)), end="\r", flush=True)
		if not token:
			print()
			return docs


def ratio(a, b):
	return round(a / b, 6) if b else ""


def player_row(p):
	t = p.get("totals") or {}
	k = p.get("keepers") or {}
	a = p.get("answers") or {}
	s = p.get("survey") or {}
	row = {"uid": p["_id"], "nickname": p.get("nickname", ""), "createdAt": p.get("createdAt", ""),
		"lastSeen": p.get("lastSeen", ""), "gameVersion": p.get("gameVersion", ""), "difficulty": p.get("difficulty", ""),
		"resets": p.get("resets", 0), "resetAt": p.get("resetAt") or ""}
	for f in TOTALS:
		row["totals_" + f] = t.get(f, 0)
	fights = t.get("fights", 0)
	row["parryRate"] = ratio(t.get("keeperParried", 0), t.get("parryAttempts", 0))
	row["winRate"] = ratio(t.get("wins", 0), fights)
	row["hitAccuracy"] = ratio(t.get("playerHits", 0), t.get("playerSwings", 0))
	row["dodgesPerFight"] = ratio(t.get("dodges", 0), fights)
	row["dmgPerMinute"] = ratio(t.get("dmgDealt", 0), t.get("seconds", 0) / 60.0)
	row["readAccuracy"] = ratio(t.get("predictionsCorrect", 0), t.get("predictions", 0))
	row["confidentReadAccuracy"] = ratio(t.get("confidentCorrect", 0), t.get("confident", 0))
	for kk in KEEPERS:
		r = k.get(kk) or {}
		row["%s_fights" % kk] = r.get("fights", 0)
		row["%s_wins" % kk] = r.get("wins", 0)
	for c in CLASSES:
		m = a.get(c) or {}
		for ans in ANSWERS:
			row["answers_%s_%s" % (c, ans)] = m.get(ans, 0)
	for f in SURVEY:
		v = s.get(f, "")
		row["survey_" + f] = int(v) if isinstance(v, bool) else v
	for i in range(8):
		e = (p.get("expected") or [])[i] if i < len(p.get("expected") or []) else {}
		row["expected%d_attack" % i] = e.get("attack", "")
		row["expected%d_answer" % i] = e.get("answer", "")
		row["expected%d_p" % i] = e.get("p", "")
	return row


def fight_row(f):
	row = {"fightId": f["_id"]}
	for k in FIGHT_SCALARS:
		v = f.get(k, "")
		row[k] = int(v) if isinstance(v, bool) else v
	for k in FIGHT_V2:
		row[k] = f[k] if k in f else V1_DEFAULTS[k]
	row["parryRate"] = ratio(f.get("keeperParried", 0), f.get("parryAttempts", 0))
	row["readAccuracy"] = ratio(f.get("predictionsCorrect", 0), f.get("predictions", 0))
	a = f.get("answers") or {}
	for c in CLASSES:
		m = a.get(c) or {}
		for ans in ANSWERS:
			row["answers_%s_%s" % (c, ans)] = m.get(ans, 0)
	exp = f.get("expected") or []
	for i in range(8):
		e = exp[i] if i < len(exp) else {}
		row["expected%d_attack" % i] = e.get("attack", "")
		row["expected%d_answer" % i] = e.get("answer", "")
		row["expected%d_p" % i] = e.get("p", "")
	return row


def _cell(v):
	# Spreadsheet formula injection: a nickname or comment starting with = + - @ (or a tab / CR) would run as a formula in
	# Excel or Sheets; a leading quote keeps it text.
	if isinstance(v, str) and v[:1] in ("=", "+", "-", "@", "\t", "\r"):
		return "'" + v
	return v


def write_csv(path, rows):
	rows = [{k: _cell(v) for k, v in r.items()} for r in rows]
	if not rows:
		with open(path, "w", encoding="utf-8-sig", newline="") as f:
			f.write("")
		return
	cols = list(rows[0].keys())
	with open(path, "w", encoding="utf-8-sig", newline="") as f:
		w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
		w.writeheader()
		w.writerows(rows)


def main():
	ap = argparse.ArgumentParser(description="HellwalkerRL research export (players + fights -> CSV)")
	ap.add_argument("--mock", help="the mock's base URL, e.g. http://127.0.0.1:8099")
	ap.add_argument("--project", help="Firebase project id (default: web/config.js)")
	ap.add_argument("--api-key", help="Firebase web API key (default: web/config.js)")
	ap.add_argument("--out", default=os.path.join(HERE, "export"), help="output folder (default web/dev/export)")
	ap.add_argument("--page-size", type=int, default=300)
	args = ap.parse_args()
	cfg_key, cfg_project = read_config()
	api_key = args.api_key if args.api_key is not None else cfg_key
	project = args.project or cfg_project or ("hellwalker-mock" if args.mock else "")
	if not project:
		sys.exit("No project id: fill projectId in web/config.js, or pass --project (or --mock for the local mock).")
	if args.mock:
		base = "%s/firestore/v1/projects/%s/databases/(default)/documents" % (args.mock.rstrip("/"), project)
		api_key = api_key or "mock-api-key"
	else:
		base = "https://firestore.googleapis.com/v1/projects/%s/databases/(default)/documents" % project
	print("reading %s" % base)
	players = list_collection(base, "players", api_key, args.page_size)
	fights = list_collection(base, "fights", api_key, args.page_size)
	os.makedirs(args.out, exist_ok=True)
	fights.sort(key=lambda f: (f.get("player", ""), f.get("at", "")))
	write_csv(os.path.join(args.out, "players.csv"), [player_row(p) for p in players])
	write_csv(os.path.join(args.out, "fights.csv"), [fight_row(f) for f in fights])
	surveyed = sum(1 for p in players if p.get("survey"))
	print("wrote %d players (%d answered the survey) and %d fights to %s" % (len(players), surveyed, len(fights), os.path.abspath(args.out)))
	print("(survey comments are private: Firebase console -> Firestore -> surveys)")


if __name__ == "__main__":
	main()
