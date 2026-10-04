// HELLWALKER site - demo mode: no Firebase project configured, so every page renders from generated sample data.
// Deterministic (a fixed seed and a fixed calendar), shaped like the contract's documents, and consistent: each
// player's totals are the sums of its generated fights. Older fights are telemetry v1; newer ones v2 (game 1.4.0: the
// parry assist, keeper damage and the Adaptive AI keeper's insight, which rises over a session as it learns the walker
// and starts at 0 every launch). Changes made on "my stats" (rename, survey, reset) live in memory only and vanish on
// reload. The banner says so.

import { ANSWERS, CLASSES } from "./model.js";

function mulberry32(seed) {
	let a = seed >>> 0;
	return () => {
		a = (a + 0x6d2b79f5) >>> 0;
		let t = a;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

const ADJ = ["Ashen", "Hollow", "Crimson", "Silent", "Ember", "Grave", "Pale", "Iron", "Wandering", "Broken", "Gilded",
	"Restless", "Sunken", "Bitter", "Quiet", "Scarred", "Veiled", "Last"];
const NOUN = ["Wanderer", "Pilgrim", "Blade", "Lantern", "Moth", "Shade", "Bellringer", "Exile", "Thorn", "Raven",
	"Vigil", "Ember", "Penitent", "Oath", "Cinder", "Heron", "Warden", "Hound"];
const KEEPER_NAMES = ["The Ninefold Warden", "The Monkey Sage", "The Warden, Returned"];
const KEEPER_KEYS = ["warden", "sage", "returned"];
const ATTACKS = [["fastSlash", "fast"], ["sweepLeft", "fast"], ["sweepRight", "fast"], ["heavyCleave", "heavy"],
	["delayedHeavy", "heavy"], ["feint", "feint"], ["grab", "heavy"], ["killer", "killer"]];
const ANCHOR = Date.UTC(2026, 8, 28, 21, 0, 0); // the demo's "now" for its own calendar: 28 Sep 2026
const HOUR = 3600e3, DAY = 24 * HOUR;
const KEEPER_HP = [1100, 1100, 1375], PLAYER_HP = 360;
const DIFFS_V1 = ["Easy", "Normal", "Hard", "Hellwalker", "Adaptive"], DIFFS_V2 = ["Easy", "Normal", "Hard", "Hellwalker"];
// The game's difficulty presets (UHWSettingsSubsystem::PresetFor): the Adaptive AI keeper's skill range, the scale on
// its hits, the parry assist's slow-motion scale.
const PRESETS = {
	Easy: { lo: 0, hi: 0.4, dmg: 0.6, slow: 0.4 },
	Normal: { lo: 0, hi: 0.7, dmg: 0.75, slow: 0.6 },
	Hard: { lo: 0.15, hi: 0.85, dmg: 0.85, slow: 0.75 },
	Hellwalker: { lo: 0.3, hi: 1, dmg: 0.9, slow: 0.85 },
};
const round3 = (x) => Math.round(x * 1000) / 1000;

/** A stand-in for the game's insight (HW::FRLInsight), for the demo only: after every Adaptive AI fight it moves toward
 *  a normalised read accuracy; a fight with few predictions moves it little. */
function afterFight(x, preds, correct) {
	if (preds <= 0) return x;
	const target = Math.max(0, Math.min(1, (correct / preds - 0.3) / 0.45));
	return x + 0.45 * Math.min(1, preds / 20) * (target - x);
}

function binom(r, n, p) {
	let k = 0;
	for (let i = 0; i < n; i++) if (r() < p) k++;
	return k;
}

function hex(r, n) {
	let s = "";
	for (let i = 0; i < n; i++) s += Math.floor(r() * 16).toString(16);
	return s;
}

function emptyTotals() {
	const t = {};
	for (const k of ["fights", "wins", "losses", "timeouts", "seconds", "dmgDealt", "dmgTaken", "playerSwings", "playerHits",
		"keeperSwings", "keeperHits", "keeperBlocked", "keeperParried", "keeperWhiffed", "parryAttempts", "dodges",
		"readsLanded", "predictions", "predictionsCorrect", "confident", "confidentCorrect"]) t[k] = 0;
	return t;
}

function emptyAnswers() {
	const a = {};
	for (const [c] of CLASSES) {
		a[c] = {};
		for (const k of ANSWERS) a[c][k] = 0;
	}
	return a;
}

function pickWeighted(r, weights) {
	let x = r() * weights.reduce((s, w) => s + w, 0);
	for (let i = 0; i < weights.length; i++) {
		x -= weights[i];
		if (x <= 0) return i;
	}
	return weights.length - 1;
}

/** One sample player and its fights. */
function makePlayer(r, opts = {}) {
	const uid = opts.uid || "demo" + hex(r, 24);
	const nickname = opts.nickname || `${ADJ[Math.floor(r() * ADJ.length)]} ${NOUN[Math.floor(r() * NOUN.length)]} ${String(Math.floor(r() * 10000)).padStart(4, "0")}`;
	const skill = opts.skill ?? Math.pow(r(), 0.9);
	const parrySkill = Math.min(0.9, 0.12 + 0.55 * skill + 0.25 * r());
	const hitSkill = Math.min(0.85, 0.3 + 0.35 * skill + 0.2 * r());
	const nFights = opts.fights ?? Math.floor(Math.pow(r(), 1.6) * 64);
	const prefDiff = opts.difficulty || ["Normal", "Normal", "Normal", "Hard", "Easy", "Hellwalker", "Adaptive"][Math.floor(r() * 7)];
	// habits: for each class of keeper swing, one favourite answer and how strongly
	const habits = {};
	for (const [c] of CLASSES) {
		const dom = (opts.habits && opts.habits[c]) || ANSWERS[pickWeighted(r, [5, 3, 2.5, 2.5, 2, 0.8, 2, 1])];
		const strength = opts.habitStrength ?? 0.3 + 0.5 * r();
		habits[c] = ANSWERS.map((a) => (a === dom ? strength : ((1 - strength) / 7) * (0.3 + 1.4 * r())));
		const sum = habits[c].reduce((s, w) => s + w, 0);
		habits[c] = habits[c].map((w) => w / sum);
	}
	const readability = opts.readability ?? 0.25 + 0.45 * Math.max(...CLASSES.map(([c]) => Math.max(...habits[c])));
	const createdAt = new Date(ANCHOR - (8 + r() * 40) * DAY);
	// Fights from v2From on come from game 1.4.0 (telemetry v2: parry assist, keeper damage, insight); a quarter of the
	// walkers never updated.
	const v2From = Math.floor(nFights * (opts.v2From ?? (r() < 0.25 ? 1 : 0.2 + 0.6 * r())));
	const assistPref = opts.assist || ["ring+slowmo", "ring+slowmo", "ring+slowmo", "ring", "ring", "off"][Math.floor(r() * 6)];
	const [lenLo, lenHi] = opts.sessionLen || [2, 6];
	const sessionLength = () => lenLo + Math.floor(r() * (lenHi - lenLo + 1));

	const fights = [];
	let t = createdAt.getTime() + r() * 6 * HOUR;
	let session = hex(r, 32), inSession = 0, len = sessionLength();
	// One walk per launch (chosen on the title screen); the insight starts at 0 every launch. In some sessions the
	// walker changes trick at fight `switchAt`, and the keeper's predictions miss for two fights.
	let script = r() < 0.15, legacy66 = r() < 0.15, insight = 0, switchAt = -1;
	for (let i = 0; i < nFights; i++) {
		if (inSession >= len) { // a new launch, a few hours or days later
			t += (3 + r() * 40) * HOUR;
			session = hex(r, 32);
			inSession = 0;
			len = sessionLength();
			script = r() < 0.15;
			legacy66 = r() < 0.15;
			insight = 0;
			switchAt = r() < 0.3 ? 3 + Math.floor(r() * 3) : -1;
		}
		t += (2 + r() * 6) * 60e3;
		inSession++;
		const v2 = i >= v2From;
		const keeper = pickWeighted(r, [0.45, 0.33, 0.22]);
		const diffs = v2 ? DIFFS_V2 : DIFFS_V1;
		let diff = r() < 0.8 ? prefDiff : diffs[Math.floor(r() * diffs.length)];
		if (v2 && diff === "Adaptive") diff = "Normal"; // old saves load as Normal
		const preset = PRESETS[diff] || PRESETS.Normal;
		const known = v2 && !script ? insight : null; // how well the keeper knows the walker going into this fight
		const diffPen = { Easy: -0.18, Normal: 0, Hard: 0.08, Hellwalker: 0.14, Adaptive: 0.02 }[diff];
		const learn = 0.12 * (i / Math.max(1, nFights));
		// v2: a trick wins while the keeper does not know you yet, and every difficulty hits softer than before
		const arc = known === null ? 0 : 0.16 * (1 - known) - 0.1 * known;
		const soft = v2 ? (1 - preset.dmg) * 0.35 : 0;
		const pWin = Math.max(0.03, Math.min(0.88, 0.06 + 0.55 * skill - [0, 0.04, 0.12][keeper] - diffPen + learn + (script ? 0.12 : 0) + arc + soft));
		const roll = r();
		const result = roll < pWin ? "win" : roll < pWin + 0.04 ? "timeout" : roll < pWin + 0.07 ? "quit" : "loss";
		const won = result === "win";
		const seconds = won ? 32 + r() * 60 : result === "quit" ? 6 + r() * 25 : 16 + r() * 58;
		const ks = Math.round(seconds * (0.9 + r() * 0.5));
		const pa = Math.round((2 + r() * 6) * (0.4 + habits.fast[0] * 2 + habits.heavy[0]) * (seconds / 40));
		const kp = binom(r, pa, parrySkill);
		const kb = binom(r, Math.max(0, ks - kp), 0.12 + 0.3 * habits.heavy[1]);
		const kw = binom(r, Math.max(0, ks - kp - kb), 0.22 + 0.2 * skill);
		const kh = Math.max(0, Math.min(ks - kp - kb - kw, Math.round((ks - kp - kb - kw) * (0.35 + 0.3 * (1 - skill)))));
		const ps = Math.round(seconds * (0.35 + 0.35 * r()));
		const ph = binom(r, ps, hitSkill);
		const keeperHealth = won ? 0 : result === "quit" ? 0.5 + 0.5 * r() : 0.1 + 0.85 * r() * (1 - skill * 0.5);
		const playerHealth = won ? 0.05 + 0.7 * r() : 0;
		const dmgDealt = KEEPER_HP[keeper] * (1 - keeperHealth);
		const dmgTaken = PLAYER_HP * (1 - playerHealth) * (result === "quit" ? 0.4 : 1);
		const warm = Math.min(1, (inSession - 1) / 3);
		const changed = switchAt > 0 && inSession >= switchAt && inSession < switchAt + 2;
		const preds = script ? 0 : Math.round(ks * (0.45 + 0.25 * r()));
		const pc = script ? 0 : binom(r, preds, Math.min(0.9, readability * (0.55 + 0.5 * warm) * (changed ? 0.55 : 1)));
		const conf = script ? 0 : binom(r, preds, 0.25 + 0.4 * (readability - 0.25));
		const cc = script ? 0 : Math.min(conf, binom(r, conf, Math.min(0.95, readability + 0.12)));
		const answers = {};
		const swingsByClass = { fast: 0.42, heavy: 0.3, feint: 0.18, killer: 0.1 };
		for (const [c] of CLASSES) {
			const n = Math.round(ks * 0.6 * swingsByClass[c] * (0.7 + 0.6 * r()));
			const m = {};
			for (let k = 0; k < n; k++) {
				const a = ANSWERS[pickWeighted(r, habits[c])];
				m[a] = (m[a] || 0) + 1;
			}
			if (Object.keys(m).length) answers[c] = m;
		}
		const expected = script ? [] : ATTACKS.map(([attack, c]) => {
			const w = habits[c];
			let best = 0;
			for (let k = 1; k < w.length; k++) if (w[k] > w[best]) best = k;
			const sure = Math.min(0.95, w[best] * (0.75 + 0.35 * warm) + 0.1 * r());
			return { attack, answer: r() < 0.85 ? ANSWERS[best] : ANSWERS[Math.floor(r() * ANSWERS.length)], p: Math.round(sure * 1000) / 1000 };
		});
		const arena = r() < 0.1;
		const fight = {
			id: hex(r, 32), v: 1, player: uid, at: new Date(t), clientTime: new Date(t - 400), session, fightInSession: inSession,
			gameVersion: i > nFights * 0.35 ? "1.3.0" : "1.2.0", mode: arena ? "arena" : "openworld",
			playMode: arena ? "arena" : script ? "pathbreaker" : legacy66 ? "66days" : "hellwalker", brain: script ? "script" : "rl",
			keeper, keeperName: KEEPER_NAMES[keeper], difficulty: diff,
			skill: { Easy: 0, Normal: 0.4, Hard: 0.75, Hellwalker: 1, Adaptive: 0.3 + 0.4 * r() }[diff], adaptive: diff === "Adaptive",
			result, seconds, playerHealth, keeperHealth, dmgDealt, dmgTaken, playerSwings: ps, playerHits: ph,
			keeperSwings: ks, keeperHits: kh, keeperBlocked: kb, keeperParried: kp, keeperWhiffed: kw, parryAttempts: pa,
			dodges: Math.round(seconds / 60 * (4 + 14 * (habits.fast[2] + habits.fast[3] + habits.heavy[2] + habits.heavy[3]) * r() + 3 * r())),
			guardBreaks: binom(r, 3, 0.25), keeperExposed: binom(r, 3, 0.15 + 0.3 * skill), readsLanded: binom(r, cc, 0.35),
			predictions: preds, predictionsCorrect: pc, confident: conf, confidentCorrect: cc, expected, answers,
		};
		if (v2) { // telemetry v2 (game 1.4.0): two walks, four difficulties, the parry assist, keeper damage, insight
			Object.assign(fight, {
				v: 2, gameVersion: "1.4.0", playMode: arena ? "arena" : script ? "pathbreaker" : "hellwalker",
				skill: known === null ? 0 : round3(preset.lo + (preset.hi - preset.lo) * known), adaptive: known !== null,
				assist: assistPref, slowmoScale: assistPref === "ring+slowmo" ? preset.slow : 1, keeperDamageScale: preset.dmg,
				parryWindowFrames: 12, insight: known === null ? 0 : round3(known),
			});
			if (known !== null) insight = afterFight(insight, preds, pc);
		}
		fights.push(fight);
	}

	// Slide the whole history so the latest duel lands on a plausible "last seen" before the demo's fixed now.
	const lastT = fights.length ? fights[fights.length - 1].at.getTime() : createdAt.getTime();
	const target = ANCHOR - (opts.lastSeenHoursAgo ?? Math.pow(r(), 2) * 20 * 24) * HOUR;
	const shift = target - lastT;
	createdAt.setTime(createdAt.getTime() + shift);
	for (const f of fights) {
		f.at = new Date(f.at.getTime() + shift);
		f.clientTime = new Date(f.clientTime.getTime() + shift);
	}

	const totals = emptyTotals();
	const keepers = { warden: { fights: 0, wins: 0 }, sage: { fights: 0, wins: 0 }, returned: { fights: 0, wins: 0 } };
	const answers = emptyAnswers();
	for (const f of fights) {
		totals.fights++;
		if (f.result === "win") totals.wins++;
		if (f.result === "loss") totals.losses++;
		if (f.result === "timeout") totals.timeouts++;
		for (const k of ["seconds", "dmgDealt", "dmgTaken", "playerSwings", "playerHits", "keeperSwings", "keeperHits",
			"keeperBlocked", "keeperParried", "keeperWhiffed", "parryAttempts", "dodges", "readsLanded", "predictions",
			"predictionsCorrect", "confident", "confidentCorrect"]) totals[k] += f[k];
		keepers[KEEPER_KEYS[f.keeper]].fights++;
		if (f.result === "win") keepers[KEEPER_KEYS[f.keeper]].wins++;
		for (const [c, m] of Object.entries(f.answers)) for (const [a, n] of Object.entries(m)) answers[c][a] += n;
	}
	const last = fights[fights.length - 1];
	const lastRl = [...fights].reverse().find((f) => f.brain === "rl");
	const player = {
		id: uid, v: 1, nickname, createdAt, lastSeen: last ? last.at : createdAt, gameVersion: last ? last.gameVersion : "1.2.0",
		difficulty: last ? last.difficulty : prefDiff, totals, keepers, answers, expected: lastRl ? lastRl.expected : [],
		resets: 0, resetAt: null,
	};
	if (opts.survey !== false && r() < 0.55 && nFights > 2) {
		const s = () => 1 + Math.min(4, Math.floor(r() * 5));
		player.survey = { feltRead: Math.min(5, s() + 1), fair: s(), difficulty: Math.min(5, s() + 1), fun: Math.min(5, s() + 1), playAgain: s(), noticedAdapting: r() < 0.7, at: new Date(player.lastSeen.getTime() + HOUR) };
	}
	return { player, fights };
}

const clone = (x) => (typeof structuredClone === "function" ? structuredClone(x) : JSON.parse(JSON.stringify(x)));
const wait = (ms) => new Promise((res) => setTimeout(res, ms));

export class DemoBackend {
	constructor() {
		this.mode = "demo";
		const r = mulberry32(0x48574c4b); // "HWLK"
		this.players = new Map();
		this.fights = new Map();
		const you = makePlayer(r, { uid: "demo-you", nickname: "Ashen Wanderer 4821", skill: 0.62, fights: 38, difficulty: "Normal",
			habits: { fast: "parry", heavy: "stepL", feint: "parry", killer: "stepB" }, habitStrength: 0.68, survey: false, lastSeenHoursAgo: 3,
			v2From: 0.3, sessionLen: [4, 7], readability: 0.78, assist: "ring+slowmo" });
		this.add(you);
		for (let i = 0; i < 46; i++) this.add(makePlayer(r));
		this.me = "demo-you";
		this.comment = null;
	}

	add({ player, fights }) {
		this.players.set(player.id, player);
		this.fights.set(player.id, fights);
	}

	async getPlayer(uid) {
		await wait(120);
		const p = this.players.get(uid);
		return p ? clone(p) : null;
	}

	async getFights(uid, n = 20) {
		await wait(80);
		return clone((this.fights.get(uid) || []).slice().sort((a, b) => b.at - a.at).slice(0, n));
	}

	async getPool() {
		await wait(150);
		return { players: clone([...this.players.values()]), capped: false };
	}

	linked() {
		return this.me ? { uid: this.me } : null;
	}

	async link() {
		this.me = "demo-you";
		return this.me;
	}

	unlink() {
		this.me = null;
	}

	async rename(nickname) {
		await wait(200);
		this.players.get(this.me).nickname = nickname;
	}

	async getComment() {
		await wait(60);
		return this.comment;
	}

	async submitSurvey(likert, noticed, comment) {
		await wait(250);
		this.players.get(this.me).survey = { ...likert, noticedAdapting: noticed, at: new Date() };
		this.comment = comment || "";
	}

	async reset(onProgress = () => {}) {
		const all = this.fights.get(this.me) || [];
		for (let k = 0; k < all.length; k += 10) {
			await wait(70);
			onProgress(Math.min(all.length, k + 10));
		}
		this.fights.set(this.me, []);
		const p = this.players.get(this.me);
		p.totals = emptyTotals();
		p.keepers = { warden: { fights: 0, wins: 0 }, sage: { fights: 0, wins: 0 }, returned: { fights: 0, wins: 0 } };
		p.answers = emptyAnswers();
		p.expected = [];
		p.resets = (p.resets || 0) + 1;
		p.resetAt = new Date();
		return all.length;
	}
}
