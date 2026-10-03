// HELLWALKER site - #/ : the hero, the live numbers, the three keepers, how it learns you.

import { h, raw, EMBLEMS, countUp, pct, int, bar } from "../ui.js";
import { KEEPERS, worldStats } from "../model.js";

export const loadingText = "The bells are ringing in the valley…";

// The READ banner of the game: after a hit lands on the answer it predicted (and it was sure), the keeper says so.
const READS = [
	["Parry", 0.78, "Delayed Heavy"],
	["Ghoststep left", 0.71, "Heavy Sweep Right"],
	["Block", 0.66, "Heavy Cleave"],
	["Swing back", 0.62, "Feint, then Fast Slash"],
	["Ghoststep back", 0.69, "Dash In"],
];

function sigil() {
	let ticks = "";
	for (let i = 0; i < 72; i++) {
		const a = (i / 72) * Math.PI * 2;
		const r1 = i % 6 === 0 ? 268 : 278, r2 = 288;
		ticks += `<line x1="${(300 + Math.cos(a) * r1).toFixed(1)}" y1="${(300 + Math.sin(a) * r1).toFixed(1)}" x2="${(300 + Math.cos(a) * r2).toFixed(1)}" y2="${(300 + Math.sin(a) * r2).toFixed(1)}"/>`;
	}
	let blades = "";
	for (let i = 0; i < 9; i++) {
		const a = (i / 9) * Math.PI * 2;
		const x = 300 + Math.cos(a) * 205, y = 300 + Math.sin(a) * 205;
		blades += `<path d="M${x.toFixed(1)} ${y.toFixed(1)} l6 -18 l6 18 l-6 34 z" transform="rotate(${(a * 180 / Math.PI + 90).toFixed(1)} ${x.toFixed(1)} ${y.toFixed(1)})"/>`;
	}
	return raw(`<svg class="hero-sigil" viewBox="0 0 600 600" aria-hidden="true">
		<g class="spin" stroke="currentColor" stroke-width="2" fill="none">${ticks}<circle cx="300" cy="300" r="292"/><circle cx="300" cy="300" r="258" stroke-dasharray="2 10"/></g>
		<g class="spin-r" fill="currentColor">${blades}</g>
		<g stroke="currentColor" fill="none" stroke-width="2"><circle cx="300" cy="300" r="150"/><path d="M120 300 Q300 150 480 300 Q300 450 120 300 Z"/><circle cx="300" cy="300" r="46"/></g></svg>`);
}

export async function view({ backend, config, onLeave }) {
	let world = null, poolError = null;
	try {
		const pool = await backend.getPool();
		world = worldStats(pool.players);
	} catch (e) {
		poolError = e;
	}
	const dl = config.itchUrl
		? h`<a class="btn" href="${config.itchUrl}" rel="noopener">Download free for Windows</a>`
		: h`<a class="btn" href="#/download">Download the game</a>`;
	const k = (key) => (world ? world.keepers[key] : { fights: 0, wins: 0 });

	const html = h`
	<section class="hero wrap">
		${sigil()}
		<p class="kicker">A duel against a mind that learns</p>
		<h1 class="carved">HELLWALKER</h1>
		<p class="tagline"><span class="dash">—</span> a boss that learns you <span class="dash">—</span></p>
		<p class="lede">The keepers of the valley's shrines watch how you fight. Always parry the fast slash? Always step left
		after a heavy? Their brain - a neural network trained from scratch with reinforcement learning - notices, and
		throws the counter. Change, and it changes with you.</p>
		<div class="btn-row">${dl}<a class="btn ghost" href="#/me">What did it learn about me?</a></div>
		<p class="hero-note">Free · Windows 10/11 · anonymous stats for research, resettable here</p>
		<div class="read-banner" id="read-banner" aria-live="off">
			<div class="rb-title">READ</div>
			<div class="rb-ans">${READS[0][0]}</div>
			${bar(READS[0][1], "", "thin")}
			<div class="rb-meta">${Math.round(READS[0][1] * 100)}%  -  ${READS[0][2]}</div>
		</div>
		<p class="read-caption">When its sword lands on the answer it predicted, the keeper tells you so.</p>
	</section>

	<section class="section wrap">
		<p class="kicker"><span><span class="live-dot" aria-hidden="true"></span>Live from the valley</span></p>
		${world ? h`<div class="tiles">
			<div class="card tile reveal"><div class="num">${countUp(world.walkers)}</div><div class="lbl">Walkers</div></div>
			<div class="card tile reveal"><div class="num">${countUp(world.fights)}</div><div class="lbl">Duels fought</div></div>
			<div class="card tile reveal"><div class="num">${countUp(world.wins)}</div><div class="lbl">Keepers felled</div><div class="sub">${pct(world.winRate)} of duels</div></div>
			<div class="card tile reveal"><div class="num">${countUp(world.parryRate || 0, "pct")}</div><div class="lbl">Parries that land</div><div class="sub">${int(world.parried)} of ${int(world.parryN)}</div></div>
			<div class="card tile reveal"><div class="num red">${countUp(world.readAcc || 0, "pct")}</div><div class="lbl">Answers it called</div><div class="sub">${int(world.correct)} of ${int(world.predictions)} reads</div></div>
			<div class="card tile reveal"><div class="num red">${countUp(world.reads)}</div><div class="lbl">READs landed</div></div>
		</div>` : h`<p class="card center muted">The live numbers are not reachable right now (${poolError ? poolError.message : ""}).</p>`}
	</section>

	<section class="section wrap">
		<h2 class="section-title carved">The three keepers</h2>
		<p class="section-sub">One network plays all three. Who it is and how hard it plays are inputs; a small style reward
		during training gave each its own way of winning.</p>
		<div class="grid grid-3">
			${KEEPERS.map((kp) => {
				const rec = k(kp.key);
				return h`<article class="card glow keeper ${kp.key} reveal">
					${raw(EMBLEMS[kp.key]())}
					<div class="style">${kp.style}</div>
					<h3>${kp.name}</h3>
					<p class="look">${kp.look}</p>
					<p>${kp.blurb}</p>
					<div class="k-stat"><span>In testing: <b>${kp.evalStat[0]}</b> ${kp.evalStat[1]}</span></div>
					<div class="k-stat"><span>Walkers beat it</span><span><b>${rec.fights ? pct(rec.wins / rec.fights) : "—"}</b> of ${int(rec.fights)} duels</span></div>
				</article>`;
			})}
		</div>
	</section>

	<section class="section wrap">
		<h2 class="section-title carved">How it learns you</h2>
		<p class="section-sub">Trained on a billion simulated decisions against thousands of players with different habits,
		it learned one thing above all: work out who it is facing.</p>
		<div class="beats">
			<div class="card beat reveal"><h3>It watches</h3><p>It sees what your character visibly does, a tenth of a second late -
			never your button presses. Distance, guard, the swing you started.</p></div>
			<div class="card beat reveal"><h3>It remembers</h3><p>Every exchange goes into its memory: what it threw, how you answered,
			who got hurt. The memory follows you from shrine to shrine.</p></div>
			<div class="card beat reveal"><h3>It counters</h3><p>Parry every fast slash and the delayed heavy comes. Step left after
			every heavy and the sweep waits there. When it is sure and right: READ.</p></div>
			<div class="card beat reveal"><h3>It forgets</h3><p>Quit the game and its memory of you is gone. Your numbers stay here,
			anonymous, until you reset them - one button on your stats page.</p></div>
		</div>
	</section>

	<section class="section wrap center">
		<div class="card reveal">
			<h2 class="carved">Walk in. Then read its notebook.</h2>
			<p class="muted">After a few duels, open your stats page from the game's pause menu: your parries, your wins, and what the keeper expects you to do next.</p>
			<div class="btn-row c">${dl}<a class="btn ghost" href="#/leaderboard">See the leaderboard</a></div>
		</div>
	</section>`;

	return {
		title: null,
		html,
		mount(root) {
			const el = root.querySelector("#read-banner");
			if (!el || (window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches)) return;
			let i = 0;
			const timer = setInterval(() => {
				i = (i + 1) % READS.length;
				el.classList.add("swap");
				setTimeout(() => {
					const [ans, p, counter] = READS[i];
					el.querySelector(".rb-ans").textContent = ans;
					el.querySelector(".rb-meta").textContent = `${Math.round(p * 100)}%  -  ${counter}`;
					const fill = el.querySelector(".bar > i");
					fill.style.width = (p * 100).toFixed(1) + "%";
					el.classList.remove("swap");
				}, 380);
			}, 3600);
			onLeave(() => clearInterval(timer));
		},
	};
}
