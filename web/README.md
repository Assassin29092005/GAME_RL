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

## Before distributing a new build: publish the rules first

The rules check every fight's schema, so a game version that sends a new schema needs the new rules **published before
anyone gets the build**. Game 1.4.0 sends **v2** fights (`web/CONTRACT.md`: five new fields — assist, slowmoScale,
keeperDamageScale, parryWindowFrames, insight). Before you push a 1.4.0 build to itch.io or hand it to anyone:

1. Firebase console → **Firestore Database → Rules** → replace everything with the contents of
   `web/firebase/firestore.rules` → **Publish**. (Or, with Node, from `web/firebase/`:
   `npx firebase-tools deploy --only firestore:rules --project <your-project-id>`.)
2. Check it: play one duel with the new build and see it on your profile (Firestore → Data → `fights` → the newest
   document has `v` = 2).

With the old rules every v2 fight is refused: the game retries it, then drops it after the third refusal (its log says
"refused by the server … three times; dropped") — those fights are lost for good. The v2 rules still accept v1 fights,
so copies of an older version keep uploading, and their offline queues still get through.

## 3. The download: GitHub Releases (in use)

The game is downloaded from the repository's **Releases** page (free, no account needed to download, no bandwidth cap).
GitHub limits each release file to 2 GiB, so the 7.7 GB game goes up in parts with a script that joins them:

1. `Tools\Package.bat` (the Shipping build), then `Tools\MakeRelease.bat`: `Build\Packaged\Release` gets
   `HellwalkerRL-<version>-Windows.zip.001 …` (each under 2 GiB), `Join-and-Extract.bat` (joins the parts with `copy /b` and
   unpacks with Windows' own `tar`; 7-Zip users can open the `.001` part instead) and `SHA256SUMS.txt`.
2. With the GitHub CLI signed in once (`gh auth login`), publish them:

   ```bash
   gh release create v1.4.0 Build/Packaged/Release/* --title "Hellwalker 1.4.0" --notes-file <release notes .md>
   ```

   (For a 7.7 GB upload, create it with `--draft`, upload with `gh release upload … --clobber` — re-run a part that
   failed — and publish with `gh release edit v1.4.0 --draft=false --latest` once every part is there.)
3. `web/config.js` `downloadUrl` = `https://github.com/<you>/<repo>/releases/latest`: the site's Download page links there
   and shows the join-and-extract steps. Players need about 16 GB free while unpacking; Windows warns once that the game
   is unsigned (More info → Run anyway).

Publish the Firestore rules for a new contract version **before** the release (above).

## 3b. itch.io (an alternative download)

The packaged game is ~7.4 GB. itch.io caps upload size by default (on the order of 1 GB per upload); a build this size
needs the limit raised — write to itch.io support before the first push, and allow a few days.

1. https://itch.io → **Upload new project**: kind *Downloadable*, classification *Game*, platform *Windows*; set the page
   to *Draft* until you are ready. Note its address (e.g. `https://yourname.itch.io/hellwalker`).
2. Install **butler** (itch.io's uploader: https://itch.io/docs/butler/) and log in once: `butler login`.
3. Build the game (`Tools\Package.bat`), publish the rules if this version changed them (the section above — 1.4.0
   does), then upload:

   ```bash
   Tools\ItchPush.bat yourname hellwalker
   ```

   (Pushes `Build\Packaged\Windows` to the `windows` channel; later pushes upload only what changed.)
4. Put the page address in `web/config.js` (`downloadUrl`, or `itchUrl` when `downloadUrl` is empty), push, and publish the
   itch.io page.

## 4. The research data

- **Export** players (with the survey's 1-5 answers) and fights to CSV — reading is public, so only the project id and
  key are needed:

  ```bash
  RL/.venv/Scripts/python.exe web/dev/export.py --project <project-id> --api-key <web-api-key> --out D:/study/export
  ```

  `fights.csv` holds both fight schemas (column `v`): a v1 fight (games before 1.4.0) fills the v2 columns with what
  it implied — assist `off`, slowmoScale 1, keeperDamageScale 1, parryWindowFrames 12 — and leaves `insight` blank
  (unknown, not 0). `seconds` is simulated fight time (slow motion does not lengthen it); `parryRate` = keeperParried /
  parryAttempts per fight.

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

- **Only the packaged (Shipping) game uploads to your real project.** Development builds — the editor, `Tools\Play.bat`,
  scripted checks — keep stats sharing off so your own test fights never land in the research data ("Open my stats page"
  says so). To try the real upload from a development build on purpose, add `-HWTelemetryDev` to its command line. Launches
  that change the keeper or the rules (`-HWPolicy=`, `-HWMoveAccel=`, `-HWMoveBraking=`, `-HWNoHitstop`) never upload.

- `web/dev/e2e_test.py` — 109 checks against the mock (108 with `--seed 0`): sign-up, uploads (incl. an idempotent
  retry), profile and leaderboard queries, linking a browser, reset, the fight schema versions (v2 fights with their
  five fields, a v1 fight from an older build still accepted, the v2 ranges at their edges), and the attacks the rules
  must refuse (another player deleting, overwriting or inflating someone's data; malformed or negative records, v2
  fields out of range or under the wrong version). Run it with `--spawn` after changing the mock: without it, it reuses
  whatever mock is already on port 8099.
- The mock enforces the same permissions as `web/firebase/firestore.rules`, but it is not Firestore: after publishing
  the rules, play one duel and check the profile appears, and try a reset.
