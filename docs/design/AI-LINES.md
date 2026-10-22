# AI personality lines

Issue #37. When a pet first reaches the internet, the server reads its genes and writes it its own lines with Workers AI. After that, the badge says them offline for the rest of the pet's life. This document covers the server (built, not deployed) and the badge (design only; it lands after Wi-Fi, #26 and #27).

The eval that chose the model and prompt is in `docs/ai-lines/EVAL.md`. The sample sheet for spot checks is `docs/ai-lines/samples.html`.

## The server (web/, built)

### From genes to words

The badge sends its genome bytes. The server reads them itself and does not trust a description sent by the device.

- **`web/src/genome.js`** decodes the genome as the engine does. It parses the gene chain and applies stage, feat and dormancy gating. It takes the first species, power and knack genes in order and the last temperament, size, metabolism and egg genes. It also reads stimulus responses, palette tints, marks, the startle receptor's gain, the hunger and melatonin emitters, and heirloom instincts. **`tools/trait_sheet.cpp`** dumps the same fields through the engine's own code (`expressStage`, the phenotype and gene views). `web/test/traits.test.mjs` holds the JavaScript to that output for every field of the 20 fixture genomes.
- **`web/src/traits.js`** turns the raw facts into words relative to the pet's own species' starter at the same stage (`web/src/baselines.json`, also from `trait_sheet`). So the sheet says what makes this mossby himself, for example "far bolder than most of his kind", "a hybrid: a gilly body carrying the zap power that fizzs are born with", "secretly LOVES being shaken", "born knowing an ancestor's lesson: when cradled, rest to ease his fear", or "no sleepy chemistry when it gets dark". A chemical with no name says nothing.
- **The genome dice** (prompt v2) are four fixed facts drawn from the genome hash: a treasure, a pet peeve, a hobby and a flaw. Two pets with near-identical genes still get different jokes, and the same genome always gets the same dice.

### The two calls

1. **Bible.** The species' character notes (`personalities/<species>.txt` above the LINES marker), the trait sheet and the dice go in. A character bible of about 120 words and three catchphrases come out.
2. **Lines.** The bible, the catchphrases, the trait sheet, a few of the species' own lines as a taste of the voice, and the situations go in. The situations are every `thoughts.def` row except the parrot and the four teaching rows, plus the three shake answers from #34, 132 in all. Each comes with its meaning (the pet-personality skill's `situations.md`), its mark (`#` or `@`, only where the row gives one) and its built-in line as an example. JSON `{situation: [2 to 4 lines]}` comes out. The 132 situations go out as four batches in parallel.

The prompt asks for lines that make a kid of 6 to 13 laugh, with a layer for parents, in the pet's own voice and using its genes. That is direction only; nothing checks it.

### What is checked (mechanical only, the owner's rule)

`web/src/validate.js`:

- **structure**: the reply is a JSON object of situation to list of strings. A key with a stray mark (`"p_mud [#]"`) is matched to its situation, and a key that was not asked for is reported back to the model;
- **coverage**: every situation asked for has lines;
- **length**: at most `kMaxThoughtText` (30) characters, counting the raw text;
- **font**: only `A-Z 0-9`, space and `. , ! ? - ' :`, plus `#` or `@` (one) only in a situation that gives it. Case and typographic quotes are normalised first, not failed;
- **no repeats**: a line repeating another line of this pet, or any built-in line (shared or the species' own), is dropped.

A batch whose lines fail is asked again in the same conversation, with each failure and its reason and only the situations still short. At most two retries follow the first attempt. A situation with no usable line after that is left out of the pack, listed in `fallback`, and the badge says its built-in line there. A busy model (HTTP 429, capacity) or a 5xx is the platform rather than the model's answer, so it is retried with backoff and does not count against the two. There is no moderation call, banned-word list or hedge rule; the owner ruled them out.

### The endpoint

`web/src/app.js` (entry `worker.js`) on the same Worker as the site. Only `/api/*` reaches the code (`run_worker_first`), so the static site is served as before.

| Route | What it does |
|---|---|
| `POST /api/pet-lines` `{mac, genome, stage?, name?, generation?, lineage?, feats?, fw?, egg?, wait?}` | The newest pack for this creature (200, `cached: true`). Otherwise it writes one and returns it (200). With `wait: false`, or if the model is down, it queues the work (202 with `retry-after`). |
| `GET /api/pet-lines?mac=&genome_hash=` | The newest pack, 202 while it is being written, 404 if unknown. |
| `POST /api/pet-lines/ack` `{mac, genome_hash, version}` | The badge has stored it, which clears `pending_device`. |
| `GET /api/creatures[?mac=]` | Everything the server holds, as JSON (`Authorization: Bearer ADMIN_TOKEN`). |
| cron, every 10 minutes | Writes queued packs (and later, diaries and nightly revisions). |

A reply carries `bible`, `catchphrases`, `lines`, `fallback`, `version`, `model`, `prompt`, `genome_hash`, `text` (the pack file the badge stores, below) and `sig`.

**Caching and limits.** A creature is identified by its badge MAC and genome hash, and its newest ready pack is returned without a model call. The same genome on another badge copies that pack (cache key: genome hash, prompt version and model). Requests are rate-limited per MAC by the Workers rate-limiting binding (6 a minute), and a badge gets at most `MAX_GENERATIONS_PER_DAY` (6) new generations a day.

**Signing.** With the `PACK_SIGNING_KEY` secret set (a P-256 key, PKCS#8 base64), `sig` is ECDSA P-256 over SHA-256 of `text`, raw `r || s`. That is the primitive the badge already verifies for OTA images (`lib/ota` `verifyP256`), with a separate key whose public half goes in the firmware.

### What the server keeps (D1, `web/migrations/0001_creatures.sql`)

Nothing is overwritten.

- **badges**: one row per MAC, with first and last seen, firmware, the owner's label and desired settings.
- **creatures**: one row per life on a badge (MAC plus genome hash). It holds species, generation, lineage, the parent creature, stage, name, feats, status (egg, alive or dead), the genome, the raw trait sheet and the words, and timestamps. A new generation or species is a new row.
- **personalities**: versioned per creature. Each version records its source (genes, hatch, edit, nightly, admin), the bible, the catchphrases, its inputs, the behaviour modifiers (#44), the model and the prompt.
- **packs**: versioned per creature. Each holds the lines, the fallback, the provenance (model, prompt, retries, the first-pass rate, every rejected line with its reason, tokens, cost and time), the signature, a status, and `pending_device` with its delivery time.
- **jobs**: generation that runs off the request.
- Room for what is queued behind #37: **life_stats** (#38), **diary_entries** (#39), the dashboard's per-badge **actions** queue with delivery and result status (#41), **ota_rollouts** (#41), **analytics_uploads** with one **analytics_metrics** row per metric for charts (analytics), and **admin_audit**.

To export everything: `GET /api/creatures` with the admin token, or `npx wrangler d1 export blorbarium --remote --output blorbarium.sql`.

### Tests

Run `node --test web/test/`. `traits.test.mjs` checks the decoder against the engine. `pet_lines.test.mjs` runs the API end to end, on the real schema in node's SQLite with a scripted model. It covers the cache, retries, a new generation as a new creature, the cross-badge cache, the queue and cron, an outage, the daily cap, bad input, the export, the checks themselves, and signatures.

After a change to `thoughts.def`, `personalities/` or the genes, regenerate the data the Worker embeds:

    python3 tools/ai_lines_catalog.py                 # web/src/catalog.json (--check: stale?)
    bash scripts/test/ai_lines/build_trait_sheet.sh   # baselines.json and the fixtures, through the engine

### Deploying (the owner's go-ahead first; none of this has been done)

1. `cd web && npx wrangler d1 create blorbarium`, then put the printed `database_id` into `wrangler.jsonc`. Recent Wrangler can instead provision the database on deploy.
2. `npx wrangler d1 migrations apply blorbarium --remote`.
3. `npx wrangler secret put ADMIN_TOKEN`. Optionally `npx wrangler secret put PACK_SIGNING_KEY`, a P-256 PKCS#8 key in base64 that never goes in the repo. The badge can only check signatures once its firmware holds the public key.
4. `npx wrangler deploy`. This also deploys the site, now through a Worker with `main`; static paths behave as before. The account is already on Workers Paid, which Kimi K2.6 requires.
5. Smoke test: `curl -X POST https://blorb.hypnodroid.com/api/pet-lines -d '{"mac":"…","genome":"…"}'`.

To try it without touching production, run `npx wrangler dev` (local D1, real Workers AI) after `npx wrangler d1 migrations apply blorbarium --local`.

### Built on the same Worker since

| Issue | Route or job | What it does |
|---|---|---|
| #38, #50 | `POST /api/stats` `{mac, genome_hash, stats_version, stats, parents, analytics, fw}` | Stores a pet's life stats (versioned, with the parents' summary). Analytics are stored raw and flattened into one row per metric for charts. |
| #39 | cron `0 4 * * *` | Writes a 50 to 90 word diary entry, in the pet's voice, for each pet with new stats. It uses the counters that changed, his bible and his last entry. `GET /api/diary?mac=&genome_hash=` returns the entries. |
| #40 | `POST /api/pet-lines` with `egg: true, wait: false, parent_genome_hash, parents` | Registers the egg and links its parent. A `hatch` job writes its bible and lines from its genes, the parent's bible, stats and diary, and what the egg carries. |
| #47 | the same nightly cron, after the diaries | Rewrites a pet whose life has moved on: a new stage, fights or hunger since, or a week of diary. The new pack version is queued for the badge. |
| #41, #49 | `/admin` (page) and `/api/admin/*` (ADMIN_TOKEN) | The dashboard. It covers badges, creatures by lineage, personality versions with edit and rewrite, packs (push any version), settings and commands for one badge or all, OTA rollouts (pause or kill), and analytics charts. The badge pulls its queue from `GET /api/badge/actions?mac=&via=` and reports each result to `POST /api/badge/actions/:id`. |
| #42, #43 | `GET /api/pet?genome_hash=`, `POST /api/personality` `{genome, bible}` | The phone (Web Bluetooth never shows the MAC, so it uses the genome it reads) shows the pet's personality, a few lines, the journal and the family. It can save the owner's own words for him, which rewrites his lines (call 1 skipped) and queues the pack. |

## The badge (design; after Wi-Fi #26/#27)

**When.** The pack is fetched in the first successful Wi-Fi window after the creature exists, and again only when the server flags a newer version. In a Wi-Fi window the badge does the following:

1. `POST /api/pet-lines` with `{mac, genome, stage, name, generation, lineage, feats, fw, wait: false}`. A 200 carries the pack. A 202 means the badge asks again with `GET` in the next window (or after `retry-after`, if the window is still open). The 2 to 3 minutes a fresh pack takes to write never hold a window open.
2. It verifies the pack before storing it: `sig` against the pack public key in firmware, and the header's genome-hash prefix against its own genome. It parses every line again with the badge's own rules (the font, `kMaxThoughtText`, marks only where the row gives one, a situation name it knows). A line that fails is dropped and the rest is kept.
3. It writes the text atomically to petfs as **`pack.lines`**, beside `taught.lines` and like it: kept across reboots, never in the keepsake, its hash or the lineage. Then it sends `POST /api/pet-lines/ack`.

**The format** is the server's `text`, one line per spoken line, sorted by situation:

    blorbpack1 <genome hash, first 16 hex> <version> <count>
    peckish	EMPTY TANK! FUEL NOW!
    peckish	LAP 47: STOMACH EVENT
    ...

The size budget is 132 situations at about 3 lines of up to 30 characters, plus the names: about 10 to 14 KB, under a 16 KB cap. At load, the badge keeps an index of offsets per situation (132 × 2 bytes) and reads lines from flash when it says one, so RAM stays at about 300 bytes. Fully loaded, the strings would be about 10 KB.

**Saying them.** `thoughtLine()` looks in order at:

1. the pack, if `pack.lines` exists, matches this creature's genome and has lines for the row;
2. then the species' own lines (`species_lines.def`);
3. then the voiced and shared lines with their frame.

Pack lines are said exactly as written, like a species' own lines after #34: no voice frame and no terse cut. `#` and `@` are filled in as now. The draw among a row's lines uses the same `pick`. The prophecy draw and the 8-Ball's verdicts (`verdict_yes`, `verdict_no` and `verdict_later`) prefer the pack in the same way. Teaching rows and the parrot never come from a pack.

**Offline for good.** After the first fetch the badge needs nothing from the server. Without a pack, or if it is torn, unsigned or for another genome, every situation uses the built-in lines exactly as today. A situation the pack lacks (the server's `fallback`) also uses the built-in line.

**A new generation or species.** A pack belongs to one genome. When a new egg hatches, the old `pack.lines` stops matching (by the genome-hash prefix) and is ignored, then deleted at the next successful fetch for the new creature. The new pet speaks its built-in lines until its own pack arrives. With #40 that pack is written while the egg incubates, so it is usually ready at hatching. A friend egg's hybrid is just another genome. A changed species changes everything the server reads, so it gets its own pack.

**Newer versions.** An edit from the phone (#43), a nightly revision (#47) or the dashboard (#41) makes a new pack version, sets `pending_device`, and queues a `line_pack` action for the badge. Once a night, in a normal Wi-Fi window, the badge does the following (offline-first rules unchanged, nothing on the frame loop):

1. `POST /api/stats` with its counters and analytics (#38, #50).
2. `GET /api/badge/actions?mac=&via=wifi`. For a `line_pack` action it fetches `GET /api/pet-lines?mac=&genome_hash=&version=`, verifies, replaces and acknowledges. An `ota` action goes through the existing signed `ota::Session` path; `settings` and `command` are applied if allowed.
3. `POST /api/badge/actions/:id` with `{mac, ok, result}` for each action.

The phone can also deliver a pack over BLE, as the same signed text with the same checks. That needs a new firmware verb (for example `PACK` in chunks, as `GENOME` is read), which does not exist yet.
