-- Remote debug (docs/remote-debug.md, src/reports.js): the report each badge
-- sends at the end of every Wi-Fi window that reached the site. The last
-- KEEP_REPORTS per badge are kept; a screenshot's lines go to device_debug
-- (kind 'shot', R2 when large) and the report keeps its id. The owner's
-- commands are rows of actions with kind 'debug'.
CREATE TABLE badge_reports (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  mac           TEXT NOT NULL,
  received_at   INTEGER NOT NULL,
  fw            TEXT,
  uptime_s      INTEGER,
  reset         TEXT,                       -- why the boot before this one ended: poweron, sw, panic, task_wdt, brownout, ...
  crashed       INTEGER NOT NULL DEFAULT 0, -- 1: reset was a panic or a watchdog
  heap          INTEGER,
  minheap       INTEGER,
  big           INTEGER,                    -- the largest free internal block
  vbat_mv       INTEGER,
  rssi          INTEGER,
  auto_on       INTEGER,                    -- a Wi-Fi network remembered: update checks can run (they are never switched off)
  ota_last      TEXT,                       -- how the last window ended
  next_check_s  INTEGER,
  ota_json      TEXT,                       -- streaks, trial, rollback, interval, windows
  pet_json      TEXT,
  crash_json    TEXT,                       -- the core dump's summary: task, pc, backtrace
  results_json  TEXT,                       -- the last commands' results
  state_text    TEXT,                       -- a `state` command's capture
  shot_id       INTEGER REFERENCES device_debug(id),
  log_text      TEXT                        -- the serial log's last 8 KB
);
CREATE INDEX badge_reports_by_badge ON badge_reports (mac, id);
