// HELLWALKER site - #/download : the download button (GitHub Releases or itch.io), what the PC needs, first steps, the settings,
// the research note.

import { h, raw } from "../ui.js";

export async function view({ config }) {
	const url = config.downloadUrl || config.itchUrl || "";
	const onGitHub = /(^|\.)github\.com\//.test(url.replace(/^https?:\/\//, ""));
	const host = onGitHub ? "from GitHub" : (/itch\.io/.test(url) ? "on itch.io" : "the game");
	const button = url
		? h`<a class="btn" href="${url}" rel="noopener">
				<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12m0 0l-5-5m5 5l5-5M4 19h16" fill="none" stroke="currentColor" stroke-width="2.2"/></svg>
				Download ${host}</a>`
		: h`<span class="btn" aria-disabled="true">Download coming soon</span>`;
	const missing = url ? "" : h`<p class="hint">The download link appears here once it is set (<code>downloadUrl</code> in <code>config.js</code>).</p>`;
	const note = onGitHub ? "Windows 64-bit · 7.4 GB in 4 parts + Join-and-Extract.bat · run HellwalkerRL.exe"
		: "Windows 64-bit · a zip of the game folder · run HellwalkerRL.exe";
	const getSteps = onGitHub
		? h`<li><span><b>Download every file</b> of the latest release into one folder: all the parts (<code>….zip.001</code>, <code>.002</code>, …) and <b>Join-and-Extract.bat</b>.</span></li>
			<li><span><b>Double-click Join-and-Extract.bat.</b> It joins the parts and unpacks the <b>HellwalkerRL</b> folder (about 16 GB free while it works). Using 7-Zip? Open the <code>.001</code> part and extract it instead.</span></li>
			<li><span>Open the folder and run <b>HellwalkerRL.exe</b>. Windows may say "Windows protected your PC" (the game is unsigned): <b>More info → Run anyway</b>. A PC without the Microsoft Visual C++ runtime gets it installed first (one Windows prompt).</span></li>`
		: h`<li><span><b>Download</b> the zip (or install it with the itch app).</span></li>
			<li><span><b>Unzip</b> the whole folder anywhere and run <b>HellwalkerRL.exe</b>. Windows may ask once whether to run an unsigned game.</span></li>`;
	const ring = raw('<span class="ring-ico" aria-hidden="true"></span>');

	const html = h`
	<div class="wrap page">
		<header class="dossier-head">
			<div>
				<div class="dossier-meta">
					<div>Issue<b>Free download</b></div>
					<div>Platform<b>Windows 10 / 11</b></div>
					<div>Account<b>None</b></div>
				</div>
				<h1 class="subject-name">Face it <em>yourself</em>.</h1>
			</div>
			<div class="threat-block"><span class="stamp">For the valley</span></div>
		</header>

		<section class="dl-hero reveal" aria-label="Download">
			<div class="dl-info">
				<p class="kicker ember">§ Field issue</p>
				<p>Three shrines, three keepers, one mind that learns you. Walk the scripted keepers until you know them, or let
				the Adaptive AI keepers learn you: a trick wins the first duels - by the fourth or fifth, they have read it.</p>
				<p>The game is free. No account, no login: open your stats page from the game and this site writes your file.</p>
			</div>
			<div class="dl-cta">
				${button}
				${missing}
				<span class="dl-cta-note">${note}</span>
			</div>
		</section>

		<div class="cols">
			<section class="block span-6 reveal" aria-labelledby="req-h">
				<header class="block-head"><h2 class="block-title" id="req-h">What your PC needs</h2><span class="block-tag">§ 01 · Requirements</span></header>
				<table class="req-table">
					<tbody>
						<tr><td>System</td><td>Windows 10 or 11, 64-bit</td></tr>
						<tr><td>Graphics</td><td>A DirectX 12 graphics card</td></tr>
						<tr><td>Disk</td><td>About 8 GB free (16 GB while unpacking, counting the downloaded parts)</td></tr>
						<tr><td>Input</td><td>Keyboard and mouse, or a gamepad</td></tr>
						<tr><td>Internet</td><td>Optional - only to send the anonymous research stats</td></tr>
					</tbody>
				</table>
			</section>

			<section class="block span-6 reveal" aria-labelledby="steps-h">
				<header class="block-head"><h2 class="block-title" id="steps-h">First steps</h2><span class="block-tag">§ 02 · Install and play</span></header>
				<ol class="steps">
					${getSteps}
					<li><span><b>Choose a walk:</b> <b>Normal</b> (scripted keepers you can learn) or <b>Adaptive AI</b> (the keepers that learn you).</span></li>
					<li><span>After a few duels, open <b>Pause → Open my stats page</b>. This browser becomes yours: your page, the survey, the reset.</span></li>
				</ol>
			</section>

			<section class="block span-6 reveal" aria-labelledby="keys-h">
				<header class="block-head"><h2 class="block-title" id="keys-h">In a duel</h2><span class="block-tag">F1 shows every binding</span></header>
				<div class="keys">
					<span><kbd>LMB</kbd></span><span>Light attack (chain)</span>
					<span><kbd>E</kbd></span><span>Heavy attack</span>
					<span><kbd>RMB</kbd></span><span>Block (hold)</span>
					<span><kbd>Q</kbd></span><span>Parry - press while the ${ring}<b>red ring</b> around the keeper's weapon is lit</span>
					<span><kbd>Space</kbd> + dir</span><span>Ghoststep (dodge) - the answer to a <b class="violet">violet</b> telegraph (killer thrust, grab): it cannot be blocked or parried</span>
					<span><kbd>N</kbd></span><span>The keeper's notebook: what it wrote down about you</span>
				</div>
			</section>

			<section class="block span-6 reveal" aria-labelledby="set-h">
				<header class="block-head"><h2 class="block-title" id="set-h">Settings worth knowing</h2><span class="block-tag">§ 03 · Pause → Settings</span></header>
				<ul class="settings-list">
					<li><span class="k">Difficulty</span><b>Easy · Normal · Hard · Hellwalker</b> - how hard the keepers hit, and in Adaptive AI how strong
						they can grow once they know you. Normal is the default; every difficulty is gentler than in earlier versions.</li>
					<li><span class="k">Parry assist</span><b>Ring + slow-mo</b> (default) · <b>Ring</b> · <b>Off</b> - the red ring lights exactly
						while a parry press would land; with slow-mo the duel slows down while it is lit.</li>
				</ul>
			</section>

			<section class="block span-12 reveal" aria-labelledby="tel-h">
				<header class="block-head"><h2 class="block-title" id="tel-h">Research stats</h2><span class="block-tag">§ 04 · Anonymous</span></header>
				<p class="note">The game sends <b>anonymous gameplay stats</b> after every duel - wins, parries, hits, dodges, your settings and what
				the keeper predicted - for a research paper on how players fare against a learning boss and how they feel about it.
				No name, no email, no account: a random id per install.</p>
				<p class="muted small">You can <a href="#/me">reset everything</a> recorded about you from your stats page at any time.
				<a href="#/research">What exactly is collected</a>.</p>
			</section>
		</div>
	</div>`;
	return { title: "Download", file: "File HW-DL · field issue", html };
}
