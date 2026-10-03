// HELLWALKER site - #/research : what is collected, why, how the reset works, who to contact.

import { h } from "../ui.js";

export async function view({ config }) {
	const c = (config.contact || "").trim();
	const contact = !c
		? h`<em>a contact address will be added here before the study opens</em>`
		: /^[^@\s]+@[^@\s]+$/.test(c) ? h`<a href="mailto:${c}">${c}</a>` : /^https?:\/\//.test(c) ? h`<a href="${c}" rel="noopener">${c}</a>` : h`${c}`;

	const html = h`
	<div class="wrap">
		<header class="page-head">
			<p class="kicker">Research</p>
			<h1 class="carved">What the keeper keeps</h1>
			<p>HELLWALKER is part of a research project on a boss driven by reinforcement learning: how players fare against it, and how they feel about it.</p>
		</header>
		<article class="card prose reveal">
			<h2>Why</h2>
			<p>The keepers' brain was trained against simulated players only. Whether it reads <strong>real</strong> people - and whether
			being read feels fair, hard, or fun - can only be measured with real duels. The results section of the paper reports exactly
			the numbers on this site: win and parry rates, how often the keeper predicted players' answers, how that changed over a
			session, and the survey answers.</p>

			<h2>Who you are to us: nobody</h2>
			<ul>
				<li>No account and no login. On first launch the game signs in to Firebase <strong>anonymously</strong> and gets a random id.</li>
				<li>Your name on this site is made up by the game (like <em>Ashen Wanderer 4821</em>) unless you change it.</li>
				<li>We never ask for your name, email, age or location. Google's servers (which host the data) see your IP address to
				deliver it, as for any website; it is not part of the dataset.</li>
			</ul>

			<h2>What one duel sends</h2>
			<table class="datatable">
				<thead><tr><th>Group</th><th>Fields</th></tr></thead>
				<tbody>
					<tr><td>The fight</td><td>which keeper, the difficulty, scripted or learning keeper, open world or arena, the result, how long it took, health left on both sides, game version, the time</td></tr>
					<tr><td>Your play</td><td>swings and hits, damage dealt and taken, parry presses, parries that caught a swing, blocks, dodges (ghoststeps), guard breaks</td></tr>
					<tr><td>The keeper's view</td><td>its swings and how they ended, how many of your answers it predicted and how many it got right, how sure it was, the READs that landed</td></tr>
					<tr><td>The notebook</td><td>what you did against each kind of keeper swing (counts per answer), and what it expects you to do next against each of its eight attacks</td></tr>
				</tbody>
			</table>
			<p>Never sent: your inputs or button presses, recordings, anything about your PC, anything outside the duels.
			The full field list is the project's <a href="CONTRACT.md">telemetry contract</a>.</p>

			<h2>Who can see it</h2>
			<ul>
				<li>Your numbers and duels are <strong>public but anonymous</strong>: anyone can open <code>#/p/&lt;id&gt;</code> or the leaderboard.</li>
				<li>Only a browser linked from your game (<em>Pause → Open my stats page</em>) can rename you, answer the survey or reset.</li>
				<li>The survey's 1-5 answers are public and anonymous; the optional comment is <strong>private</strong> - only the researcher reads it.</li>
			</ul>

			<h2>How the reset works</h2>
			<p>On your stats page, <em>Forget everything it learned about me</em> does exactly this:</p>
			<ol>
				<li>every duel recorded for your id is deleted from the database;</li>
				<li>your totals, your record against each keeper, your answers and the keeper's expectations are set back to zero;
				the number of resets and the time of the last one are recorded;</li>
				<li>your name, the date you started and your survey answers stay.</li>
			</ol>
			<p>The game keeps no totals of its own, so nothing comes back. New duels after a reset are recorded as usual. The keepers'
			memory of you <em>inside the game</em> was never stored: it is dropped every time you quit.</p>

			<h2>How long it is kept</h2>
			<p>For the duration of the research project. Anonymous, aggregated results may be published in the paper.</p>

			<h2>Contact</h2>
			<p>Questions, or a request that we cannot handle through the reset: ${contact}.</p>
		</article>
	</div>`;
	return { title: "Research", html };
}
