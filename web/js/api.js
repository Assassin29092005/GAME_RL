// HELLWALKER site - the Firebase REST client (web/CONTRACT.md; no SDK). Public reads need no sign-in; writes and the
// private survey comment use the linked browser's id token, refreshed from the game's refresh token.
//
// Backend interface (also implemented by demo.js):
//   mode                       "live" | "mock" | "demo"
//   getPlayer(uid)             players/{uid} decoded, or null
//   getFights(uid, n)          the newest n fights of uid (runQuery player == uid order by at desc)
//   getPool()                  {players, capped}: the most active players (leaderboards, world stats)
//   linked()                   {uid} of this browser, or null
//   link(refreshToken)         exchange at securetoken, remember {uid, refreshToken}; returns uid
//   unlink()
//   rename(nickname) / getComment() / submitSurvey(likert, noticed, comment) / reset(onProgress)

export const POOL_LIMIT = 500;

export class ApiError extends Error {
	constructor(http, status, message) {
		super(message || status || "request failed");
		this.http = http;
		this.status = status;
	}
}

// ---- Firestore typed JSON ---------------------------------------------------------------------------------------------

export function decodeValue(v) {
	if (!v || typeof v !== "object") return null;
	if ("nullValue" in v) return null;
	if ("booleanValue" in v) return !!v.booleanValue;
	if ("integerValue" in v) return Number(v.integerValue);
	if ("doubleValue" in v) return Number(v.doubleValue);
	if ("timestampValue" in v) return new Date(v.timestampValue);
	if ("stringValue" in v) return v.stringValue;
	if ("mapValue" in v) return decodeFields(v.mapValue.fields || {});
	if ("arrayValue" in v) return (v.arrayValue.values || []).map(decodeValue);
	return null;
}

export function decodeFields(fields) {
	const out = {};
	for (const k of Object.keys(fields || {})) out[k] = decodeValue(fields[k]);
	return out;
}

export function decodeDoc(doc) {
	const data = decodeFields(doc.fields || {});
	data.id = doc.name.slice(doc.name.lastIndexOf("/") + 1);
	return data;
}

export const T = {
	int: (n) => ({ integerValue: String(Math.trunc(n)) }),
	dbl: (x) => ({ doubleValue: Number(x) }),
	str: (s) => ({ stringValue: String(s) }),
	bool: (b) => ({ booleanValue: !!b }),
	map: (o) => ({ mapValue: { fields: o } }),
	arr: (xs) => (xs.length ? { arrayValue: { values: xs } } : { arrayValue: {} }),
};

const INT_TOTALS = ["fights", "wins", "losses", "timeouts", "playerSwings", "playerHits", "keeperSwings", "keeperHits",
	"keeperBlocked", "keeperParried", "keeperWhiffed", "parryAttempts", "dodges", "readsLanded", "predictions",
	"predictionsCorrect", "confident", "confidentCorrect"];
const CLASS_KEYS = ["fast", "heavy", "feint", "killer"];
const ANSWER_KEYS = ["parry", "block", "stepL", "stepR", "stepB", "stepF", "attack", "none"];

/** The reset's zeroed maps (contract: totals, keepers and answers back to 0). */
export function zeroFields() {
	const totals = {};
	for (const k of INT_TOTALS) totals[k] = T.int(0);
	for (const k of ["seconds", "dmgDealt", "dmgTaken"]) totals[k] = T.dbl(0);
	const keepers = {};
	for (const k of ["warden", "sage", "returned"]) keepers[k] = T.map({ fights: T.int(0), wins: T.int(0) });
	const answers = {};
	for (const c of CLASS_KEYS) {
		const m = {};
		for (const a of ANSWER_KEYS) m[a] = T.int(0);
		answers[c] = T.map(m);
	}
	return { totals: T.map(totals), keepers: T.map(keepers), answers: T.map(answers), expected: T.arr([]) };
}

// ---- storage (per-browser convenience; never required) ------------------------------------------------------------------

function loadLink(key) {
	try {
		const v = JSON.parse(localStorage.getItem(key) || "null");
		return v && typeof v.uid === "string" && typeof v.refreshToken === "string" ? v : null;
	} catch {
		return null;
	}
}

function saveLink(key, v) {
	try {
		if (v) localStorage.setItem(key, JSON.stringify(v));
		else localStorage.removeItem(key);
	} catch {
		/* private mode: the link lasts for this page only */
	}
}

// ---- the remote backend -------------------------------------------------------------------------------------------------

export class RemoteBackend {
	constructor({ apiKey, projectId, mock }) {
		this.mode = mock ? "mock" : "live";
		this.apiKey = apiKey;
		this.projectId = projectId;
		const m = mock ? mock.replace(/\/+$/, "") : "";
		this.idt = mock ? m + "/identitytoolkit/v1" : "https://identitytoolkit.googleapis.com/v1";
		this.sts = mock ? m + "/securetoken/v1" : "https://securetoken.googleapis.com/v1";
		this.root = `projects/${projectId}/databases/(default)/documents`;
		this.fs = (mock ? m + "/firestore/v1/" : "https://firestore.googleapis.com/v1/") + this.root;
		// One link per backend: a ?mock= page can never read (and send off) the real project's refresh token.
		this.linkKey = `hw.link.v1|${this.mode}|${projectId}|${m}`;
		this.cacheTag = `${this.mode}|${projectId}|${m}`; // cached reads never mix a mock with the real project
		this.memLink = loadLink(this.linkKey); // kept in memory too, so a storage failure only shortens the link to this page
		this.idToken = null;
		this.idTokenExp = 0;
		this.poolCache = null;
	}

	key() {
		return "key=" + encodeURIComponent(this.apiKey);
	}

	async call(method, url, { body, form, token } = {}) {
		const headers = {};
		let data;
		if (form) {
			headers["Content-Type"] = "application/x-www-form-urlencoded";
			data = new URLSearchParams(form).toString();
		} else if (body !== undefined) {
			headers["Content-Type"] = "application/json";
			data = JSON.stringify(body);
		}
		if (token) headers.Authorization = "Bearer " + token;
		let res;
		try {
			res = await fetch(url, { method, headers, body: data, cache: "no-store" });
		} catch {
			throw new ApiError(0, "NETWORK", "Could not reach the server. Check your connection and try again.");
		}
		const text = await res.text();
		let json = null;
		try {
			json = text ? JSON.parse(text) : null;
		} catch {
			json = null;
		}
		if (!res.ok) {
			const err = (Array.isArray(json) ? json[0] && json[0].error : json && json.error) || {};
			throw new ApiError(res.status, err.status || String(res.status), err.message || text || res.statusText);
		}
		return json;
	}

	/** An id token for the linked uid (refreshed when older than ~55 min). */
	async token(force = false) {
		const link = this.memLink; // {uid, refreshToken}; linked() only exposes the uid
		if (!link) throw new ApiError(401, "NOT_LINKED", "This browser is not linked to a game.");
		if (!force && this.idToken && Date.now() < this.idTokenExp - 60000) return this.idToken;
		try {
			const r = await this.call("POST", `${this.sts}/token?${this.key()}`, { form: { grant_type: "refresh_token", refresh_token: link.refreshToken } });
			this.idToken = r.id_token;
			this.idTokenExp = Date.now() + (parseInt(r.expires_in, 10) || 3600) * 1000;
			if (r.refresh_token && r.refresh_token !== link.refreshToken) {
				this.memLink = { ...link, refreshToken: r.refresh_token };
				saveLink(this.linkKey, this.memLink);
			}
			return this.idToken;
		} catch (e) {
			// The refresh token itself is dead (INVALID_REFRESH_TOKEN, TOKEN_EXPIRED, USER_DISABLED, USER_NOT_FOUND):
			// forget it. Any other 400 (a bad API key in config.js) must not unlink the visitor.
			if (e.http === 400 && /TOKEN|USER_/.test(e.message || "")) {
				this.unlink();
				throw new ApiError(400, "LINK_EXPIRED", "This browser's link to the game no longer works. Open the stats page from the game again.");
			}
			throw e;
		}
	}

	/** A signed request; retries once with a fresh token on 401. */
	async authed(method, url, opts = {}) {
		try {
			return await this.call(method, url, { ...opts, token: await this.token() });
		} catch (e) {
			if (e.http !== 401) throw e;
			return this.call(method, url, { ...opts, token: await this.token(true) });
		}
	}

	async runQuery(q, auth = false) {
		const url = `${this.fs}:runQuery?${this.key()}`;
		const rows = auth ? await this.authed("POST", url, { body: { structuredQuery: q } }) : await this.call("POST", url, { body: { structuredQuery: q } });
		return (rows || []).filter((r) => r.document).map((r) => decodeDoc(r.document));
	}

	commit(writes) {
		return this.authed("POST", `${this.fs}:commit?${this.key()}`, { body: { writes } });
	}

	name(path) {
		return `${this.root}/${path}`;
	}

	// ---- reads ----
	async getPlayer(uid) {
		try {
			const doc = await this.call("GET", `${this.fs}/players/${encodeURIComponent(uid)}?${this.key()}`);
			return decodeDoc(doc);
		} catch (e) {
			if (e.http === 404) return null;
			throw e;
		}
	}

	getFights(uid, n = 20) {
		return this.runQuery({
			from: [{ collectionId: "fights" }],
			where: { fieldFilter: { field: { fieldPath: "player" }, op: "EQUAL", value: T.str(uid) } },
			orderBy: [{ field: { fieldPath: "at" }, direction: "DESCENDING" }],
			limit: n,
		});
	}

	/** The POOL_LIMIT most active players - everyone who could place on a leaderboard. Cached for five minutes, also
	 *  across reloads in this tab (sessionStorage): every player document read counts against Firestore's daily quota. */
	async getPool() {
		const ttl = 5 * 60000;
		if (this.poolCache && Date.now() - this.poolCache.at < ttl) return this.poolCache.value;
		const key = "hw.pool." + (this.cacheTag || "");
		try {
			const saved = JSON.parse(sessionStorage.getItem(key) || "null");
			if (saved && Date.now() - saved.at < ttl && Array.isArray(saved.value?.players)) {
				this.poolCache = saved;
				return saved.value;
			}
		} catch (e) { /* storage blocked or corrupt: read afresh */ }
		const players = await this.runQuery({
			from: [{ collectionId: "players" }],
			orderBy: [{ field: { fieldPath: "totals.fights" }, direction: "DESCENDING" }],
			limit: POOL_LIMIT,
		});
		const value = { players, capped: players.length >= POOL_LIMIT };
		this.poolCache = { at: Date.now(), value };
		try { sessionStorage.setItem(key, JSON.stringify(this.poolCache)); } catch (e) { /* full or blocked: memory only */ }
		return value;
	}

	// ---- the linked browser ----
	linked() {
		return this.memLink ? { uid: this.memLink.uid } : null;
	}

	async link(refreshToken) {
		const r = await this.call("POST", `${this.sts}/token?${this.key()}`, { form: { grant_type: "refresh_token", refresh_token: refreshToken } });
		this.memLink = { uid: r.user_id, refreshToken: r.refresh_token || refreshToken, linkedAt: new Date().toISOString() };
		saveLink(this.linkKey, this.memLink);
		this.idToken = r.id_token;
		this.idTokenExp = Date.now() + (parseInt(r.expires_in, 10) || 3600) * 1000;
		return r.user_id;
	}

	unlink() {
		this.memLink = null;
		this.idToken = null;
		saveLink(this.linkKey, null);
	}

	async rename(nickname) {
		const { uid } = this.linked();
		await this.authed("PATCH", `${this.fs}/players/${encodeURIComponent(uid)}?updateMask.fieldPaths=nickname&${this.key()}`,
			{ body: { fields: { nickname: T.str(nickname) } } });
		this.poolCache = null;
	}

	async getComment() {
		const { uid } = this.linked();
		try {
			const doc = await this.authed("GET", `${this.fs}/surveys/${encodeURIComponent(uid)}?${this.key()}`);
			return decodeDoc(doc).comment || "";
		} catch (e) {
			if (e.http === 404) return null;
			throw e;
		}
	}

	/** Likert answers -> players/{uid}.survey (public), the comment -> surveys/{uid} (private); one commit. */
	async submitSurvey(likert, noticed, comment) {
		const { uid } = this.linked();
		const fields = {};
		for (const k of ["feltRead", "fair", "difficulty", "fun", "playAgain"]) fields[k] = T.int(likert[k]);
		fields.noticedAdapting = T.bool(noticed);
		await this.commit([
			{ update: { name: this.name("players/" + uid), fields: { survey: T.map(fields) } },
				updateMask: { fieldPaths: ["survey.feltRead", "survey.fair", "survey.difficulty", "survey.fun", "survey.playAgain", "survey.noticedAdapting"] },
				currentDocument: { exists: true },
				updateTransforms: [{ fieldPath: "survey.at", setToServerValue: "REQUEST_TIME" }] },
			{ update: { name: this.name("surveys/" + uid), fields: { v: T.int(1), comment: T.str(comment || "") } },
				updateTransforms: [{ fieldPath: "at", setToServerValue: "REQUEST_TIME" }] },
		]);
	}

	/** The contract's reset: delete every fight of uid (pages of <= 50: real Firestore evaluates at most 1,000 rule
	 *  expressions per request), then zero the player (resets + 1, resetAt now). */
	async reset(onProgress = () => {}) {
		const { uid } = this.linked();
		let deleted = 0;
		for (let guard = 0; guard < 1000; guard++) {
			const page = await this.runQuery({
				from: [{ collectionId: "fights" }],
				where: { fieldFilter: { field: { fieldPath: "player" }, op: "EQUAL", value: T.str(uid) } },
				limit: 50,
			});
			if (!page.length) break;
			await this.commit(page.map((f) => ({ delete: this.name("fights/" + f.id) })));
			deleted += page.length;
			onProgress(deleted);
		}
		await this.commit([{
			update: { name: this.name("players/" + uid), fields: zeroFields() },
			updateMask: { fieldPaths: ["totals", "keepers", "answers", "expected"] },
			currentDocument: { exists: true },
			updateTransforms: [{ fieldPath: "resets", increment: T.int(1) }, { fieldPath: "resetAt", setToServerValue: "REQUEST_TIME" }],
		}]);
		this.poolCache = null;
		return deleted;
	}
}
