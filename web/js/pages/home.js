// HELLWALKER site - #/ : the hero, the READ banner, the live numbers, the two walks, the three keepers, how it learns.

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

// An illustration of one Adaptive AI session: the keeper's insight going into fights 1..6 when you repeat one trick.
const ARC = [0.03, 0.16, 0.4, 0.63, 0.8, 0.9];

const ICONS = {
	watch: `<svg viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M3 24 Q24 6 45 24 Q24 42 3 24 Z"/><circle cx="24" cy="24" r="7"/><circle cx="24" cy="24" r="2" fill="currentColor"/></svg>`,
	remember: `<svg viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><rect x="9" y="5" width="30" height="38"/><path d="M15 14h18M15 21h18M15 28h12"/><path d="M9 5v38" stroke-width="4"/></svg>`,
	counter: `<svg viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M6 42 L30 18"/><path d="M30 18 L36 6 L42 12 L30 18 Z" fill="currentColor"/><path d="M42 42 L18 18"/><path d="M14 22 L22 14"/></svg>`,
	forget: `<svg viewBox="0 0 48 48" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="24" cy="24" r="19" stroke-dasharray="3 5"/><path d="M16 16 L32 32 M32 16 L16 32"/></svg>`,
};

export async function view({ backend, config, onLeave }) {
	let world = null, poolError = null;
	try {
		const pool = await backend.getPool();
		world = worldStats(pool.players);
	} catch (e) {
		poolError = e;
	}
	const dl = config.itchUrl
		? h`<a class="btn" href="${config.itchUrl}" rel="noopener">Play free on Windows</a>`
		: h`<a class="btn" href="#/download">Download the game</a>`;
	const k = (key) => (world ? world.keepers[key] : { fights: 0, wins: 0 });

	const html = h`
	<section class="hero" aria-labelledby="hero-title">
		<div class="hero-bg" aria-hidden="true"></div>
		<div class="hero-shade" aria-hidden="true"></div>
		<div class="hero-in">
			<p class="hero-kicker">Reinforcement-learning boss combat</p>
			<h1 class="hero-title" id="hero-title">HELLWALKER</h1>
			<p class="hero-tag"><span class="dash">—</span> a boss that learns you <span class="dash">—</span></p>
			<p class="hero-sub">It watches how you fight. It learns your habits. Then it throws the counter.</p>
			<div class="btn-row">${dl}<a class="btn ghost" href="#/me">What did it learn about me?</a></div>
			<p class="hero-note">Free · Windows 10/11 · anonymous stats for research, resettable here</p>
		</div>
		<div class="scroll-hint" aria-hidden="true"><span class="scroll-arrow"></span><span>Scroll</span></div>
	</section>

	<div class="wrap">
		<section class="section split" aria-labelledby="int-h">
			<div class="intercept-copy reveal">
				<p class="kicker ember">§ 01 · Intercepted</p>
				<h2 class="sec-title" id="int-h">It tells you when it <em>read</em> you.</h2>
				<p>The keepers of the valley's shrines watch how you fight. Always parry the fast slash? Always step left after
				a heavy? Their brain - a neural network trained from scratch with reinforcement learning - notices, and throws
				the counter. Change, and it changes with you.</p>
				<p>When its sword lands on the answer it predicted, and it was sure, the banner says so.</p>
			</div>
			<div class="reveal">
				<div class="read-banner" id="read-banner" aria-live="off">
					<div class="rb-title">READ</div>
					<div class="rb-ans">${READS[0][0]}</div>
					${bar(READS[0][1], "", "thin")}
					<div class="rb-meta">${Math.round(READS[0][1] * 100)}%  -  ${READS[0][2]}</div>
				</div>
				<p class="read-caption">The answer it expected, how sure it was, and the attack it chose.</p>
			</div>
		</section>

		<section class="section" aria-labelledby="live-h">
			<header class="sec-head">
				<p class="kicker ember"><span class="live-dot" aria-hidden="true"></span>§ 02 · Live from the valley</p>
				<h2 class="sec-title" id="live-h">Every duel is <em>filed</em>.</h2>
			</header>
			${world ? h`<div class="stats-grid reveal">
				<div class="stat"><div class="k">Walkers</div><div class="v">${countUp(world.walkers)}</div></div>
				<div class="stat"><div class="k">Duels fought</div><div class="v">${countUp(world.fights)}</div></div>
				<div class="stat stone"><div class="k">Keepers felled</div><div class="v">${countUp(world.wins)}</div><div class="stat-note">${pct(world.winRate)} of duels</div></div>
				<div class="stat"><div class="k">Parries that land</div><div class="v">${countUp(world.parryRate || 0, "pct")}</div><div class="stat-note">${int(world.parried)} of ${int(world.parryN)} · parries made with the assist ring count too</div></div>
				<div class="stat accent"><div class="k">Answers it called</div><div class="v">${countUp(world.readAcc || 0, "pct")}</div><div class="stat-note">${int(world.correct)} of ${int(world.predictions)} predictions</div></div>
				<div class="stat accent"><div class="k">READs landed</div><div class="v">${countUp(world.reads)}</div></div>
			</div>` : h`<p class="block center muted">The live numbers are not reachable right now (${poolError ? poolError.message : ""}).</p>`}
		</section>

		<section class="section" aria-labelledby="walk-h">
			<header class="sec-head">
				<p class="kicker ember">§ 03 · Two walks</p>
				<h2 class="sec-title" id="walk-h">Learn it - or let it <em>learn you</em>.</h2>
				<p class="sec-sub">Pick a walk on the title screen. The difficulty (Easy, Normal, Hard, Hellwalker) and the parry
				assist are in the settings, for both walks.</p>
			</header>
			<div class="grid grid-2">
				<article class="feature walk normal reveal">
					<span class="feature-no" aria-hidden="true">I</span>
					<div class="walk-name">Normal</div>
					<h3>The scripted keepers</h3>
					<p>All three keepers fight from a script - the same patterns every time, the final shrine included. Learn
					their rhythm, find the openings, and beat them on your own terms.</p>
					<ul>
						<li>No brain behind the eyes: nothing is written about you.</li>
						<li>The place to learn the parry, the ghoststep and every attack.</li>
					</ul>
				</article>
				<article class="feature walk adaptive reveal">
					<span class="feature-no" aria-hidden="true">II</span>
					<div class="walk-name">Adaptive AI</div>
					<h3>The keepers that learn you</h3>
					<p>Every keeper is played by the RL brain, and its strength grows with how well it actually predicts you. It
					starts every session not knowing you, so a trick wins the first fights. Repeat it, and by the fourth or fifth
					duel it has read you. Change, and its insight fades.</p>
					<div class="arc">
						<div class="arc-head"><span>Its insight, one session</span><span>an illustration</span></div>
						<div class="per-fight" role="img" aria-label="Illustration: the keeper's insight going into fights 1 to 6 of one session, ${ARC.map((v) => Math.round(v * 100) + "%").join(", ")}">
							${ARC.map((v, i) => h`<i class="${i >= 3 ? "hot" : ""}" data-h="${v}"><b></b></i>`)}
						</div>
						<div class="arc-foot" aria-hidden="true">${ARC.map((_, i) => h`<span>${i + 1}</span>`)}</div>
					</div>
				</article>
			</div>
		</section>

		<section class="section" aria-labelledby="keep-h">
			<header class="sec-head">
				<p class="kicker ember">§ 04 · The three keepers</p>
				<h2 class="sec-title" id="keep-h">One mind, <em>three</em> faces.</h2>
				<p class="sec-sub">One network plays all three. Who it is and how hard it plays are inputs; a small style reward
				during training gave each its own way of winning.</p>
			</header>
			<div class="grid grid-3">
				${KEEPERS.map((kp) => {
					const rec = k(kp.key);
					return h`<article class="feature keeper ${kp.key} reveal">
						${raw(EMBLEMS[kp.key]())}
						<div class="style">${kp.style}</div>
						<h3>${kp.name}</h3>
						<p class="look">${kp.look}</p>
						<p>${kp.blurb}</p>
						<div class="k-stat"><span>In testing</span><span><b>${kp.evalStat[0]}</b> ${kp.evalStat[1]}</span></div>
						<div class="k-stat"><span>Walkers beat it</span><span><b>${rec.fights ? pct(rec.wins / rec.fights) : "—"}</b> of ${int(rec.fights)} duels</span></div>
					</article>`;
				})}
			</div>
		</section>

		<section class="section" aria-labelledby="how-h">
			<header class="sec-head">
				<p class="kicker ember">§ 05 · How it learns you</p>
				<h2 class="sec-title" id="how-h">It doesn't just <em>fight</em> you. It <em>studies</em> you.</h2>
				<p class="sec-sub">Trained on a billion simulated decisions against thousands of players with different habits,
				it learned one thing above all: work out who it is facing.</p>
			</header>
			<div class="grid grid-4">
				<article class="feature reveal"><div class="feature-icon">${raw(ICONS.watch)}</div><h3>It watches</h3>
					<p>It sees what your character visibly does, a tenth of a second late - never your button presses. Distance,
					guard, the swing you started.</p></article>
				<article class="feature reveal"><div class="feature-icon">${raw(ICONS.remember)}</div><h3>It remembers</h3>
					<p>Every exchange goes into its memory: what it threw, how you answered, who got hurt. The memory follows you
					from shrine to shrine.</p></article>
				<article class="feature reveal"><div class="feature-icon">${raw(ICONS.counter)}</div><h3>It counters</h3>
					<p>Parry every fast slash and the delayed heavy comes. Step left after every heavy and the sweep waits there.
					When it is sure and right: READ.</p></article>
				<article class="feature reveal"><div class="feature-icon">${raw(ICONS.forget)}</div><h3>It forgets</h3>
					<p>Quit the game and its memory of you - and its insight - is gone. Your numbers stay here, anonymous, until
					you reset them: one button on your stats page.</p></article>
			</div>
		</section>

		<section class="section sec-head reveal" aria-labelledby="cta-h">
			<p class="kicker ember">§ 06 · Your file</p>
			<h2 class="sec-title" id="cta-h">Walk in. Then read its <em>notebook</em>.</h2>
			<p class="sec-sub">After a few duels, open your stats page from the game's pause menu: your parries, your wins, how
			well it knew you, and what the keeper expects you to do next.</p>
			<div class="btn-row c gap">${dl}<a class="btn ghost" href="#/leaderboard">See the leaderboard</a></div>
		</section>
	</div>`;

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
