// HELLWALKER site - #/leaderboard[/wins|parry|read|fights] : four boards over the most active players.

import { h, rankBadge, $, $$, int } from "../ui.js";
import { BOARDS, skills, rankOf, worldStats } from "../model.js";
import { POOL_LIMIT } from "../api.js";

export const loadingText = "Counting the fallen keepers…";
const SHOW = 50;

function board(b, players, meUid) {
	const rows = b.rows(players).slice(0, SHOW);
	if (!rows.length) {
		return h`<div class="lb-empty">${b.id === "parry" || b.id === "read" ? "Nobody has qualified yet." : "No duels recorded yet."} The valley is quiet.</div>`;
	}
	return h`<div class="lb-head" aria-hidden="true"><span class="center">№</span><span>Walker</span><span class="r">${b.col}</span><span class="r">Detail</span></div>
		${rows.map((p, i) => {
			const s = skills(p);
			return h`<div class="lb-row ${i < 3 ? "pos-" + (i + 1) : ""} ${p.id === meUid ? "me" : ""}">
				<span class="lb-pos">${String(i + 1).padStart(2, "0")}</span>
				<span class="lb-name">${rankBadge(rankOf(p), "sm")}<a href="#/p/${encodeURIComponent(p.id)}">${p.nickname || "Nameless walker"}</a>${p.id === meUid ? h`<span class="pill you">you</span>` : ""}</span>
				<span class="lb-val">${b.value(s)}</span>
				<span class="lb-detail">${b.detail(s)}</span>
			</div>`;
		})}`;
}

export async function view({ backend, board: which }) {
	const pool = await backend.getPool();
	const me = backend.linked();
	const active = BOARDS.find((b) => b.id === which) || BOARDS[0];
	const total = pool.players.filter((p) => skills(p).fights > 0).length;
	const world = worldStats(pool.players);

	const html = h`
	<div class="wrap page">
		<header class="dossier-head">
			<div>
				<div class="dossier-meta">
					<div>File no.<b>HW—HALL</b></div>
					<div>Walkers<b>${int(total)}</b></div>
					<div>Duels on file<b>${int(world.fights)}</b></div>
				</div>
				<h1 class="subject-name">Hall of the <em>fallen</em></h1>
				<p class="dossier-lede">Every walker is anonymous: a name the game made up, or one its owner chose. ${int(total)} ${total === 1 ? "walker has" : "walkers have"} fought so far.</p>
			</div>
			<div class="threat-block"><span class="stamp verdigris">Anonymous · public</span></div>
		</header>
		<div class="tabs" role="tablist" aria-label="Leaderboards">
			${BOARDS.map((b) => h`<button type="button" class="tab" role="tab" id="tab-${b.id}" aria-controls="panel-${b.id}"
				aria-selected="${b === active ? "true" : "false"}" tabindex="${b === active ? "0" : "-1"}" data-board="${b.id}">${b.tab}</button>`)}
		</div>
		${BOARDS.map((b) => h`<section class="block lb" role="tabpanel" id="panel-${b.id}" aria-labelledby="tab-${b.id}" ${b === active ? "" : h`hidden`}>
			<h2 class="sr-only">${b.title}</h2>
			${board(b, pool.players, me && me.uid)}
		</section>
		${b.note ? h`<p class="lb-note" data-note="${b.id}" ${b === active ? "" : h`hidden`}>${b.note}</p>` : ""}`)}
		${pool.capped ? h`<p class="lb-note">Ranked among the ${POOL_LIMIT} walkers with the most duels.</p>` : ""}
		${me ? "" : h`<p class="lb-note">Played the game? Open <b>Pause → Open my stats page</b> in it to find yourself here.</p>`}
	</div>`;

	return {
		title: "Leaderboard",
		file: "File HW-HALL · the leaderboard",
		html,
		mount(root) {
			const tabs = $$(".tab", root);
			const select = (tab, focus) => {
				for (const t of tabs) {
					const on = t === tab;
					t.setAttribute("aria-selected", on ? "true" : "false");
					t.tabIndex = on ? 0 : -1;
					$("#panel-" + t.dataset.board, root).hidden = !on;
					const note = $(`[data-note="${t.dataset.board}"]`, root);
					if (note) note.hidden = !on;
				}
				if (focus) tab.focus();
				history.replaceState(null, "", location.pathname + location.search + "#/leaderboard/" + tab.dataset.board);
			};
			for (const t of tabs) {
				t.addEventListener("click", () => select(t, false));
				t.addEventListener("keydown", (e) => {
					const i = tabs.indexOf(t);
					if (e.key === "ArrowRight") select(tabs[(i + 1) % tabs.length], true);
					else if (e.key === "ArrowLeft") select(tabs[(i + tabs.length - 1) % tabs.length], true);
					else if (e.key === "Home") select(tabs[0], true);
					else if (e.key === "End") select(tabs[tabs.length - 1], true);
					else return;
					e.preventDefault();
				});
			}
		},
	};
}
