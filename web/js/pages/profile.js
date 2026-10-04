// HELLWALKER site - #/p/<uid> (anyone's public numbers) and #/me (the linked browser: + nickname, survey, reset).
// Laid out as the keeper's dossier on the walker: the header (rank, facts), the keeper's note (its habit sentence),
// vital signs, how well it knew you (insight per Adaptive AI duel, v2 fights), the record against each keeper, the
// notebook, and the engagement log (filterable by walk).

import { h, raw, $, $$, EMBLEMS, bar, stat, pct, int, dec, duration, date, ago, toast, confirmDialog, lineChart } from "../ui.js";
import {
	KEEPERS, keeperByIdx, skills, rankOf, RANK_PARRY_MIN, expectations, answerMix, habit, CLASSES, ANSWERS, answerLabel,
	walkOf, walkLabel, difficultyLabel, assistOf, keeperDamageOf, insightOf, readOf, insightWords,
} from "../model.js";

export const loadingText = "The keeper leafs through its notebook…";
const FIGHTS = 30; // recent duels read per profile (the chart and the log)

// ---- small formats ---------------------------------------------------------------------------------------------------

const MON = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"];
const two = (n) => String(n).padStart(2, "0");
function stamp(d) {
	if (!(d instanceof Date) || isNaN(d)) return "—";
	return `${d.getDate()} ${MON[d.getMonth()]} ${two(d.getHours())}:${two(d.getMinutes())}`;
}
const dayMark = (d) => (d instanceof Date && !isNaN(d) ? `${d.getDate()} ${MON[d.getMonth()]}` : "");
function mmss(s) {
	if (!isFinite(s) || s <= 0) return "0:00";
	return `${Math.floor(s / 60)}:${two(Math.round(s % 60) % 60)}`;
}
/** A rate as [number, "%"] for a big figure (the % set smaller), or ["—", ""]. */
const pp = (x) => (x === null || x === undefined || !isFinite(x) ? ["—", ""] : [String(Math.round(x * 100)), "%"]);

/** The walker's name with its last word set in ember italics, like a dossier's subject line. */
function subjectName(name) {
	const words = String(name || "Nameless walker").trim().split(/\s+/);
	const last = words.pop();
	return words.length ? h`${words.join(" ")} <em>${last}</em>` : h`<em>${last}</em>`;
}

// ---- sections ----------------------------------------------------------------------------------------------------------

function dossierHead(p, s, rank, fights, { me, isYou }) {
	const next = rank.next;
	let nextLine = h`<p class="next-rank">The highest rank. The keepers speak of you.</p>`;
	if (next) {
		const needW = Math.max(0, next.wins - s.wins);
		const needP = next.parry > 0 && rank.parry < next.parry;
		nextLine = h`<p class="next-rank">Next rank: <b>${next.name}</b> -
			${needW > 0 ? `${needW} more win${needW === 1 ? "" : "s"}` : "wins done"}${next.parry > 0 ? h`, ${needP ? h`a parry rate of ${pct(next.parry)}${rank.parryCounts ? "" : ` (counts after ${RANK_PARRY_MIN} parry attempts)`}` : "parries done"}` : ""}.</p>`;
	}
	const latest = fights[0];
	const assist = latest && typeof latest.assist === "string" ? assistOf(latest) : null;
	const file = String(p.id || "").replace(/[^A-Za-z0-9]/g, "").slice(0, 4).toUpperCase() || "ANON";
	return h`<header class="dossier-head">
		<div>
			<div class="dossier-meta">
				<div>File no.<b>HW—${file}</b></div>
				<div>Walking since<b>${date(p.createdAt)}</b></div>
				${p.lastSeen ? h`<div>Last duel<b>${ago(p.lastSeen)}</b></div>` : ""}
				${p.difficulty ? h`<div>Difficulty<b>${difficultyLabel(p.difficulty)}</b></div>` : ""}
				${assist ? h`<div>Parry assist<b>${assist.label || "off"}</b></div>` : ""}
				${p.gameVersion ? h`<div>Game<b>v${p.gameVersion}</b></div>` : ""}
			</div>
			<h1 class="subject-name">${subjectName(p.nickname)}</h1>
			<div class="facts">
				<span><b>${int(s.fights)}</b> duel${s.fights === 1 ? "" : "s"}</span>
				<span><b>${int(s.wins)}</b> keeper${s.wins === 1 ? "" : "s"} felled</span>
				<span><b>${duration(s.seconds)}</b> fought</span>
				<span class="red"><b>${int(s.reads)}</b> READ${s.reads === 1 ? "" : "s"} suffered</span>
				${p.resets ? h`<span><b>${p.resets}</b> reset${p.resets === 1 ? "" : "s"}</span>` : ""}
			</div>
			${nextLine}
		</div>
		<div class="threat-block">
			<span class="stamp">${me ? "Your file" : isYou ? "This is you" : "Under observation"}</span>
			<div class="threat-grade" aria-hidden="true">${rank.numeral}</div>
			<div class="threat-label">Rank ${rank.numeral}<b>${rank.name}</b></div>
		</div>
	</header>`;
}

function keeperNote(p, s) {
	if (!s.fights) return "";
	const hb = habit(p.answers);
	return h`<blockquote class="assessment ${hb.noticed ? "" : "calm"} reveal">
		<div class="who">Keeper's note · from its notebook</div>
		<p class="body">${hb.text}</p>
	</blockquote>`;
}

function vitals(s) {
	const [win, winU] = pp(s.winRate), [par, parU] = pp(s.parry), [acc, accU] = pp(s.accuracy);
	return h`<section class="block reveal" aria-labelledby="sk-h">
		<header class="block-head"><h2 class="block-title" id="sk-h">Vital signs</h2><span class="block-tag">§ 01 · All duels</span></header>
		<div class="stats">
			${stat("Win rate", win, s.winRate, { unit: winU, accent: true, note: s.fights ? `${int(s.wins)} won · ${int(s.losses)} lost · ${int(s.timeouts)} timed out` : "No duels yet" })}
			${stat("Parry success", par, s.parry, { unit: parU, tone: "gold", note: s.parryN ? `${int(s.parried)} of ${int(s.parryN)} presses caught a swing · assisted parries count` : "No parry attempts yet" })}
			${stat("Hit accuracy", acc, s.accuracy, { unit: accU, note: s.swings ? `${int(s.hits)} of ${int(s.swings)} swings landed` : "No swings yet" })}
			${stat("Dodges per duel", dec(s.dodgesPerFight), s.dodgesPerFight === null ? null : s.dodgesPerFight / 20, { tone: "stone", note: "ghoststeps per duel (bar: 20)" })}
			${stat("Damage per minute", int(s.dpm), s.dpm === null ? null : s.dpm / 2000, { note: s.tpm !== null ? `you take ${int(s.tpm)} per minute (bar: 2,000)` : "" })}
		</div>
	</section>`;
}

/** The insight chart's data: the recent duels against the RL brain, oldest first, broken at every new game session. */
function insightData(fights) {
	const rows = fights.filter((f) => f.brain === "rl" && (insightOf(f) !== null || readOf(f) !== null)).sort((a, b) => a.at - b.at);
	const breaks = [], xLabels = [];
	rows.forEach((f, i) => {
		if (i === 0 || f.session !== rows[i - 1].session) {
			if (i > 0) breaks.push(i);
			xLabels.push({ i, text: dayMark(f.at) });
		}
	});
	return {
		n: rows.length, breaks, xLabels, rows,
		insight: rows.map(insightOf), read: rows.map(readOf),
		titles: rows.map((f) => `${stamp(f.at)}, duel ${f.fightInSession || "?"} of the session`),
	};
}

function knewYou(data) {
	const head = h`<header class="block-head"><h2 class="block-title" id="ky-h">How well it knew you</h2><span class="block-tag">§ 02 · Adaptive AI · per duel</span></header>`;
	if (!data.n) {
		return h`<section class="block warm reveal" aria-labelledby="ky-h">${head}
			<p class="chart-sum">No Adaptive AI duels on file yet.</p>
			<p class="chart-note">Choose Adaptive AI on the title screen. The keeper starts every session not knowing you; its insight - and with it
			its strength - grows as its predictions of your answers come true, and fades when you change.</p></section>`;
	}
	const hasInsight = data.insight.some((v) => v !== null);
	const lastIdx = data.insight.map((v, i) => (v !== null ? i : -1)).filter((i) => i >= 0).pop();
	const last = lastIdx === undefined ? null : data.insight[lastIdx];
	const reads = data.read.filter((v) => v !== null);
	const avgRead = reads.length ? reads.reduce((a, b) => a + b, 0) / reads.length : null;
	const label = `Line chart of the last ${data.n} duels against the learning keeper, oldest first.`
		+ (hasInsight ? ` Insight going into each duel: ${data.insight.map((v) => (v === null ? "not recorded" : Math.round(v * 100) + "%")).join(", ")}.` : "")
		+ (reads.length ? ` Its read per duel: ${data.read.map((v) => (v === null ? "none" : Math.round(v * 100) + "%")).join(", ")}.` : "");
	return h`<section class="block warm reveal" aria-labelledby="ky-h">${head}
		${hasInsight
			? h`<p class="chart-sum"><span class="big">${pct(last)}</span> going into your latest Adaptive AI duel - <b>${insightWords(last)}</b>.
				Its insight starts at zero every time you launch the game.</p>`
			: h`<p class="chart-sum">Its read of you per duel${avgRead !== null ? h` - <b>${pct(avgRead)}</b> on average` : ""}. Insight is recorded from game version 1.4 on.</p>`}
		<div class="chart" id="insight-chart" role="img" aria-label="${label}"></div>
		<div class="chart-legend" aria-hidden="true">
			${hasInsight ? h`<span class="l-insight"><i></i>Insight</span>` : ""}
			${reads.length ? h`<span class="l-read"><i></i>Its read</span>` : ""}
			${data.breaks.length ? h`<span class="l-session"><i></i>New session</span>` : ""}
		</div>
		<p class="chart-note">Insight: how well it knew you going into the duel, from how often its predictions came true - it sets the keeper's
		strength. Its read: the share of your answers it called in that duel.</p>
	</section>`;
}

function recordCard(p, s) {
	return h`<section class="block reveal" aria-labelledby="rec-h">
		<header class="block-head"><h2 class="block-title" id="rec-h">Against the keepers</h2><span class="block-tag">§ 03 · ${s.fights ? `${pct(s.winRate)} overall` : "Record"}</span></header>
		<div class="vs">
			${KEEPERS.map((k) => {
				const r = (p.keepers && p.keepers[k.key]) || { fights: 0, wins: 0 };
				const lost = Math.max(0, (r.fights || 0) - (r.wins || 0));
				return h`<div class="vs-card ${k.key}">
					${raw(EMBLEMS[k.key]())}
					<div class="vs-body">
						<h3>${k.name}</h3>
						<div class="rec"><b>${int(r.wins || 0)}</b> won · <b>${int(lost)}</b> lost</div>
						${bar(r.fights ? r.wins / r.fights : null, k.key === "sage" ? "gold" : k.key === "returned" ? "stone" : "", "thin")}
					</div>
					<div class="vs-pct">${r.fights ? pct(r.wins / r.fights) : "—"}</div>
				</div>`;
			})}
		</div>
	</section>`;
}

function notebook(p, s) {
	const rows = expectations(p.expected);
	const anyExpected = rows.some((r) => r.known);
	const mixes = CLASSES.map(([cls, label]) => ({ cls, label, ...answerMix(p.answers, cls) }));
	const anyAnswers = mixes.some((m) => m.n > 0);
	const head = h`<header class="block-head"><h2 class="block-title" id="nb-h">The keeper's notebook</h2><span class="block-tag">§ 04 · What it wrote down about you</span></header>`;

	if (!anyExpected && !anyAnswers && !s.predictions) {
		return h`<section class="block warm notebook reveal" aria-labelledby="nb-h">${head}
			<p class="habit">The notebook fills as the keepers read you.</p>
			<p class="muted">Fight in Adaptive AI: every exchange is written down - what it threw, how you answered, whether it saw you coming.</p>
		</section>`;
	}
	return h`<section class="block warm notebook reveal" aria-labelledby="nb-h">${head}
		<div class="nb-cols">
			<div>
				<h3 class="nb-h">What it expects you to do next</h3>
				${anyExpected ? rows.map((r) => h`<div class="exp-row ${r.p >= 0.55 ? "sure" : ""}">
					<span class="against">Against ${r.label}</span>
					<span class="ans">${r.answer || "-"}</span>
					${bar(r.p, r.p >= 0.55 ? "" : "dim", "thin")}
					<span class="p">${r.known ? pct(r.p) : ""}</span>
				</div>`) : h`<p class="muted">Nothing yet - the scripted keepers of the Normal walk write nothing down.</p>`}
				${anyExpected ? h`<p class="hint">After your latest duel with the learning keeper. Ember: it is at least 55 % sure - sure enough to READ you.</p>` : ""}
			</div>
			<div>
				<h3 class="nb-h">What you actually did</h3>
				${mixes.map((m) => h`<div class="did">
					<div class="did-head"><span>To its <b>${m.label}</b></span><span class="n">${int(m.n)}</span></div>
					${m.n ? h`<div class="stack" role="img" aria-label="${m.shares.filter((x) => x.n).map((x) => `${answerLabel(x.answer)} ${Math.round(x.share * 100)}%`).join(", ")}">
						${m.shares.filter((x) => x.n).map((x) => h`<i class="a-${x.answer}" data-w="${x.share.toFixed(4)}"></i>`)}</div>
						<div class="did-top">${m.top.map((t) => h`<span><b>${answerLabel(t.answer)}</b> ${pct(t.share)}</span>`)}</div>`
						: h`<div class="did-top"><span>not seen yet</span></div>`}
				</div>`)}
				<div class="legend" aria-hidden="true">${ANSWERS.map((a) => h`<span><i class="a-${a}"></i>${answerLabel(a)}</span>`)}</div>
			</div>
		</div>
		<div class="nb-foot">
			<p>${s.predictions ? h`It called your answer right <b>${pct(s.readAcc)}</b> of the time (${int(s.correct)} of ${int(s.predictions)})${s.confident ? h` - <b>${pct(s.sureAcc)}</b> when it was sure` : ""} - <b>${int(s.reads)}</b> READ${s.reads === 1 ? "" : "s"} landed.` : "It has not predicted you yet."}</p>
		</div>
	</section>`;
}

const RESULT = { win: "Victory", loss: "Defeat", timeout: "Time", quit: "Fled" };

function settingsPills(f) {
	const a = assistOf(f), dmg = keeperDamageOf(f);
	const out = [];
	if (a.label) {
		const tip = a.slowmo !== null && a.slowmo < 1 ? `The parry ring was on, with slow motion at ${Math.round(a.slowmo * 100)}% speed` : "The parry ring was on";
		out.push(h`<span class="pill assist" title="${tip}"><span class="ring-ico" aria-hidden="true"></span>${a.label}</span>`);
	}
	if (dmg !== null) out.push(h`<span class="pill" title="The keeper's hits were scaled to ${Math.round(dmg * 100)}% by the difficulty">keeper dmg ${Math.round(dmg * 100)}%</span>`);
	return out.length ? out : "—";
}

function engagementLog(fights, s) {
	const count = (w) => fights.filter((f) => walkOf(f) === w).length;
	return h`<section class="block reveal" aria-labelledby="log-h">
		<header class="block-head"><h2 class="block-title" id="log-h">Engagement log</h2><span class="block-tag">§ 05 · ${fights.length ? `the last ${fights.length} on file` : "no records"}</span></header>
		${fights.length ? h`
		<div class="log-tools">
			<div class="seg" role="group" aria-label="Show duels of">
				<button type="button" data-walk="all" aria-pressed="true">All · ${fights.length}</button>
				<button type="button" data-walk="normal" aria-pressed="false">Normal · ${count("normal")}</button>
				<button type="button" data-walk="adaptive" aria-pressed="false">Adaptive AI · ${count("adaptive")}</button>
			</div>
		</div>
		<table class="log-table">
			<thead><tr><th scope="col">№</th><th scope="col">Logged</th><th scope="col">Opponent</th><th scope="col">Outcome</th><th scope="col">Time</th>
				<th scope="col">Parried</th><th scope="col">Hits</th><th scope="col">It read you</th><th scope="col">Insight</th><th scope="col">Settings</th></tr></thead>
			<tbody>
				${fights.map((f, i) => {
					const k = keeperByIdx(f.keeper);
					const read = readOf(f), ins = insightOf(f);
					return h`<tr data-walk="${walkOf(f)}">
						<td class="c-no" data-label="№">${String(Math.max(1, s.fights - i)).padStart(3, "0")}</td>
						<td class="c-when" data-label="Logged">${stamp(f.at)}</td>
						<td class="c-opp" data-label="Opponent"><span class="opp">${f.keeperName || (k ? k.name : "A keeper")}</span><span class="opp-sub">${walkLabel(f)} · ${difficultyLabel(f.difficulty)}</span></td>
						<td class="c-res" data-label="Outcome"><span class="chip ${f.result}">${RESULT[f.result] || f.result}</span></td>
						<td class="c-time" data-label="Time">${mmss(f.seconds)}</td>
						<td class="c-parry" data-label="Parried"><b>${int(f.keeperParried)}</b>/${int(f.parryAttempts)}</td>
						<td class="c-hits" data-label="Hits"><b>${int(f.playerHits)}</b>/${int(f.playerSwings)}</td>
						<td class="c-read" data-label="It read you">${read === null ? "—" : pct(read)}${f.readsLanded ? h` <span class="pill adaptive">${int(f.readsLanded)} READ${f.readsLanded === 1 ? "" : "s"}</span>` : ""}</td>
						<td class="c-ins" data-label="Insight">${ins === null ? "—" : pct(ins)}</td>
						<td class="c-set" data-label="Settings">${settingsPills(f)}</td>
					</tr>`;
				})}
				<tr class="log-empty" hidden><td colspan="10">No duels of this walk among the recent ones.</td></tr>
			</tbody>
		</table>` : h`<p class="empty-line muted">No duels recorded yet.</p>`}
	</section>`;
}

// ---- the linked browser's extras ---------------------------------------------------------------------------------------

const LIKERT = [
	["feltRead", "I felt the keeper was reading me.", "Strongly disagree", "Strongly agree"],
	["fair", "The fights felt fair.", "Strongly disagree", "Strongly agree"],
	["difficulty", "How hard were the keepers?", "Far too easy", "Far too hard"],
	["fun", "I had fun.", "Strongly disagree", "Strongly agree"],
	["playAgain", "I would play again.", "Strongly disagree", "Strongly agree"],
];

function meCards(p, comment, mode) {
	const sv = p.survey || {};
	const shareUrl = `${location.origin}${location.pathname}${location.search}#/p/${encodeURIComponent(p.id)}`;
	return h`
	<div class="me-cols gap">
		<div class="col">
		<section class="block reveal" aria-labelledby="name-h">
			<header class="block-head"><h2 class="block-title" id="name-h">Your name</h2><span class="block-tag">§ 06 · Public</span></header>
			<form id="name-form" novalidate>
				<label for="nick">Shown on the leaderboard</label>
				<div class="field-row">
					<input type="text" id="nick" name="nick" maxlength="24" autocomplete="off" spellcheck="false" value="${p.nickname || ""}" aria-describedby="nick-hint">
					<button class="btn small" type="submit">Save</button>
				</div>
				<p class="hint" id="nick-hint">1 to 24 characters. No real names, please - it is public.</p>
			</form>
			<div class="field">
				<label for="share">Your public page</label>
				<div class="field-row"><input type="text" id="share" readonly value="${shareUrl}"><button class="btn ghost small" type="button" id="copy">Copy</button></div>
				<p class="hint">Anyone with this link sees your numbers (never who you are).</p>
			</div>
			<p class="hint">This browser is linked to your game${mode === "demo" ? " (demo)" : ""}. <button type="button" class="linklike" id="unlink">Unlink this browser</button></p>
		</section>
		<section class="block danger-zone reveal" aria-labelledby="reset-h">
			<header class="block-head"><h2 class="block-title" id="reset-h">Forget me</h2><span class="block-tag">§ 08 · Reset</span></header>
			<p>Erase everything recorded about your play:</p>
			<ul>
				<li>every duel you uploaded is deleted;</li>
				<li>your totals, your record against each keeper and the keeper's notebook go back to zero;</li>
				<li>your name, the date you started and your survey answers stay.</li>
			</ul>
			<p class="muted small">The game keeps uploading new duels afterwards. The keepers' memory of you inside the game - and their insight - is dropped whenever you quit it, as always.</p>
			<button class="btn danger" type="button" id="reset">Reset: forget everything</button>
			<p class="progress-line" id="reset-progress" aria-live="polite"></p>
		</section>
		</div>

		<section class="block reveal" aria-labelledby="sv-h">
			<header class="block-head"><h2 class="block-title" id="sv-h">How did it feel?</h2><span class="block-tag">§ 07 · ${sv.at ? `answered ${ago(sv.at)}` : "Survey"}</span></header>
			<form id="survey-form" novalidate>
				${LIKERT.map(([key, q, lo, hi]) => h`<fieldset>
					<legend>${q}</legend>
					<div class="likert"><span class="end">${lo}</span><span class="opts">
						${[1, 2, 3, 4, 5].map((v) => h`<input type="radio" id="${key}-${v}" name="${key}" value="${v}" aria-label="${v} of 5${v === 1 ? ", " + lo : v === 5 ? ", " + hi : ""}" ${sv[key] === v ? h`checked` : ""}><label for="${key}-${v}">${v}</label>`)}
					</span><span class="end">${hi}</span></div>
				</fieldset>`)}
				<fieldset>
					<legend>Did you notice the keeper adapting to you?</legend>
					<div class="yesno">
						<input type="radio" id="na-yes" name="noticedAdapting" value="yes" ${sv.noticedAdapting === true ? h`checked` : ""}><label for="na-yes">Yes</label>
						<input type="radio" id="na-no" name="noticedAdapting" value="no" ${sv.noticedAdapting === false ? h`checked` : ""}><label for="na-no">No</label>
					</div>
				</fieldset>
				<div class="field">
					<label for="comment">Anything else? (optional, private)</label>
					<textarea id="comment" name="comment" maxlength="500" aria-describedby="comment-hint">${comment || ""}</textarea>
					<p class="hint" id="comment-hint"><span id="comment-count">${(comment || "").length}</span> / 500 · only the researcher reads this; the answers above are public and anonymous.</p>
				</div>
				<p class="hint err" id="survey-err" hidden></p>
				<button class="btn" type="submit">${sv.at ? "Update my answers" : "Send my answers"}</button>
			</form>
		</section>
	</div>`;
}

function validNick(v) {
	const t = v.trim();
	if (t.length < 1 || t.length > 24) return "1 to 24 characters.";
	if (/[\u0000-\u001f\u007f]/.test(t)) return "No control characters.";
	return null;
}

function mountMe(root, ctx, p) {
	const { backend } = ctx;
	const nameForm = $("#name-form", root);
	nameForm.addEventListener("submit", async (e) => {
		e.preventDefault();
		const input = $("#nick", root);
		const hint = $("#nick-hint", root);
		const err = validNick(input.value);
		hint.classList.toggle("err", !!err);
		if (err) {
			hint.textContent = err;
			input.focus();
			return;
		}
		const btn = $("button", nameForm);
		btn.disabled = true;
		try {
			await backend.rename(input.value.trim());
			toast("Your name is on file.");
			ctx.rerender();
		} catch (ex) {
			hint.textContent = ex.message;
			hint.classList.add("err");
			btn.disabled = false;
		}
	});

	$("#copy", root).addEventListener("click", async () => {
		const input = $("#share", root);
		try {
			await navigator.clipboard.writeText(input.value);
			toast("Link copied.");
		} catch {
			input.select();
			toast("Select and copy the link above.");
		}
	});

	$("#unlink", root).addEventListener("click", async () => {
		const ok = await confirmDialog({ title: "Unlink this browser?", confirm: "Unlink", body: h`<p>Your numbers stay as they are. To link again, open the stats page from the game's pause menu.</p>` });
		if (!ok) return;
		backend.unlink();
		toast("This browser is no longer linked.");
		ctx.rerender();
	});

	const comment = $("#comment", root);
	comment.addEventListener("input", () => ($("#comment-count", root).textContent = comment.value.length));
	const sForm = $("#survey-form", root);
	sForm.addEventListener("submit", async (e) => {
		e.preventDefault();
		const errEl = $("#survey-err", root);
		const data = new FormData(sForm);
		const likert = {};
		const missing = [];
		for (const [key, q] of LIKERT) {
			const v = parseInt(data.get(key), 10);
			if (v >= 1 && v <= 5) likert[key] = v;
			else missing.push(q);
		}
		const na = data.get("noticedAdapting");
		if (!na) missing.push("Did you notice the keeper adapting to you?");
		if (missing.length) {
			errEl.textContent = "Please answer: " + missing.join(" · ");
			errEl.hidden = false;
			return;
		}
		errEl.hidden = true;
		const btn = $('button[type="submit"]', sForm);
		btn.disabled = true;
		try {
			await backend.submitSurvey(likert, na === "yes", comment.value.slice(0, 500));
			toast("Thank you. Your answers are in the notebook.");
			ctx.rerender();
		} catch (ex) {
			errEl.textContent = ex.message;
			errEl.hidden = false;
			btn.disabled = false;
		}
	});

	$("#reset", root).addEventListener("click", async () => {
		const n = (p.totals && p.totals.fights) || 0;
		const ok = await confirmDialog({
			title: "Forget everything?",
			danger: true,
			confirm: "Yes, forget me",
			cancel: "Keep my record",
			body: h`<p>${n ? `All ${n} recorded duel${n === 1 ? "" : "s"}` : "Every recorded duel"}, your totals, your record against the keepers and the notebook will be erased. This cannot be undone.</p><p class="muted small">Your name and survey answers stay.</p>`,
		});
		if (!ok) return;
		const btn = $("#reset", root);
		const prog = $("#reset-progress", root);
		btn.disabled = true;
		prog.textContent = "The keeper tears out the pages…";
		try {
			const deleted = await backend.reset((k) => (prog.textContent = `${k} duel${k === 1 ? "" : "s"} erased…`));
			toast(`Forgotten: ${deleted} duel${deleted === 1 ? "" : "s"} erased, totals at zero.`);
			ctx.rerender();
		} catch (ex) {
			prog.textContent = "The reset stopped: " + ex.message + " - press the button again to finish it.";
			btn.disabled = false;
		}
	});
}

/** The chart (drawn at the container's width, redrawn when it changes) and the log's walk filter. */
function mountPublic(root, ctx, data) {
	const chartEl = $("#insight-chart", root);
	if (chartEl && data.n) {
		const draw = () => lineChart(chartEl, {
			n: data.n, breaks: data.breaks, xLabels: data.xLabels, titles: data.titles,
			series: [
				{ cls: "read", label: "its read", values: data.read, dots: false },
				{ cls: "insight", label: "insight", values: data.insight, dots: true, area: true },
			],
		});
		draw();
		let lastW = chartEl.clientWidth, raf = 0;
		const onResize = () => {
			cancelAnimationFrame(raf);
			raf = requestAnimationFrame(() => {
				if (Math.abs(chartEl.clientWidth - lastW) < 2) return;
				lastW = chartEl.clientWidth;
				draw();
			});
		};
		if ("ResizeObserver" in window) {
			const ro = new ResizeObserver(onResize);
			ro.observe(chartEl);
			ctx.onLeave(() => ro.disconnect());
		} else {
			window.addEventListener("resize", onResize);
			ctx.onLeave(() => window.removeEventListener("resize", onResize));
		}
	}
	const seg = $(".seg", root);
	if (seg) {
		const buttons = $$("button", seg);
		const rows = $$(".log-table tbody tr[data-walk]", root);
		const empty = $(".log-empty", root);
		for (const b of buttons) {
			b.addEventListener("click", () => {
				const w = b.dataset.walk;
				for (const x of buttons) x.setAttribute("aria-pressed", x === b ? "true" : "false");
				let shown = 0;
				for (const r of rows) {
					const on = w === "all" || r.dataset.walk === w;
					r.hidden = !on;
					if (on) shown++;
				}
				if (empty) empty.hidden = shown > 0;
			});
		}
	}
}

// ---- views ---------------------------------------------------------------------------------------------------------------

function notFound(uid) {
	return {
		title: "No such walker",
		file: "File HW-404 · no such walker",
		html: h`<div class="wrap page"><div class="lost">
			<p class="kicker ember">§ No record</p>
			<h1 class="subject-name">No walker by that <em>mark</em></h1>
			<p class="lost-text">Nobody with the id <code>${uid}</code> has walked the valley - or the link is mistyped.</p>
			<div class="btn-row"><a class="btn ghost" href="#/leaderboard">See the leaderboard</a></div></div></div>`,
	};
}

function notLinked(message) {
	return {
		title: "My stats",
		file: "File HW-ME · not linked",
		html: h`<div class="wrap page">
			<header class="dossier-head">
				<div>
					<div class="dossier-meta"><div>File no.<b>HW—ME</b></div><div>Accounts<b>None</b></div><div>Logins<b>None</b></div></div>
					<h1 class="subject-name">Find <em>yourself</em></h1>
					<p class="dossier-lede">There are no accounts and no logins. Your game links this browser to its record.</p>
				</div>
				<div class="threat-block"><span class="stamp verdigris">Not linked</span></div>
			</header>
			${message ? h`<p class="note red">${message}</p>` : ""}
			<div class="cols">
				<section class="block span-7 reveal" aria-labelledby="ln-h"><header class="block-head"><h2 class="block-title" id="ln-h">Link this browser</h2><span class="block-tag">§ 01 · From the game</span></header>
					<ol class="steps">
						<li><span>Start <b>HELLWALKER</b> and fight at least one duel.</span></li>
						<li><span>Press <b>Esc</b> (or Start) and choose <b>Open my stats page</b>.</span></li>
						<li><span>Your browser opens this page, linked: your numbers, how well the keeper knew you, its notebook, the survey and the reset.</span></li>
					</ol>
				</section>
				<section class="block span-5 reveal" aria-labelledby="ng-h"><header class="block-head"><h2 class="block-title" id="ng-h">No game here?</h2><span class="block-tag">§ 02</span></header>
					<p>Each install has its own anonymous record. Link the browser from the PC you play on, or look anyone up on the leaderboard.</p>
					<div class="btn-row"><a class="btn ghost small" href="#/leaderboard">Leaderboard</a><a class="btn ghost small" href="#/download">Download</a></div>
				</section>
			</div></div>`,
	};
}

export async function view(ctx) {
	const { backend } = ctx;
	let linkError = null;
	if (ctx.me && ctx.linkToken) {
		try {
			await backend.link(ctx.linkToken);
			toast("This browser is now linked to your game.");
		} catch (e) {
			linkError = e.http === 400 ? "That link from the game did not work (it may be from another install). Open the stats page from the game again." : e.message;
		}
	}
	const link = backend.linked();
	const uid = ctx.me ? link && link.uid : ctx.uid;
	if (ctx.me && !uid) return notLinked(linkError);

	const [p, fights] = await Promise.all([backend.getPlayer(uid), backend.getFights(uid, FIGHTS)]);
	if (!p) {
		if (ctx.me) {
			return {
				title: "My stats",
				file: "File HW-ME · empty",
				html: h`<div class="wrap page"><div class="lost">
					<p class="kicker ember">§ Linked · nothing on file</p>
					<h1 class="subject-name">Not written down <em>yet</em></h1>
					<p class="lost-text">This browser is linked, but the game has not sent a duel yet. Fight one, then come back.</p></div></div>`,
			};
		}
		return notFound(uid);
	}
	let comment = null;
	if (ctx.me) {
		try {
			comment = await backend.getComment();
		} catch {
			comment = null; // the private comment is a nicety; the page works without it
		}
	}
	const s = skills(p);
	const rank = rankOf(p);
	const isYou = !!(link && link.uid === p.id);
	const data = insightData(fights);
	const file = String(p.id || "").replace(/[^A-Za-z0-9]/g, "").slice(0, 4).toUpperCase() || "ANON";

	const html = h`<div class="wrap page profile">
		${linkError ? h`<p class="note red">${linkError}</p>` : ""}
		${dossierHead(p, s, rank, fights, { me: ctx.me, isYou })}
		${keeperNote(p, s)}
		<div class="cols">
			<div class="span-4">${vitals(s)}</div>
			<div class="span-8 stack-col">${knewYou(data)}${recordCard(p, s)}</div>
			<div class="span-12">${notebook(p, s)}</div>
			<div class="span-12">${engagementLog(fights, s)}</div>
		</div>
		${ctx.me ? meCards(p, comment, backend.mode) : isYou ? h`<p class="lb-note">This is you. <a href="#/me">Open your stats page</a> to rename, answer the survey or reset.</p>` : ""}
	</div>`;

	return {
		title: ctx.me ? "My stats" : p.nickname || "Walker",
		file: `File HW-${file} · dossier`,
		html,
		mount: (root) => {
			mountPublic(root, ctx, data);
			if (ctx.me) mountMe(root, ctx, p);
		},
	};
}
