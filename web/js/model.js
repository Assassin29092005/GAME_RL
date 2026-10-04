// HELLWALKER site - what the numbers mean: the keepers, the move and answer names (worded as the game's notebook
// words them), derived skills (web/CONTRACT.md fields -> rates), ranks, leaderboards and global stats.

export const KEEPERS = [
	{ key: "warden", idx: 0, name: "The Ninefold Warden", short: "the Warden", style: "Pressure",
		look: "The scythe-bearer of the first shrine.",
		blurb: "Heavies into your guard until it breaks. It closes the distance and keeps swinging - blocking only buys you time.",
		evalStat: ["7.1", "guard breaks / min"] },
	{ key: "sage", idx: 1, name: "The Monkey Sage", short: "the Sage", style: "Baits",
		look: "The staff-dancer of the second shrine.",
		blurb: "Feints, evasions and the swing that never comes. It waits for your panic parry, then punishes the empty air.",
		evalStat: ["14.6", "feint bites / min"] },
	{ key: "returned", idx: 2, name: "The Warden, Returned", short: "the Returned", style: "The reader",
		look: "Reborn in stone, behind the sealed gate.",
		blurb: "Fast, exact counters. Sealed until the other two fall; in Adaptive AI it carries everything they learned about you.",
		evalStat: ["53.2", "READs / min"] },
];

export const keeperByIdx = (i) => KEEPERS[i] || null;

// The eight attacks of the notebook's left page, in the game's order and wording (HWHUD.cpp DrawNotebookPage).
export const ATTACKS = [
	["fastslash", "a fast slash"], ["sweepleft", "a sweep to your left"], ["sweepright", "a sweep to your right"],
	["heavycleave", "a heavy cleave"], ["delayedheavy", "a delayed heavy"], ["feint", "a feint"], ["grab", "a grab"],
	["killer", "the killer thrust"],
];
const ATTACK_ALIASES = { fast: "fastslash", bfastslash: "fastslash", bsweepleft: "sweepleft", bsweepright: "sweepright",
	bheavycleave: "heavycleave", bdelayedheavy: "delayedheavy", feintmid: "feint", bfeintmid: "feint", feintearly: "feint",
	feintlate: "feint", bgrab: "grab", killerthrust: "killer", bkillerthrust: "killer" };
const norm = (s) => String(s || "").toLowerCase().replace(/[^a-z]/g, "");
export function attackKey(s) {
	const k = norm(s);
	return ATTACK_ALIASES[k] || k;
}
export function attackLabel(s) {
	const k = attackKey(s);
	const hit = ATTACKS.find((a) => a[0] === k);
	return hit ? hit[1] : String(s || "?");
}

// What the player did: the contract's answer keys, and the read head's player symbols (HumanSym in HWHUD.cpp).
export const ANSWERS = ["parry", "block", "stepL", "stepR", "stepB", "stepF", "attack", "none"];
const ANSWER_LABELS = {
	parry: "Parry", block: "Block", stepl: "Ghoststep left", stepr: "Ghoststep right", stepb: "Ghoststep back",
	stepf: "Ghoststep in", attack: "Swing back", none: "Do nothing", neutral: "Stand your ground", advance: "Walk in",
	retreat: "Back off", light: "Light attack", heavy: "Heavy attack", switch: "Weapon switch",
};
export const answerLabel = (s) => ANSWER_LABELS[norm(s)] || String(s || "?");

export const CLASSES = [["fast", "fast swings"], ["heavy", "heavy swings"], ["feint", "feints"], ["killer", "killer thrusts"]];

// ---- the two walks, difficulty, and the v2 fight fields (web/CONTRACT.md) ---------------------------------------------

/** The game's two walks. Telemetry keeps the old names: playMode "pathbreaker" = Normal, "hellwalker" = Adaptive AI. */
export const WALKS = [
	{ key: "normal", label: "Normal", line: "The scripted keepers - every one of them. Patterns you can learn." },
	{ key: "adaptive", label: "Adaptive AI", line: "The keepers driven by the RL brain. They learn you." },
];

/** "normal" | "adaptive". The retired "66days" walk was an Adaptive AI walk; an arena duel goes by its brain. */
export function walkOf(f) {
	const m = f && f.playMode;
	if (m === "pathbreaker") return "normal";
	if (m === "hellwalker" || m === "66days") return "adaptive";
	return f && f.brain === "rl" ? "adaptive" : "normal";
}

export function walkLabel(f) {
	const w = walkOf(f) === "adaptive" ? "Adaptive AI" : "Normal";
	return f && f.playMode === "arena" ? w + " · arena" : w;
}

/** The difficulties the game offers now; "Adaptive" was retired (old saves load as Normal). */
export const DIFFICULTIES = ["Easy", "Normal", "Hard", "Hellwalker"];
export const DIFFICULTY_ORDER = [...DIFFICULTIES, "Adaptive"];
export const difficultyLabel = (d) => (d === "Adaptive" ? "Adaptive (old)" : d ? String(d) : "—");

/** The parry assist of a fight. v1 fights had none (no field = "off"). */
export function assistOf(f) {
	const a = f && typeof f.assist === "string" ? f.assist : "off";
	if (a === "ring+slowmo") {
		const s = f && typeof f.slowmoScale === "number" && isFinite(f.slowmoScale) ? f.slowmoScale : null;
		return { key: a, label: "ring + slow-mo", slowmo: s };
	}
	if (a === "ring") return { key: a, label: "ring", slowmo: null };
	return { key: "off", label: null, slowmo: null };
}

const num01 = (x, hi = 1) => (typeof x === "number" && isFinite(x) ? Math.max(0, Math.min(hi, x)) : null);

/** The scale on the keeper's hits this fight (v2; null when the fight did not record it). */
export const keeperDamageOf = (f) => (f ? num01(f.keeperDamageScale, 2) : null);

/** How well the Adaptive AI keeper knew the player going into this fight, 0..1 (v2 RL fights only; else null). */
export const insightOf = (f) => (f && f.brain === "rl" ? num01(f.insight) : null);

/** The read accuracy of one fight (null without predictions). */
export const readOf = (f) => (f && f.predictions > 0 ? Math.max(0, Math.min(1, (f.predictionsCorrect || 0) / f.predictions)) : null);

/** A few words for an insight value. */
export function insightWords(x) {
	if (x === null || x === undefined) return "";
	if (x < 0.2) return "a stranger to it";
	if (x < 0.45) return "it is getting to know you";
	if (x < 0.7) return "it reads you";
	return "it knows you";
}

// ---- derived skills ---------------------------------------------------------------------------------------------------

const ratio = (a, b) => (b > 0 ? Math.max(0, Math.min(1, (a || 0) / b)) : null);

/** Rates from players/{uid}.totals (null when there is nothing to divide by). */
export function skills(p) {
	const t = (p && p.totals) || {};
	const fights = t.fights || 0;
	return {
		fights,
		wins: t.wins || 0,
		losses: t.losses || 0,
		timeouts: t.timeouts || 0,
		seconds: t.seconds || 0,
		parry: ratio(t.keeperParried, t.parryAttempts),
		parryN: t.parryAttempts || 0,
		parried: t.keeperParried || 0,
		winRate: ratio(t.wins, fights),
		accuracy: ratio(t.playerHits, t.playerSwings),
		swings: t.playerSwings || 0,
		hits: t.playerHits || 0,
		dodgesPerFight: fights > 0 ? (t.dodges || 0) / fights : null,
		dpm: t.seconds > 0 ? (t.dmgDealt || 0) / (t.seconds / 60) : null,
		tpm: t.seconds > 0 ? (t.dmgTaken || 0) / (t.seconds / 60) : null,
		readAcc: ratio(t.predictionsCorrect, t.predictions),
		predictions: t.predictions || 0,
		correct: t.predictionsCorrect || 0,
		sureAcc: ratio(t.confidentCorrect, t.confident),
		confident: t.confident || 0,
		reads: t.readsLanded || 0,
		guarded: ratio((t.keeperBlocked || 0) + (t.keeperParried || 0) + (t.keeperWhiffed || 0), t.keeperSwings),
	};
}

// ---- ranks (wins + parry rate) ------------------------------------------------------------------------------------------

/** A parry rate counts toward a rank after this many parry attempts. */
export const RANK_PARRY_MIN = 20;
export const RANKS = [
	{ tier: 1, numeral: "I", name: "Ashborn", wins: 0, parry: 0 },
	{ tier: 2, numeral: "II", name: "Wanderer", wins: 1, parry: 0 },
	{ tier: 3, numeral: "III", name: "Bellringer", wins: 3, parry: 0.25 },
	{ tier: 4, numeral: "IV", name: "Shrinebreaker", wins: 8, parry: 0.35 },
	{ tier: 5, numeral: "V", name: "Keeperbane", wins: 15, parry: 0.45 },
	{ tier: 6, numeral: "VI", name: "Hellwalker", wins: 30, parry: 0.55 },
];

export function rankOf(p) {
	const s = skills(p);
	const parry = s.parryN >= RANK_PARRY_MIN ? s.parry || 0 : 0;
	let r = RANKS[0];
	for (const k of RANKS) if (s.wins >= k.wins && parry >= k.parry) r = k;
	const next = RANKS[r.tier] || null;
	return { ...r, next, parry, parryCounts: s.parryN >= RANK_PARRY_MIN };
}

// ---- the notebook ------------------------------------------------------------------------------------------------------

/** The keeper's expectations in the notebook's order: [{key, label, answer, p, known}] (8 rows + any extras). */
export function expectations(list) {
	const byKey = new Map();
	for (const e of Array.isArray(list) ? list : []) if (e && e.attack) byKey.set(attackKey(e.attack), e);
	const rows = ATTACKS.map(([key, label]) => {
		const e = byKey.get(key);
		byKey.delete(key);
		return { key, label, answer: e ? answerLabel(e.answer) : null, p: e && isFinite(e.p) ? e.p : null, known: !!e };
	});
	for (const [key, e] of byKey) rows.push({ key, label: attackLabel(e.attack), answer: answerLabel(e.answer), p: e.p, known: true });
	return rows;
}

/** What the player did against one class: {n, shares: [{answer, n, share}], top: [...2]}. */
export function answerMix(answers, cls) {
	const m = (answers && answers[cls]) || {};
	const n = ANSWERS.reduce((a, k) => a + (m[k] || 0), 0);
	const shares = ANSWERS.map((k) => ({ answer: k, n: m[k] || 0, share: n > 0 ? (m[k] || 0) / n : 0 }));
	const top = shares.filter((x) => x.n > 0).sort((a, b) => b.n - a.n).slice(0, 2);
	return { n, shares, top };
}

/** The habit the keeper leans on - the game's own sentence (DrawNotebookPage): needs 8 answers and a 55 % share. */
export function habit(answers) {
	let best = null;
	for (const [cls, label] of CLASSES) {
		const mix = answerMix(answers, cls);
		const top = mix.top[0];
		if (top && mix.n >= 8 && (!best || top.share > best.share)) best = { cls, label, answer: top.answer, share: top.share, n: mix.n };
	}
	if (best && best.share >= 0.55) {
		return { noticed: true, text: `Against its ${best.label} you ${answerLabel(best.answer).toLowerCase()} ${Math.round(best.share * 100)}% of the time (${best.n} times). It has noticed.` };
	}
	return { noticed: false, text: "No habit stands out yet. Keep it that way." };
}

// ---- leaderboards and the world ----------------------------------------------------------------------------------------

export const BOARD_MIN = { parry: 20, read: 50 };

export const BOARDS = [
	{ id: "wins", tab: "Most wins", title: "Most keepers felled", col: "Wins",
		rows: (ps) => ps.filter((p) => skills(p).wins > 0).sort((a, b) => skills(b).wins - skills(a).wins || (skills(b).winRate || 0) - (skills(a).winRate || 0)),
		value: (s) => s.wins.toLocaleString("en-US"), detail: (s) => `${s.fights} fights · ${Math.round((s.winRate || 0) * 100)}% won` },
	{ id: "parry", tab: "Best parry rate", title: "Cleanest parries", col: "Parried",
		note: `At least ${BOARD_MIN.parry} parry attempts. Parry rate = keeper swings parried / parry presses. Parries made with the parry assist (the red ring, with or without slow motion) count too.`,
		rows: (ps) => ps.filter((p) => skills(p).parryN >= BOARD_MIN.parry).sort((a, b) => skills(b).parry - skills(a).parry || skills(b).parryN - skills(a).parryN),
		value: (s) => Math.round(s.parry * 100) + "%", detail: (s) => `${s.parried} of ${s.parryN} parries` },
	{ id: "read", tab: "Hardest to read", title: "The unreadable", col: "Read",
		note: `At least ${BOARD_MIN.read} of the keeper's predictions. Lower = the keeper called your answer less often.`,
		rows: (ps) => ps.filter((p) => skills(p).predictions >= BOARD_MIN.read).sort((a, b) => skills(a).readAcc - skills(b).readAcc || skills(b).predictions - skills(a).predictions),
		value: (s) => Math.round(s.readAcc * 100) + "%", detail: (s) => `called ${s.correct} of ${s.predictions}` },
	{ id: "fights", tab: "Most fights", title: "The most stubborn", col: "Fights",
		rows: (ps) => ps.filter((p) => skills(p).fights > 0).sort((a, b) => skills(b).fights - skills(a).fights || skills(b).wins - skills(a).wins),
		value: (s) => s.fights.toLocaleString("en-US"), detail: (s) => `${s.wins} wins · ${Math.round(s.seconds / 60)} min fought` },
];

/** Sums over every player in the pool (home page). */
export function worldStats(players) {
	const w = { walkers: 0, fights: 0, wins: 0, parried: 0, parryN: 0, predictions: 0, correct: 0, reads: 0, seconds: 0,
		keepers: { warden: { fights: 0, wins: 0 }, sage: { fights: 0, wins: 0 }, returned: { fights: 0, wins: 0 } } };
	for (const p of players) {
		const t = p.totals || {};
		if ((t.fights || 0) > 0) w.walkers++;
		w.fights += t.fights || 0;
		w.wins += t.wins || 0;
		w.parried += t.keeperParried || 0;
		w.parryN += t.parryAttempts || 0;
		w.predictions += t.predictions || 0;
		w.correct += t.predictionsCorrect || 0;
		w.reads += t.readsLanded || 0;
		w.seconds += t.seconds || 0;
		for (const k of KEEPERS) {
			const r = (p.keepers && p.keepers[k.key]) || {};
			w.keepers[k.key].fights += r.fights || 0;
			w.keepers[k.key].wins += r.wins || 0;
		}
	}
	w.parryRate = ratio(w.parried, w.parryN);
	w.readAcc = ratio(w.correct, w.predictions);
	w.winRate = ratio(w.wins, w.fights);
	return w;
}
