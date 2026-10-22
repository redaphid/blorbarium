-- The server's memory of every badge and every creature it has met (#37), and
-- room for what comes next: life stats (#38), diaries (#39), packs ready at
-- hatching (#40), the admin dashboard and its per-badge action queue (#41),
-- personality views and edits from the phone (#42, #43), behaviour modifiers
-- (#44), nightly line revision, and analytics. D1 (SQLite).
--
-- Nothing is overwritten: a new generation or species on a badge is a new
-- creature row; an edit, a hatch or a nightly revision is a new personality
-- and pack version. Times are unix milliseconds. JSON columns hold JSON text.
--
-- Apply: npx wrangler d1 migrations apply blorbarium --local   (or --remote, with the owner's go-ahead)

-- A badge, by its MAC (lower-case hex, colon-free: "7cdfa1b2c3d4").
CREATE TABLE badges (
  mac          TEXT PRIMARY KEY,
  first_seen   INTEGER NOT NULL,
  last_seen    INTEGER NOT NULL,
  fw           TEXT,                       -- firmware version last reported
  label        TEXT,                       -- the owner's name for it in the dashboard ("Ada's badge")
  settings_json TEXT                       -- desired settings the dashboard last set (#41); the queue delivers them
);

-- A creature: one life, on one badge. Keyed by the badge and the genome (a
-- genome is fixed for one life), so a new egg, generation or species is a new row.
CREATE TABLE creatures (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  mac           TEXT NOT NULL REFERENCES badges(mac),
  genome_hash   TEXT NOT NULL,             -- sha-256 of the genome bytes
  genome_hex    TEXT NOT NULL,
  species       TEXT NOT NULL,
  generation    INTEGER,
  lineage_id    TEXT,                      -- the badge's lineage id, if it sends one
  parent_id     INTEGER REFERENCES creatures(id),  -- the previous life on this lineage, when known
  stage         TEXT,                      -- last reported: baby, child, adult, elder
  name          TEXT,                      -- the owner's name for the pet, if set
  feats         INTEGER NOT NULL DEFAULT 0,
  status        TEXT NOT NULL DEFAULT 'alive',     -- egg, alive, dead (#40 registers eggs)
  fw            TEXT,                      -- firmware when first seen
  raw_json      TEXT NOT NULL,             -- the decoded raw trait sheet (web/src/genome.js)
  traits_json   TEXT NOT NULL,             -- the trait sheet in words (web/src/traits.js)
  first_seen    INTEGER NOT NULL,
  last_seen     INTEGER NOT NULL,
  hatched_at    INTEGER,
  died_at       INTEGER,
  UNIQUE (mac, genome_hash)
);
CREATE INDEX creatures_by_lineage ON creatures (lineage_id, generation);
CREATE INDEX creatures_by_genome ON creatures (genome_hash);

-- A personality version: the character bible a pack is written from. v1 comes
-- from the genes; later versions from a hatch with the parents' stats and
-- diaries (#40), an owner's edit from the phone (#43), a nightly revision, or the admin.
CREATE TABLE personalities (
  id             INTEGER PRIMARY KEY AUTOINCREMENT,
  creature_id    INTEGER NOT NULL REFERENCES creatures(id),
  version        INTEGER NOT NULL,         -- 1, 2, ... per creature
  source         TEXT NOT NULL,            -- genes, hatch, edit, nightly, admin
  bible          TEXT NOT NULL,
  catchphrases_json TEXT,
  edited_by      TEXT,                     -- for an edit: who (phone, admin)
  inputs_json    TEXT,                     -- what fed it: stats and diary ids, the dice, the parent personalities
  modifiers_json TEXT,                     -- bounded behaviour modifiers derived from it (#44, LINEAGE.md)
  model          TEXT,
  prompt_version TEXT,
  created_at     INTEGER NOT NULL,
  UNIQUE (creature_id, version)
);

-- A line pack: what the badge stores and says. One per personality version
-- at least; regenerating from the same personality is a new pack version too.
CREATE TABLE packs (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  creature_id     INTEGER NOT NULL REFERENCES creatures(id),
  personality_id  INTEGER NOT NULL REFERENCES personalities(id),
  version         INTEGER NOT NULL,        -- 1, 2, ... per creature: the badge keeps the newest it has
  cache_key       TEXT NOT NULL,           -- genome_hash : prompt version : model, for reuse across badges
  lines_json      TEXT NOT NULL,           -- {situation: [lines]}
  fallback_json   TEXT NOT NULL,           -- situations left to the built-in lines
  provenance_json TEXT NOT NULL,           -- model, prompt, retries, first-pass rate, rejections, cost, timing
  sig             TEXT,                    -- Ed25519 over the canonical pack, when PACK_SIGNING_KEY is set
  status          TEXT NOT NULL DEFAULT 'ready',   -- ready, superseded, withdrawn
  pending_device  INTEGER NOT NULL DEFAULT 1,      -- 1 until the badge confirms it has it (the nightly fetch checks this)
  delivered_at    INTEGER,
  created_at      INTEGER NOT NULL,
  UNIQUE (creature_id, version)
);
CREATE INDEX packs_by_cache_key ON packs (cache_key);
CREATE INDEX packs_pending ON packs (pending_device, creature_id);

-- Generation work that runs off the request: a POST that asked not to wait,
-- an egg reported before hatching (#40), a nightly revision, an admin regenerate.
CREATE TABLE jobs (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  creature_id  INTEGER NOT NULL REFERENCES creatures(id),
  kind         TEXT NOT NULL,              -- lines, hatch, edit, nightly, admin
  input_json   TEXT,                       -- e.g. the edited bible (#43)
  status       TEXT NOT NULL DEFAULT 'queued',   -- queued, running, done, failed
  attempts     INTEGER NOT NULL DEFAULT 0,
  error        TEXT,
  pack_id      INTEGER REFERENCES packs(id),
  created_at   INTEGER NOT NULL,
  updated_at   INTEGER NOT NULL
);
CREATE INDEX jobs_by_status ON jobs (status, created_at);

-- Life stats a badge uploads when online (#38): counters for one life, and a
-- summary of the parents' lives. Versioned by the badge's stats format.
CREATE TABLE life_stats (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  mac           TEXT NOT NULL REFERENCES badges(mac),
  creature_id   INTEGER NOT NULL REFERENCES creatures(id),
  stats_version INTEGER NOT NULL,
  stats_json    TEXT NOT NULL,             -- feeds, knocks, fights won/lost, prophecies, starvation, sleep, carried time...
  parents_json  TEXT,
  uploaded_at   INTEGER NOT NULL
);
CREATE INDEX life_stats_by_creature ON life_stats (creature_id, uploaded_at);

-- One diary entry per pet per day, in his voice, from his new stats (#39 cron).
CREATE TABLE diary_entries (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  creature_id  INTEGER NOT NULL REFERENCES creatures(id),
  day          TEXT NOT NULL,              -- YYYY-MM-DD, the owner's day
  entry        TEXT NOT NULL,
  stats_id     INTEGER REFERENCES life_stats(id),
  model        TEXT,
  created_at   INTEGER NOT NULL,
  UNIQUE (creature_id, day)
);

-- The dashboard's per-badge action queue (#41): the badge pulls it in a Wi-Fi
-- window or through the phone, and reports each result.
CREATE TABLE actions (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  mac           TEXT NOT NULL REFERENCES badges(mac),
  batch_id      TEXT,                      -- one "all badges" or "some badges" order, fanned out per badge
  kind          TEXT NOT NULL,             -- line_pack, ota, settings, command
  payload_json  TEXT NOT NULL,             -- e.g. {pack_id}, {rollout_id}, {idle_timeout_s}, {verb, args}
  status        TEXT NOT NULL DEFAULT 'pending',  -- pending, delivered, done, failed, cancelled, expired
  created_by    TEXT NOT NULL,
  created_at    INTEGER NOT NULL,
  not_before    INTEGER,
  expires_at    INTEGER,
  delivered_at  INTEGER,
  delivered_via TEXT,                      -- wifi, phone
  completed_at  INTEGER,
  result_json   TEXT
);
CREATE INDEX actions_for_badge ON actions (mac, status, created_at);

-- OTA rollouts the dashboard targets (#41), over the existing signed manifest flow.
CREATE TABLE ota_rollouts (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  release      TEXT NOT NULL,              -- the release version / manifest path under /fw/
  target_json  TEXT NOT NULL,              -- "all" or a list of MACs
  state        TEXT NOT NULL DEFAULT 'active',   -- active, paused, killed
  created_by   TEXT NOT NULL,
  created_at   INTEGER NOT NULL,
  updated_at   INTEGER NOT NULL
);

-- Analytics a badge uploads with its stats: the raw payload, and the same
-- flattened to one row per metric so the dashboard can chart across badges and time.
CREATE TABLE analytics_uploads (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  mac          TEXT NOT NULL REFERENCES badges(mac),
  creature_id  INTEGER REFERENCES creatures(id),
  fw           TEXT,
  period_start INTEGER,
  period_end   INTEGER,
  payload_json TEXT NOT NULL,
  uploaded_at  INTEGER NOT NULL
);
CREATE TABLE analytics_metrics (
  upload_id    INTEGER NOT NULL REFERENCES analytics_uploads(id),
  mac          TEXT NOT NULL,
  ts           INTEGER NOT NULL,
  metric       TEXT NOT NULL,              -- duel.rounds, gesture.latency_ms.p50, fps.hist.30, wifi.connect_fail...
  dims_json    TEXT,                       -- {stance: "guard"}, {bucket: "30-40"}
  value        REAL NOT NULL
);
CREATE INDEX analytics_by_metric ON analytics_metrics (metric, ts);
CREATE INDEX analytics_by_badge ON analytics_metrics (mac, ts);

-- Who did what in the dashboard.
CREATE TABLE admin_audit (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  who          TEXT NOT NULL,
  what         TEXT NOT NULL,
  details_json TEXT,
  at           INTEGER NOT NULL
);
