# The HellwalkerRL website

The website where people download the game and see what it collected about them: their skills (parry success, win
rate, accuracy, dodges, damage) and what the RL keeper learned about them (the keeper's notebook). There is no login of
any kind. It exists for the research paper's results section — how players fare against the keeper and how they feel
about it (the short survey on the "My stats" page).

```
web/                    the site: index.html, styles.css, js/ (no build step — plain HTML, CSS and ES modules)
web/config.js           the five settings below (empty = demo mode with generated players)
web/CONTRACT.md         the data contract the game, the site, the rules and the mock all follow
web/firebase/           Firestore rules, the index, firebase.json
web/dev/                local tools: mock_firebase.py, e2e_test.py, serve.py, export.py (not published)
render.yaml             the Render Blueprint (repo root)
Tools/ItchPush.bat      uploads the packaged game to itch.io with butler
```

How it works: each copy of the game signs in to Firebase **anonymously** (invisible to the player: no window, no
account), gets a random id and sends one record per duel. Anyone can view any profile (anonymous numbers and a made-up
nickname). "Open my stats page" in the game's pause or title menu opens `#/me` linked to that copy of the game; only a
linked browser can change the nickname, answer the survey or **reset** ("forget everything it learned about me" —
deletes that player's fights and zeroes their totals; the nickname and survey stay). Until Firebase is set up the game
sends nothing and the site shows demo data.

## Try it locally (no accounts needed)

```bash
RL/.venv/Scripts/python.exe web/dev/serve.py --port 8000
```

Open http://127.0.0.1:8000 — demo mode. To try the real data path on your machine, start the mock in a second terminal
and keep it running, then run its test (it seeds eight sample players and prints links to their profiles and a linked
"my stats" page, which work while the mock runs):

```bash
RL/.venv/Scripts/python.exe web/dev/mock_firebase.py
```

```bash
RL/.venv/Scripts/python.exe web/dev/e2e_test.py
```

The game can talk to the mock too: `-HWTelemetryEndpoint=http://127.0.0.1:8099` (see the uploader in Source/).

## 1. Firebase (the data) — free Spark plan

1. Go to https://console.firebase.google.com → **Add project** (e.g. `hellwalker-research`). Google Analytics is not
   needed.
2. **Build → Authentication → Get started → Sign-in method → Anonymous → Enable.** (This is the invisible sign-in; no
   other provider is needed.)
3. **Build → Firestore Database → Create database** → *production mode* → a location near your players (it cannot be
   changed later).
4. Publish the rules: **Firestore → Rules**, replace everything with the contents of `web/firebase/firestore.rules`,
   **Publish**.
5. Create the index: **Firestore → Indexes → Composite → Add index**: collection `fights`, fields `player` Ascending,
   `at` Descending, query scope Collection. (Or, with Node installed, from `web/firebase/`:
   `npx firebase-tools login` then `npx firebase-tools deploy --only firestore --project <your-project-id>` publishes
   both the rules and the index.)
6. **Project settings (gear) → General**: copy the **Web API key** and the **Project ID**. In the Google Cloud console
   (APIs & Services → Credentials → that key) restrict the key to three APIs: the *Identity Toolkit API* (sign-in), the
   *Token Service API* (`securetoken.googleapis.com`, refreshing a sign-in — without it uploads stop after the first
   hour and "my stats" links fail) and the *Cloud Firestore API*. The key is not a secret — the rules are the
   security — but restricting it limits misuse.
7. Put the two values in **both** places:
   - `web/config.js`: `apiKey`, `projectId`
   - `Config/DefaultGame.ini`, section `[HWTelemetry]`: `ApiKey=`, `ProjectId=`, and `SiteUrl=` (step 2's address)
   Then rebuild/repackage the game (`Tools\Package.bat`) so downloads carry them.

## 2. Render (the website)

1. Push this repo to GitHub (the site lives in `web/`).
2. https://dashboard.render.com → **New → Blueprint** → connect the GitHub repo → Render reads `render.yaml` and creates
   the static site `hellwalker-site` (no build step; only changes under `web/` redeploy it).
3. Its address (e.g. `https://hellwalker-site.onrender.com`) goes into `web/config.js` `siteUrl` and the game's
   `[HWTelemetry] SiteUrl=`. Push again.
4. Optional: a custom domain in the service's settings.

## 3. itch.io (the download)

The packaged game is ~7.4 GB. itch.io caps upload size by default (on the order of 1 GB per upload); a build this size
needs the limit raised — write to itch.io support before the first push, and allow a few days.

1. https://itch.io → **Upload new project**: kind *Downloadable*, classification *Game*, platform *Windows*; set the page
   to *Draft* until you are ready. Note its address (e.g. `https://yourname.itch.io/hellwalker`).
2. Install **butler** (itch.io's uploader: https://itch.io/docs/butler/) and log in once: `butler login`.
3. Build the game (`Tools\Package.bat`), then upload:

   ```bash
   Tools\ItchPush.bat yourname hellwalker
   ```

   (Pushes `Build\Packaged\Windows` to the `windows` channel; later pushes upload only what changed.)
4. Put the page address in `web/config.js` `itchUrl`, push, and publish the itch.io page.

## 4. The research data

- **Export** players (with the survey's 1-5 answers) and fights to CSV — reading is public, so only the project id and
  key are needed:

  ```bash
  RL/.venv/Scripts/python.exe web/dev/export.py --project <project-id> --api-key <web-api-key> --out D:/study/export
  ```

- The survey's free-text comments are private (`surveys/{uid}`, readable only by that player): read them in the Firebase
  console (Firestore → Data → `surveys`).
- What is collected is listed on the site's Research page and in `web/CONTRACT.md`: gameplay numbers per duel, the
  keeper's predictions about the player, an anonymous id, a made-up nickname — no names, emails or device data.
  Fights played by the game's autoplay bot or the tools are never uploaded.
- Ethics: a study with human participants usually needs informed consent under your institution's rules. The game shows
  a notice that anonymous stats are sent for research and the site explains it; if your ethics board needs explicit
  consent, add it before you publish.
- Anyone can reset their own data from the site; a reset deletes their fights (they are gone from the export too). Fights
  that copy of the game had queued offline before the reset are dropped by the game when it next comes online.
- Quotas: the free Spark plan allows 50,000 document reads a day. A visit to the home page or the leaderboard reads up to
  500 player documents (cached for five minutes per browser tab), so with 100 players that is about 500 fresh visits a
  day. A research study rarely gets near it; if yours does, switch the project to the pay-as-you-go Blaze plan (the
  cost at this scale is cents).

## Testing and safety checks

- `web/dev/e2e_test.py` — 82 checks against the mock: sign-up, uploads (incl. an idempotent retry), profile and
  leaderboard queries, linking a browser, reset, and the attacks the rules must refuse (another player deleting,
  overwriting or inflating someone's data; malformed or negative records).
- The mock enforces the same permissions as `web/firebase/firestore.rules`, but it is not Firestore: after publishing
  the rules, play one duel and check the profile appears, and try a reset.
