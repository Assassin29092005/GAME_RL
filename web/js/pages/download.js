// HELLWALKER site - #/download : the itch.io button, what the PC needs, the research note, first steps.

import { h } from "../ui.js";

export async function view({ config }) {
	const button = config.itchUrl
		? h`<a class="btn" href="${config.itchUrl}" rel="noopener">
				<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12m0 0l-5-5m5 5l5-5M4 19h16" fill="none" stroke="currentColor" stroke-width="2.2"/></svg>
				Download on itch.io</a>`
		: h`<span class="btn" aria-disabled="true">Coming soon to itch.io</span>`;
	const missing = config.itchUrl ? "" : h`<p class="hint">The download link appears here once the itch.io page is live (<code>itchUrl</code> in <code>config.js</code>).</p>`;

	const html = h`
	<div class="wrap">
		<header class="page-head">
			<p class="kicker">Free download</p>
			<h1 class="carved">Enter the valley</h1>
			<p>Three shrines, three keepers, one mind that learns you. The game is free; no account, no login.</p>
		</header>

		<div class="card dl-card reveal">
			<div class="btn-row c">${button}</div>
			${missing}
			<p class="hint">Windows 10 / 11, 64-bit · a zip of the whole game folder · run <b>HellwalkerRL.exe</b></p>
		</div>

		<div class="grid grid-2 section">
			<section class="card reveal" aria-labelledby="req-h">
				<div class="card-title"><h2 id="req-h">What your PC needs</h2></div>
				<ul class="reqs">
					<li><span class="k">System</span><span class="v">Windows 10 or 11, 64-bit</span></li>
					<li><span class="k">Graphics</span><span class="v">A DirectX 12 graphics card</span></li>
					<li><span class="k">Disk</span><span class="v">About 8 GB free</span></li>
					<li><span class="k">Input</span><span class="v">Keyboard and mouse, or a gamepad</span></li>
					<li><span class="k">Internet</span><span class="v">Optional - only to send the anonymous research stats</span></li>
				</ul>
			</section>

			<section class="card reveal" aria-labelledby="steps-h">
				<div class="card-title"><h2 id="steps-h">First steps</h2></div>
				<ol class="steps">
					<li><b>Download</b> the zip from itch.io (or install it with the itch app).</li>
					<li><b>Unzip</b> the whole folder anywhere and run <b>HellwalkerRL.exe</b>. Windows may ask once whether to run an unsigned game.</li>
					<li><b>Choose a walk:</b> Pathbreaker (a scripted keeper you can learn) or Hellwalker (the one that learns you).</li>
					<li>After a few duels, open <b>Pause → Open my stats page</b>. This browser becomes yours: your page, the survey, the reset.</li>
				</ol>
			</section>
		</div>

		<div class="grid grid-2 section">
			<section class="card reveal" aria-labelledby="keys-h">
				<div class="card-title"><h2 id="keys-h">In a duel</h2><span class="muted">F1 shows every binding</span></div>
				<div class="keys">
					<span><kbd>LMB</kbd></span><span>Light attack (chain)</span>
					<span><kbd>E</kbd></span><span>Heavy attack</span>
					<span><kbd>RMB</kbd></span><span>Block (hold)</span>
					<span><kbd>Q</kbd></span><span>Parry - just before the hit</span>
					<span><kbd>Space</kbd> + dir</span><span>Ghoststep (dodge)</span>
					<span><kbd>N</kbd></span><span>The keeper's notebook: what it wrote down about you</span>
				</div>
			</section>

			<section class="card reveal" aria-labelledby="tel-h">
				<div class="card-title"><h2 id="tel-h">Research stats</h2></div>
				<p class="note">The game sends <b>anonymous gameplay stats</b> after every duel - wins, parries, hits, dodges and what
				the keeper predicted - for a research paper on how players fare against a learning boss and how they feel about it.
				No name, no email, no account: a random id per install.</p>
				<p class="muted small">You can <a href="#/me">reset everything</a> recorded about you from your stats page at any time.
				<a href="#/research">What exactly is collected</a>.</p>
			</section>
		</div>
	</div>`;
	return { title: "Download", html };
}
