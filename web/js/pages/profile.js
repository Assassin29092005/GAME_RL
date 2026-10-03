// HELLWALKER site - #/p/<uid> (anyone's public numbers) and #/me (the linked browser: + nickname, survey, reset).

import { h, raw, $, $$, EMBLEMS, bar, stat, rankBadge, pct, int, dec, duration, date, ago, toast, confirmDialog } from "../ui.js";
import { KEEPERS, keeperByIdx, skills, rankOf, RANK_PARRY_MIN, expectations, answerMix, habit, CLASSES, ANSWERS, answerLabel } from "../model.js";

export const loadingText = "The keeper leafs through its notebook…";

// ---- sections ------------------------------------------------------------------------------------------------------

function hero(p, s, rank, { me, isYou }) {
	const next = rank.next;
	let nextLine = h`<p class="next-rank">The highest rank. The keepers speak of you.</p>`;
	if (next) {
		const needW = Math.max(0, next.wins - s.wins);
		const needP = next.parry > 0 && rank.parry < next.parry;
		nextLine = h`<p class="next-rank">Next: <b>${next.name}</b> -
			${needW > 0 ? `${needW} more win${needW === 1 ? "" : "s"}` : "wins done"}${next.parry > 0 ? h`, ${needP ? h`a parry rate of ${pct(next.parry)}${rank.parryCounts ? "" : ` (counts after ${RANK_PARRY_MIN} parry attempts)`}` : "parries done"}` : ""}.</p>`;
	}
	return h`<section class="card prof-hero reveal">
		${rankBadge(rank, "lg")}
		<div class="who">
			<p class="kicker left">${me ? "Your record" : "Walker"}${isYou && !me ? h` <span class="pill">you</span>` : ""}</p>
			<h1 class="carved">${p.nickname || "Nameless walker"}</h1>
			<p class="meta">Walking since ${date(p.createdAt)}${p.lastSeen ? h` · last duel ${ago(p.lastSeen)}` : ""}${p.gameVersion ? h` · v${p.gameVersion}` : ""}${p.difficulty ? h` · ${p.difficulty}` : ""}</p>
			<div class="chips">
				<span class="chip"><b>${int(s.fights)}</b> duel${s.fights === 1 ? "" : "s"}</span>
				<span class="chip"><b>${int(s.wins)}</b> keeper${s.wins === 1 ? "" : "s"} felled</span>
				<span class="chip"><b>${duration(s.seconds)}</b> fought</span>
				<span class="chip red"><b>${int(s.reads)}</b> READ${s.reads === 1 ? "" : "s"} suffered</span>
				${p.resets ? h`<span class="chip"><b>${p.resets}</b> reset${p.resets === 1 ? "" : "s"}</span>` : ""}
			</div>
			${nextLine}
		</div>
	</section>`;
}

function skillCard(s) {
	return h`<section class="card reveal" aria-labelledby="sk-h">
		<div class="card-title"><h2 id="sk-h">Skills</h2><span class="muted">all duels</span></div>
		${stat("Parry success", pct(s.parry), s.parry, { tone: "gold", note: s.parryN ? `${int(s.parried)} of ${int(s.parryN)} parry presses caught a swing` : "No parry attempts yet" })}
		${stat("Win rate", pct(s.winRate), s.winRate, { note: s.fights ? `${int(s.wins)} won · ${int(s.losses)} lost · ${int(s.timeouts)} timed out · ${int(s.fights)} duels` : "No duels yet" })}
		${stat("Hit accuracy", pct(s.accuracy), s.accuracy, { note: s.swings ? `${int(s.hits)} of ${int(s.swings)} swings landed` : "No swings yet" })}
		${stat("Dodges per fight", dec(s.dodgesPerFight), s.dodgesPerFight === null ? null : s.dodgesPerFight / 20, { tone: "stone", note: "ghoststeps per duel (bar: 20)" })}
		${stat("Damage per minute", int(s.dpm), s.dpm === null ? null : s.dpm / 2000, { note: s.tpm !== null ? `you take ${int(s.tpm)} per minute (bar: 2,000)` : "" })}
	</section>`;
}

function recordCard(p, s) {
	return h`<section class="card reveal" aria-labelledby="rec-h">
		<div class="card-title"><h2 id="rec-h">Against the keepers</h2><span class="muted">${s.fights ? `${pct(s.winRate)} overall` : ""}</span></div>
		<div class="vs">
			${KEEPERS.map((k) => {
				const r = (p.keepers && p.keepers[k.key]) || { fights: 0, wins: 0 };
				const lost = Math.max(0, (r.fights || 0) - (r.wins || 0));
				return h`<div class="card vs-card ${k.key}">
					${raw(EMBLEMS[k.key]())}
					<div class="vs-body">
						<h3>${k.name}</h3>
						<div class="rec">${int(r.wins || 0)} <small>won</small> · ${int(lost)} <small>lost</small></div>
						${bar(r.fights ? r.wins / r.fights : null, k.key === "sage" ? "gold" : k.key === "returned" ? "stone" : "", "thin")}
					</div>
					<div class="vs-pct">${r.fights ? pct(r.wins / r.fights) : "—"}</div>
				</div>`;
			})}
		</div>
	</section>`;
}

function notebook(p, s, fights) {
	const rows = expectations(p.expected);
	const anyExpected = rows.some((r) => r.known);
	const mixes = CLASSES.map(([cls, label]) => ({ cls, label, ...answerMix(p.answers, cls) }));
	const anyAnswers = mixes.some((m) => m.n > 0);
	const hb = habit(p.answers);
	const perFight = fights.filter((f) => f.predictions > 0).slice(0, 16).reverse();

	if (!anyExpected && !anyAnswers && !s.predictions) {
		return h`<section class="card notebook reveal" aria-labelledby="nb-h">
			<div class="nb-title"><h2 id="nb-h">The Keeper's Notebook</h2></div>
			<p class="habit">The notebook fills as the keepers read you.</p>
			<p class="muted">Fight a Hellwalker keeper: every exchange is written down - what it threw, how you answered, whether it saw you coming.</p>
		</section>`;
	}
	return h`<section class="card notebook reveal" aria-labelledby="nb-h">
		<div class="nb-title"><h2 id="nb-h">The Keeper's Notebook</h2><p>what the keepers wrote down about you</p></div>
		<div class="nb-cols">
			<div>
				<h3 class="nb-h">What it expects you to do next</h3>
				${anyExpected ? rows.map((r) => h`<div class="exp-row ${r.p >= 0.55 ? "sure" : ""}">
					<span class="against">Against ${r.label}</span>
					<span class="ans">${r.answer || "-"}</span>
					${bar(r.p, r.p >= 0.55 ? "" : "dim", "thin")}
					<span class="p">${r.known ? pct(r.p) : ""}</span>
				</div>`) : h`<p class="muted">Nothing yet - the scripted keepers (Pathbreaker) write nothing down.</p>`}
				${anyExpected ? h`<p class="muted small">After your latest duel with the RL keeper. Red: it is at least 55 % sure - sure enough to READ you.</p>` : ""}
			</div>
			<div>
				<h3 class="nb-h">What you actually did</h3>
				${mixes.map((m) => h`<div class="did">
					<div class="did-head"><span>To its <b>${m.label}</b></span><span>${int(m.n)}</span></div>
					${m.n ? h`<div class="stack" role="img" aria-label="${m.shares.filter((x) => x.n).map((x) => `${answerLabel(x.answer)} ${Math.round(x.share * 100)}%`).join(", ")}">
						${m.shares.filter((x) => x.n).map((x) => h`<i class="a-${x.answer}" data-w="${x.share.toFixed(4)}"></i>`)}</div>
						<div class="did-top">${m.top.map((t) => h`<span><b>${answerLabel(t.answer)}</b> ${pct(t.share)}</span>`)}</div>`
						: h`<div class="did-top"><span>not seen yet</span></div>`}
				</div>`)}
				<div class="legend" aria-hidden="true">${ANSWERS.map((a) => h`<span><i class="a-${a}"></i>${answerLabel(a)}</span>`)}</div>
			</div>
		</div>
		<div class="nb-foot">
			<div class="reads">
				<h3 class="nb-h">How well it reads you</h3>
				<p>${s.predictions ? h`It called your answer right <b>${pct(s.readAcc)}</b> of the time (${int(s.correct)} of ${int(s.predictions)})
					${s.confident ? h` - <b>${pct(s.sureAcc)}</b> when it was sure` : ""} - <b>${int(s.reads)}</b> READ${s.reads === 1 ? "" : "s"} landed.` : "It has not predicted you yet."}</p>
				<p class="habit ${hb.noticed ? "noticed" : ""}">${hb.text}</p>
			</div>
			${perFight.length > 1 ? h`<div class="per-fight-wrap">
				<div class="per-fight" role="img" aria-label="Read accuracy per recent fight, oldest first: ${perFight.map((f) => Math.round((f.predictionsCorrect / f.predictions) * 100) + "%").join(", ")}">
					${perFight.map((f) => h`<i data-h="${(f.predictionsCorrect / f.predictions).toFixed(3)}" title="${date(f.at)}: ${pct(f.predictionsCorrect / f.predictions)}"><b></b></i>`)}
				</div><span>its read, per fight</span></div>` : ""}
		</div>
	</section>`;
}

const RESULT = { win: "Victory", loss: "Defeated", timeout: "Time", quit: "Fled" };

function timeline(fights) {
	return h`<section class="card reveal" aria-labelledby="tl-h">
		<div class="card-title"><h2 id="tl-h">Recent duels</h2><span class="muted">${fights.length ? `the last ${fights.length}` : ""}</span></div>
		${fights.length ? h`<ol class="timeline">${fights.map((f) => {
			const k = keeperByIdx(f.keeper);
			return h`<li class="fight ${f.result}">
				<div class="fight-top">
					<span class="res">${RESULT[f.result] || f.result}</span>
					<span class="fight-keeper">${f.keeperName || (k ? k.name : "A keeper")}</span>
					<span class="pill ${f.brain === "rl" ? "rl" : ""}">${f.brain === "rl" ? "learning keeper" : "scripted"}</span>
					<span class="pill">${f.difficulty}</span>
					<span class="fight-when">${ago(f.at)}</span>
				</div>
				<div class="fight-stats">
					<span><b>${duration(f.seconds)}</b></span>
					<span>dealt <b>${int(f.dmgDealt)}</b> · took <b>${int(f.dmgTaken)}</b></span>
					<span>parried <b>${int(f.keeperParried)}</b>/${int(f.parryAttempts)}</span>
					<span>hits <b>${int(f.playerHits)}</b>/${int(f.playerSwings)}</span>
					<span>dodges <b>${int(f.dodges)}</b></span>
					${f.predictions ? h`<span>it read you <b>${pct(f.predictionsCorrect / f.predictions)}</b></span>` : ""}
					${f.readsLanded ? h`<span><b>${int(f.readsLanded)}</b> READ${f.readsLanded === 1 ? "" : "s"}</span>` : ""}
				</div>
			</li>`;
		})}</ol>` : h`<p class="muted">No duels recorded yet.</p>`}
	</section>`;
}

// ---- the linked browser's extras ----------------------------------------------------------------------------------

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
		<section class="card reveal" aria-labelledby="name-h">
			<div class="card-title"><h2 id="name-h">Your name</h2></div>
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
		<section class="card danger-zone reveal" aria-labelledby="reset-h">
			<div class="card-title"><h2 id="reset-h">Forget me</h2></div>
			<p>Erase everything recorded about your play:</p>
			<ul>
				<li>every duel you uploaded is deleted;</li>
				<li>your totals, your record against each keeper and the keeper's notebook go back to zero;</li>
				<li>your name, the date you started and your survey answers stay.</li>
			</ul>
			<p class="muted small">The game keeps uploading new duels afterwards. The keepers' memory of you inside the game is dropped whenever you quit it, as always.</p>
			<button class="btn danger" type="button" id="reset">Reset: forget everything</button>
			<p class="progress-line" id="reset-progress" aria-live="polite"></p>
		</section>
		</div>

		<section class="card reveal" aria-labelledby="sv-h">
			<div class="card-title"><h2 id="sv-h">How did it feel?</h2>${sv.at ? h`<span class="muted">answered ${ago(sv.at)}</span>` : ""}</div>
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
					<label for="comment">Anything else? <span class="muted">(optional, private)</span></label>
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
			toast("Your name is carved.");
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

// ---- views --------------------------------------------------------------------------------------------------------

function notFound(uid) {
	return {
		title: "No such walker",
		html: h`<div class="wrap section"><div class="card empty">${raw(EMBLEMS.unknown())}
			<h1 class="carved">No walker by that mark</h1>
			<p>Nobody with the id <code>${uid}</code> has walked the valley - or the link is mistyped.</p>
			<a class="btn ghost" href="#/leaderboard">See the leaderboard</a></div></div>`,
	};
}

function notLinked(message) {
	return {
		title: "My stats",
		html: h`<div class="wrap">
			<header class="page-head"><p class="kicker">My stats</p><h1 class="carved">Find yourself</h1>
			<p>There are no accounts and no logins. Your game links this browser to its record.</p></header>
			${message ? h`<p class="note red center">${message}</p>` : ""}
			<div class="grid grid-2 section">
				<section class="card reveal"><div class="card-title"><h2>Link this browser</h2></div>
					<ol class="steps">
						<li>Start <b>HELLWALKER</b> and fight at least one duel.</li>
						<li>Press <b>Esc</b> (or Start) and choose <b>Open my stats page</b>.</li>
						<li>Your browser opens this page, linked: your numbers, the keeper's notebook, the survey and the reset.</li>
					</ol>
				</section>
				<section class="card reveal"><div class="card-title"><h2>No game here?</h2></div>
					<p class="muted">Each install has its own anonymous record. Link the browser from the PC you play on, or look anyone up on the leaderboard.</p>
					<div class="btn-row"><a class="btn ghost" href="#/leaderboard">Leaderboard</a><a class="btn ghost" href="#/download">Download</a></div>
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

	const [p, fights] = await Promise.all([backend.getPlayer(uid), backend.getFights(uid, 20)]);
	if (!p) {
		if (ctx.me) {
			return {
				title: "My stats",
				html: h`<div class="wrap section"><div class="card empty">${raw(EMBLEMS.unknown())}<h1 class="carved">Not written down yet</h1>
					<p>This browser is linked, but the game has not sent a duel yet. Fight one, then come back.</p></div></div>`,
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

	const html = h`<div class="wrap profile">
		${linkError ? h`<p class="note red">${linkError}</p>` : ""}
		${hero(p, s, rank, { me: ctx.me, isYou })}
		<div class="grid grid-2 gap">${skillCard(s)}${recordCard(p, s)}</div>
		<div class="gap">${notebook(p, s, fights)}</div>
		<div class="gap">${timeline(fights)}</div>
		${ctx.me ? meCards(p, comment, backend.mode) : isYou ? h`<p class="lb-note">This is you. <a href="#/me">Open your stats page</a> to rename, answer the survey or reset.</p>` : ""}
	</div>`;

	return {
		title: ctx.me ? "My stats" : p.nickname || "Walker",
		html,
		mount: ctx.me ? (root) => mountMe(root, ctx, p) : null,
	};
}
