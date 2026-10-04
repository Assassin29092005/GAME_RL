// HELLWALKER site - small DOM helpers: escaped HTML templates, number formats, animated bars, emblems, the line chart.
// Pages build HTML strings with h`...` (every interpolated value is escaped unless wrapped in raw()), then call
// hydrate(root) once the markup is in the page: bars fill and numbers count up as they scroll into view.

const ESC = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" };
export const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ESC[c]);
export const raw = (s) => ({ __raw: String(s) });

function part(v) {
	if (v === null || v === undefined || v === false) return "";
	if (Array.isArray(v)) return v.map(part).join("");
	if (typeof v === "object" && "__raw" in v) return v.__raw;
	return esc(v);
}

/** Tagged template: h`<p>${text}</p>` escapes text; nested h`` results and raw() pass through. */
export function h(strings, ...vals) {
	let out = "";
	strings.forEach((s, i) => {
		out += s;
		if (i < vals.length) out += part(vals[i]);
	});
	return raw(out);
}

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

// ---- formats --------------------------------------------------------------------------------------------------------

export const clamp01 = (x) => Math.max(0, Math.min(1, x));
export const pct = (x, digits = 0) => (x === null || x === undefined || !isFinite(x) ? "—" : (x * 100).toFixed(digits) + "%");
export const int = (n) => (n === null || n === undefined || !isFinite(n) ? "—" : Math.round(n).toLocaleString("en-US"));
export const dec = (n, d = 1) => (n === null || n === undefined || !isFinite(n) ? "—" : n.toLocaleString("en-US", { minimumFractionDigits: d, maximumFractionDigits: d }));

export function duration(seconds) {
	if (!isFinite(seconds) || seconds <= 0) return "0 s";
	if (seconds < 90) return Math.round(seconds) + " s";
	const m = seconds / 60;
	if (m < 90) return Math.round(m) + " min";
	const hrs = m / 60;
	return (hrs < 10 ? hrs.toFixed(1) : Math.round(hrs)) + " h";
}

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
export function date(d) {
	if (!(d instanceof Date) || isNaN(d)) return "—";
	return d.getDate() + " " + MONTHS[d.getMonth()] + " " + d.getFullYear();
}

export function ago(d) {
	if (!(d instanceof Date) || isNaN(d)) return "—";
	const s = (Date.now() - d.getTime()) / 1000;
	if (s < 0) return date(d);
	if (s < 60) return "just now";
	if (s < 3600) return Math.floor(s / 60) + " min ago";
	if (s < 86400) return Math.floor(s / 3600) + " h ago";
	if (s < 86400 * 30) return Math.floor(s / 86400) + " d ago";
	return date(d);
}

// ---- widgets --------------------------------------------------------------------------------------------------------

/** An animated bar; frac 0..1 (null = empty). tone: "" (ember), "gold" (bone), "stone" (verdigris), "dim". */
export function bar(frac, tone = "", extra = "") {
	const f = frac === null || frac === undefined || !isFinite(frac) ? 0 : clamp01(frac);
	return h`<div class="bar ${tone} ${extra}" data-fill="${f.toFixed(4)}" aria-hidden="true"><i></i></div>`;
}

/** A vital sign: a small label, a big number (unit set smaller, e.g. "%"), a thin bar and a note. */
export function stat(name, value, frac, { tone = "", unit = "", note = "", accent = false } = {}) {
	return h`<div class="stat ${accent ? "accent" : ""} ${tone}" role="group" aria-label="${name}: ${value}${unit}">
		<div class="k">${name}</div>
		<div class="v">${value}${unit ? h`<small>${unit}</small>` : ""}</div>
		${bar(frac, tone, "thin")}
		${note ? h`<div class="stat-note">${note}</div>` : ""}
	</div>`;
}

/** Count-up number: the element shows `final` as text for no-JS/no-motion, animates from 0 when visible. */
export function countUp(value, format = "int") {
	const v = isFinite(value) ? value : 0;
	const shown = format === "pct" ? pct(v) : int(v);
	return h`<span data-count="${v}" data-format="${format}">${shown}</span>`;
}

const reduceMotion = () => window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

let observer = null;
function onVisible(el, fn) {
	if (!("IntersectionObserver" in window) || reduceMotion()) {
		fn(el);
		return;
	}
	if (!observer) {
		observer = new IntersectionObserver((entries) => {
			for (const e of entries) {
				if (e.isIntersecting) {
					observer.unobserve(e.target);
					const f = e.target.__onVisible;
					delete e.target.__onVisible;
					if (f) f(e.target);
				}
			}
		}, { rootMargin: "0px 0px -8% 0px", threshold: 0.05 });
	}
	el.__onVisible = fn;
	observer.observe(el);
}

function animateCount(el) {
	const target = parseFloat(el.dataset.count) || 0;
	const fmt = el.dataset.format;
	const show = (x) => (el.textContent = fmt === "pct" ? pct(x) : int(x));
	if (reduceMotion()) return show(target);
	const t0 = performance.now(), dur = 1400;
	const step = (t) => {
		const k = Math.min(1, (t - t0) / dur);
		show(target * (1 - Math.pow(1 - k, 3)));
		if (k < 1) requestAnimationFrame(step);
	};
	requestAnimationFrame(step);
}

/** Wire up everything in root that animates on view: bars, stacks, per-fight columns, count-ups, reveals. */
export function hydrate(root) {
	for (const el of $$("[data-fill]", root)) {
		onVisible(el, (b) => requestAnimationFrame(() => {
			const i = b.firstElementChild;
			if (i) i.style.width = (parseFloat(b.dataset.fill) * 100).toFixed(2) + "%";
		}));
	}
	for (const el of $$(".stack", root)) {
		onVisible(el, (s) => requestAnimationFrame(() => {
			for (const seg of s.children) seg.style.width = (parseFloat(seg.dataset.w) * 100).toFixed(2) + "%";
		}));
	}
	for (const el of $$(".per-fight", root)) {
		onVisible(el, (s) => requestAnimationFrame(() => {
			for (const col of s.children) {
				const b = col.firstElementChild;
				if (b) b.style.height = (parseFloat(col.dataset.h) * 100).toFixed(1) + "%";
			}
		}));
	}
	for (const el of $$("[data-count]", root)) onVisible(el, animateCount);
	for (const el of $$(".reveal", root)) onVisible(el, (r) => r.classList.add("in"));
}

let toastTimer = 0;
export function toast(msg, isError = false) {
	const el = document.getElementById("toast");
	if (!el) return;
	el.textContent = msg;
	el.classList.toggle("err", !!isError);
	el.classList.add("show");
	clearTimeout(toastTimer);
	toastTimer = setTimeout(() => el.classList.remove("show"), 3600);
}

/** A confirm dialog; resolves true / false. `body` is h`` markup. */
export function confirmDialog({ title, body, confirm = "Confirm", cancel = "Cancel", danger = false }) {
	return new Promise((resolve) => {
		const prev = document.activeElement;
		const wrap = document.createElement("div");
		wrap.className = "modal";
		wrap.innerHTML = h`<div class="card ${danger ? "danger-zone" : ""}" role="dialog" aria-modal="true" aria-labelledby="dlg-title">
			<h2 id="dlg-title">${title}</h2>${body}
			<div class="btn-row"><button type="button" class="btn ghost small" data-act="no">${cancel}</button>
			<button type="button" class="btn small ${danger ? "danger" : ""}" data-act="yes">${confirm}</button></div></div>`.__raw;
		document.body.appendChild(wrap);
		const done = (v) => {
			wrap.remove();
			document.removeEventListener("keydown", onKey);
			if (prev && prev.focus) prev.focus();
			resolve(v);
		};
		const onKey = (e) => {
			if (e.key === "Escape") done(false);
			if (e.key === "Tab") { // keep focus inside
				const f = $$("button, input, a[href], textarea", wrap);
				if (!f.length) return;
				const first = f[0], last = f[f.length - 1];
				if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
				else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
			}
		};
		document.addEventListener("keydown", onKey);
		wrap.addEventListener("click", (e) => {
			if (e.target === wrap) done(false);
			const act = e.target.closest("[data-act]");
			if (act) done(act.dataset.act === "yes");
		});
		$('[data-act="no"]', wrap).focus();
	});
}

// ---- emblems (inline SVG, drawn in currentColor) -----------------------------------------------------------------------

function ninefold() {
	let spikes = "";
	for (let i = 0; i < 9; i++) {
		const a = (i / 9) * Math.PI * 2 - Math.PI / 2;
		const p = (r, da) => [50 + Math.cos(a + da) * r, 50 + Math.sin(a + da) * r].map((v) => v.toFixed(2)).join(",");
		spikes += `<polygon points="${p(30, -0.16)} ${p(46, 0)} ${p(30, 0.16)}" fill="currentColor" opacity=".9"/>`;
	}
	return spikes;
}

export const EMBLEMS = {
	// the Ninefold Warden: a crown of nine blades around a hooded void, a scythe through it
	warden: () => `<svg class="emblem" viewBox="0 0 100 100" aria-hidden="true">${ninefold()}
		<circle cx="50" cy="50" r="27" fill="none" stroke="currentColor" stroke-width="2.5"/>
		<path d="M50 30 C38 36 36 50 40 64 L50 70 L60 64 C64 50 62 36 50 30 Z" fill="currentColor" opacity=".35"/>
		<path d="M44 52 L48 55 M56 52 L52 55" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"/>
		<path d="M28 80 L70 22" stroke="currentColor" stroke-width="3"/><path d="M70 22 C82 24 88 34 86 44 C80 34 74 30 66 30 Z" fill="currentColor"/></svg>`,
	// the Monkey Sage: a staff across a cloud-swirl and a crescent
	sage: () => `<svg class="emblem" viewBox="0 0 100 100" aria-hidden="true">
		<circle cx="50" cy="50" r="40" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 5" opacity=".7"/>
		<path d="M30 58 C22 58 20 46 29 44 C30 36 42 34 46 41 C52 34 64 38 62 47 C70 46 74 56 66 59 Z" fill="none" stroke="currentColor" stroke-width="2.5"/>
		<path d="M64 20 A18 18 0 1 0 80 44 A14 14 0 1 1 64 20 Z" fill="currentColor" opacity=".85"/>
		<path d="M18 84 L82 16" stroke="currentColor" stroke-width="4.5" stroke-linecap="round"/>
		<path d="M18 84 L82 16" stroke="#120d0a" stroke-width="1.2" stroke-dasharray="1 7"/>
		<circle cx="22" cy="80" r="4" fill="currentColor"/><circle cx="78" cy="20" r="4" fill="currentColor"/></svg>`,
	// the Warden, Returned: a cracked stone mask with ember eyes
	returned: () => `<svg class="emblem" viewBox="0 0 100 100" aria-hidden="true">
		<polygon points="50,8 84,26 84,66 50,92 16,66 16,26" fill="currentColor" opacity=".16" stroke="currentColor" stroke-width="2.5"/>
		<polygon points="50,20 72,32 72,60 50,78 28,60 28,32" fill="none" stroke="currentColor" stroke-width="1.5" opacity=".6"/>
		<path d="M34 46 L46 50 L34 52 Z M66 46 L54 50 L66 52 Z" fill="currentColor"/>
		<path d="M50 8 L46 24 L54 34 L47 46 L52 58 L46 70 L50 92" fill="none" stroke="#0c0a09" stroke-width="2.5"/>
		<path d="M50 8 L46 24 L54 34 L47 46 L52 58 L46 70 L50 92" fill="none" stroke="currentColor" stroke-width=".8" opacity=".8"/></svg>`,
	unknown: () => `<svg class="emblem" viewBox="0 0 100 100" aria-hidden="true"><circle cx="50" cy="50" r="38" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="4 6"/><path d="M14 50 Q50 22 86 50 Q50 78 14 50 Z" fill="none" stroke="currentColor" stroke-width="2.5"/><circle cx="50" cy="50" r="8" fill="currentColor"/></svg>`,
};

/** The rank badge: a carved hexagonal seal with the tier's numeral. */
export function rankBadge(rank, size = "") {
	return h`<span class="rank rank-${rank.tier} ${size}" title="Rank ${rank.numeral}: ${rank.name}">${raw(`<svg viewBox="0 0 100 110" aria-hidden="true">
		<polygon points="50,4 94,28 94,82 50,106 6,82 6,28" fill="#0e0f13" stroke="currentColor" stroke-width="3"/>
		<polygon points="50,14 85,33 85,77 50,96 15,77 15,33" fill="none" stroke="currentColor" stroke-width="1.2" opacity=".55"/>
		${rank.tier >= 4 ? '<path d="M50 14 L56 24 L50 21 L44 24 Z M50 96 L56 86 L50 89 L44 86 Z" fill="currentColor"/>' : ""}
		${rank.tier >= 6 ? '<circle cx="50" cy="55" r="34" fill="none" stroke="currentColor" stroke-width=".8" stroke-dasharray="2 3"/>' : ""}
		<text x="50" y="${rank.numeral.length > 2 ? 64 : 67}" text-anchor="middle" font-family="Fraunces, Georgia, serif" font-style="italic" font-weight="600" font-size="${rank.numeral.length > 2 ? 30 : 38}" fill="currentColor">${rank.numeral}</text></svg>`)}${size === "sm" ? "" : h`<span class="rank-name">${rank.name}</span>`}</span>`;
}

// ---- the line chart (inline SVG, drawn at the container's pixel width so text and dots stay crisp) ----------------------

const SVG_NS = "http://www.w3.org/2000/svg";
function svgEl(tag, attrs, parent) {
	const e = document.createElementNS(SVG_NS, tag);
	for (const k of Object.keys(attrs)) e.setAttribute(k, attrs[k]);
	if (parent) parent.appendChild(e);
	return e;
}

/** Draw a 0..1 line chart into el (replacing its content); call again on resize.
 *  n points in order; series = [{cls, values: [y|null] (length n), dots, area, label}];
 *  breaks = indices that start a new group (a dashed rule before them; lines do not cross them);
 *  titles[i] = the tooltip of point i; xLabels = [{i, text}]. Colours come from the stylesheet (.chart .s-<cls>). */
export function lineChart(el, { n, series, breaks = [], titles = [], xLabels = [], height = 210 }) {
	const W = Math.max(260, Math.round(el.clientWidth || 600)), H = height;
	const L = 44, R = 14, T = 14, B = 28;
	const x = (i) => L + (n <= 1 ? (W - L - R) / 2 : (i * (W - L - R)) / (n - 1));
	const y = (v) => T + (1 - v) * (H - T - B);
	const svg = svgEl("svg", { class: "chart-svg", width: W, height: H, viewBox: `0 0 ${W} ${H}`, "aria-hidden": "true", focusable: "false" });
	for (const v of [0, 0.25, 0.5, 0.75, 1]) {
		svgEl("line", { class: v === 0 ? "axis" : "gridline", x1: L, x2: W - R, y1: y(v), y2: y(v) }, svg);
		if (v === 0 || v === 0.5 || v === 1) {
			const t = svgEl("text", { class: "tick", x: L - 8, y: y(v) + 3.5, "text-anchor": "end" }, svg);
			t.textContent = Math.round(v * 100) + "%";
		}
	}
	const brk = new Set(breaks);
	for (const i of breaks) if (i > 0 && i < n) svgEl("line", { class: "brk", x1: (x(i - 1) + x(i)) / 2, x2: (x(i - 1) + x(i)) / 2, y1: T - 4, y2: H - B }, svg);
	let lastX = -1e9;
	for (const { i, text } of xLabels) {
		const px = x(i);
		if (px - lastX < 64) continue;
		const t = svgEl("text", { class: "tick", x: Math.min(px, W - R - 24), y: H - 8, "text-anchor": i === 0 ? "start" : "middle" }, svg);
		t.textContent = text;
		lastX = px;
	}
	for (const s of series) {
		const g = svgEl("g", { class: "s-" + s.cls }, svg);
		// runs of consecutive values, cut at nulls and at group breaks
		const runs = [];
		let run = [];
		for (let i = 0; i < n; i++) {
			const v = s.values[i];
			if (brk.has(i) && run.length) { runs.push(run); run = []; }
			if (v === null || v === undefined || !isFinite(v)) { if (run.length) runs.push(run); run = []; continue; }
			run.push([x(i), y(Math.max(0, Math.min(1, v))), i]);
		}
		if (run.length) runs.push(run);
		for (const r of runs) {
			const d = r.map(([px, py], k) => (k ? "L" : "M") + px.toFixed(1) + " " + py.toFixed(1)).join(" ");
			if (s.area && r.length > 1) svgEl("path", { class: "area", d: `${d} L${r[r.length - 1][0].toFixed(1)} ${y(0)} L${r[0][0].toFixed(1)} ${y(0)} Z` }, g);
			if (r.length > 1) svgEl("path", { class: "line", d }, g);
			if (s.dots || r.length === 1) {
				for (const [px, py, i] of r) {
					const c = svgEl("circle", { class: "dot", cx: px.toFixed(1), cy: py.toFixed(1), r: s.dots ? 3.6 : 2.4 }, g);
					if (titles[i]) svgEl("title", {}, c).textContent = `${titles[i]} · ${s.label}: ${Math.round(s.values[i] * 100)}%`;
				}
			}
		}
	}
	el.replaceChildren(svg);
}
