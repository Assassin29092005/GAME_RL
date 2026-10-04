// HELLWALKER site - boot and hash router.
//   #/ home · #/download · #/leaderboard[/wins|parry|read|fights] · #/p/<uid> · #/me[?t=<refreshToken>] · #/research
// Data: config.js -> live Firebase; empty apiKey/projectId -> demo data; ?mock=<url> (localhost only) -> the mock.

import config from "../config.js";
import { RemoteBackend } from "./api.js";
import { DemoBackend } from "./demo.js";
import { startEmbers } from "./embers.js";
import { $, $$, h, hydrate, EMBLEMS, raw } from "./ui.js";
import * as home from "./pages/home.js";
import * as download from "./pages/download.js";
import * as leaderboard from "./pages/leaderboard.js";
import * as profile from "./pages/profile.js";
import * as research from "./pages/research.js";

function mockUrl() {
	const m = new URLSearchParams(location.search).get("mock");
	if (!m) return null;
	try {
		const u = new URL(m);
		// The mock is a local development tool; never route the site's requests anywhere else.
		if ((u.protocol === "http:" || u.protocol === "https:") && ["127.0.0.1", "localhost", "[::1]"].includes(u.hostname)) return u.origin;
	} catch {
		/* ignored below */
	}
	console.warn("Ignoring ?mock= (only http://127.0.0.1:<port> or http://localhost:<port> is allowed):", m);
	return null;
}

function makeBackend() {
	const mock = mockUrl();
	if (mock) return new RemoteBackend({ apiKey: config.apiKey || "mock-api-key", projectId: config.projectId || "hellwalker-mock", mock });
	if (config.apiKey && config.projectId) return new RemoteBackend({ apiKey: config.apiKey, projectId: config.projectId });
	return new DemoBackend();
}

const backend = makeBackend();
const main = document.getElementById("main");

function showBanner() {
	const el = document.getElementById("banner");
	if (backend.mode === "demo") {
		el.innerHTML = h`<b>Demo file</b><span>Sample data - no Firebase project is configured yet, so every walker and duel here is generated.</span>`.__raw;
		el.hidden = false;
	} else if (backend.mode === "mock") {
		el.innerHTML = h`<b>Mock backend</b><span>Reading and writing the local mock at ${mockUrl()} - not the real database.</span>`.__raw;
		el.className = "banner mock";
		el.hidden = false;
	}
}

// The pages that stand in front of the keeper's silhouette (the rest have the plain ink and ember atmosphere).
const SILHOUETTE = new Set(["profile", "me", "download", "lost"]);

function setScene(page, file) {
	document.body.dataset.page = page;
	document.body.classList.toggle("has-silhouette", SILHOUETTE.has(page));
	const f = $(".file-no");
	if (f) f.textContent = file || "Hellwalker · the keeper's files";
}

const ROUTES = [
	{ re: /^\/?$/, page: home, nav: "home" },
	{ re: /^\/download\/?$/, page: download, nav: "download" },
	{ re: /^\/leaderboard(?:\/([a-z]+))?\/?$/, page: leaderboard, nav: "leaderboard", args: (m) => ({ board: m[1] }) },
	{ re: /^\/p\/([A-Za-z0-9_-]{1,128})\/?$/, page: profile, nav: null, args: (m) => ({ uid: m[1] }) },
	{ re: /^\/me\/?$/, page: profile, nav: "me", args: () => ({ me: true }) },
	{ re: /^\/research\/?$/, page: research, nav: "research" },
];

function parseHash() {
	const hash = location.hash.replace(/^#/, "") || "/";
	const q = hash.indexOf("?");
	return { path: q < 0 ? hash : hash.slice(0, q), params: new URLSearchParams(q < 0 ? "" : hash.slice(q + 1)) };
}

let seq = 0;
let leaving = [];

function setNav(nav) {
	for (const a of $$(".nav a")) {
		if (a.dataset.route === nav) a.setAttribute("aria-current", "page");
		else a.removeAttribute("aria-current");
	}
	$(".nav").classList.remove("open");
	$(".nav-toggle").setAttribute("aria-expanded", "false");
}

function loadingMarkup(text) {
	return h`<div class="wrap loading-page"><p class="loading"><span class="boot-mark" aria-hidden="true"></span>${text}</p></div>`.__raw;
}

function errorMarkup(e, retry = true) {
	const msg = e && e.message ? e.message : String(e);
	return h`<div class="wrap page"><div class="block empty" role="alert">${raw(EMBLEMS.unknown())}
		<p class="kicker ember">§ Error · the file is sealed</p>
		<h2 class="empty-title">The notebook would not <em>open</em></h2><p>${msg}</p>
		${retry ? h`<button type="button" class="btn ghost" data-retry>Try again</button>` : ""}</div></div>`.__raw;
}

async function render({ keepScroll = false } = {}) {
	const my = ++seq;
	for (const fn of leaving) {
		try { fn(); } catch { /* a page's cleanup must not stop the next page */ }
	}
	leaving = [];
	let { path, params } = parseHash();

	// A link from the game: take the refresh token out of the address bar before anything else (CONTRACT.md "Identity").
	let linkToken = null;
	if (/^\/me\/?$/.test(path) && params.has("t")) {
		linkToken = params.get("t");
		history.replaceState(null, "", location.pathname + location.search + "#/me");
		params = new URLSearchParams();
	}

	const route = ROUTES.find((r) => r.re.test(path));
	setNav(route ? route.nav : null);
	const scrollY = window.scrollY;
	if (!route) {
		document.title = "Lost in the valley · Hellwalker";
		setScene("lost", "File HW-404 · no such path");
		main.innerHTML = h`<div class="wrap page"><div class="lost">
			<p class="kicker ember">§ 404 · No record</p>
			<h1 class="subject-name">Lost in the <em>valley</em></h1>
			<p class="lost-text">No path leads to <code>${path}</code>. The keeper watched you wander off the map.</p>
			<div class="btn-row"><a class="btn" href="#/">Back to the bell</a><a class="btn ghost" href="#/leaderboard">The leaderboard</a></div></div></div>`.__raw;
		const h1 = $("h1", main);
		if (h1) {
			h1.setAttribute("tabindex", "-1");
			h1.focus({ preventScroll: true });
		}
		return;
	}
	setScene(route.nav || "profile");
	if (!keepScroll) main.innerHTML = loadingMarkup(route.page.loadingText || "The keeper opens its notebook…");
	const ctx = {
		backend, config, params, linkToken,
		...(route.args ? route.args(path.match(route.re)) : {}),
		rerender: (opts) => render({ keepScroll: true, ...opts }),
		onLeave: (fn) => leaving.push(fn),
		isCurrent: () => my === seq,
	};
	try {
		const out = await route.page.view(ctx);
		if (my !== seq) return;
		document.title = out.title ? `${out.title} · Hellwalker` : "Hellwalker: a boss that learns you";
		setScene(route.nav || "profile", out.file);
		main.innerHTML = out.html.__raw;
		hydrate(main);
		if (out.mount) out.mount(main, ctx);
	} catch (e) {
		if (my !== seq) return;
		console.error(e);
		main.innerHTML = errorMarkup(e);
		const b = $("[data-retry]", main);
		if (b) b.addEventListener("click", () => render());
	}
	if (keepScroll) {
		window.scrollTo(0, scrollY);
	} else {
		window.scrollTo(0, 0);
		const h1 = $("h1", main);
		if (h1) {
			h1.setAttribute("tabindex", "-1");
			h1.focus({ preventScroll: true });
		}
	}
}

function boot() {
	showBanner();
	startEmbers(document.getElementById("embers"));
	const toggle = $(".nav-toggle");
	toggle.addEventListener("click", () => {
		const open = $(".nav").classList.toggle("open");
		toggle.setAttribute("aria-expanded", String(open));
	});
	window.addEventListener("hashchange", () => render());
	render();
}

boot();
