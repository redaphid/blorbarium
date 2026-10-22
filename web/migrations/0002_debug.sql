-- Debug and history (the owner: "store the old lines and other debug
-- information in Cloudflare"). Nothing here is deleted unless RETENTION_DAYS
-- is set (it is not, by default). Large bodies go to the R2 bucket bound as
-- DEBUG and are referenced by key; small ones stay inline.

-- Which pack each badge actually installed, and when (from /api/pet-lines/ack
-- and from a line_pack action's result).
CREATE TABLE installs (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  mac          TEXT NOT NULL,
  creature_id  INTEGER NOT NULL REFERENCES creatures(id),
  pack_id      INTEGER REFERENCES packs(id),
  version      INTEGER NOT NULL,
  via          TEXT,                       -- wifi, phone, action
  installed_at INTEGER NOT NULL
);
CREATE INDEX installs_by_creature ON installs (creature_id, installed_at);
CREATE INDEX installs_by_badge ON installs (mac, installed_at);

-- A generation's raw trail: every model reply before validation, what the
-- checks made of it, and the error if it failed.
CREATE TABLE gen_debug (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  creature_id  INTEGER REFERENCES creatures(id),
  pack_id      INTEGER REFERENCES packs(id),
  source       TEXT,                       -- genes, hatch, edit, nightly, admin
  ok           INTEGER NOT NULL,           -- 0: it failed (error says why)
  error        TEXT,
  model        TEXT,
  prompt       TEXT,
  calls        INTEGER,
  ms           INTEGER,
  usd          REAL,
  size         INTEGER NOT NULL,           -- bytes of the body
  r2_key       TEXT,                       -- the body in R2, when it is large
  body_json    TEXT,                       -- or inline, when it is small
  created_at   INTEGER NOT NULL
);
CREATE INDEX gen_debug_by_creature ON gen_debug (creature_id, created_at);

-- What badges and phones upload for debugging: boot lines, reset and crash
-- reasons, OTA results, remote debug command results (#56), logs.
CREATE TABLE device_debug (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  mac          TEXT NOT NULL,
  creature_id  INTEGER REFERENCES creatures(id),
  kind         TEXT NOT NULL,              -- boot, reset, crash, ota, command, log, ...
  fw           TEXT,
  via          TEXT,                       -- wifi, phone
  summary      TEXT,                       -- one line for the list
  size         INTEGER NOT NULL,
  r2_key       TEXT,
  body_json    TEXT,
  received_at  INTEGER NOT NULL
);
CREATE INDEX device_debug_by_badge ON device_debug (mac, received_at);
CREATE INDEX device_debug_by_kind ON device_debug (kind, received_at);
