// Remote debug (docs/remote-debug.md, migrations/0003_badge_reports.sql): what
// each badge sends home at the end of every Wi-Fi window that reached the
// site, and the owner's few commands for it, handed over in the reply.
//
//   POST /api/badge/report               the badge: its report; the reply is its commands, plain text
//   GET  /api/admin/health               every badge, with its newest report: healthy and updating?
//   GET  /api/admin/badges/:mac/reports  a badge's last reports, newest first, logs and all
//   POST /api/admin/badges/:mac/debug    {verb, arg?}: queue one allowlisted command for it
//
// The commands are an allowlist, checked here when they are queued and again
// on the badge (lib/rdebug): check, interval <30..720 min>, state, shot,
// reboot, and marquee <text> (a message for the kids, scrolled full screen). None can turn updates off, forget a network or touch the firmware.
// The badge takes them only from this site's HTTPS reply, whose certificate
// it checks against its pinned roots, so only ADMIN_TOKEN can queue them.
import { storeDeviceDebug } from "./debug.js";
import { enqueue } from "./admin.js";

export const KEEP_REPORTS = 40;                 // per badge; older ones are deleted as new ones come
export const MAX_REPORT_BODY = 320 * 1024;      // a report with a screenshot is about 200 KB
export const VERBS = { check: false, interval: true, state: false, shot: false, reboot: false, marquee: true };   // verb: takes an argument
export const INTERVAL_MIN = 30, INTERVAL_MAX = 720;
export const MARQUEE_MAX = 120;   // rdebug::kMarqueeMax: the badge refuses longer

// A marquee's text as the badge can draw it: printable ASCII (accents dropped,
// anything else a space), one line, at most MARQUEE_MAX characters.
export function marqueeText(t) {
  return String(t ?? "").normalize("NFKD").replace(/[\u0300-\u036f]/g, "").replace(/[^\x20-\x7e]+/g, " ")
    .replace(/ {2,}/g, " ").trim().slice(0, MARQUEE_MAX).trim();
}
const COMMAND_TTL_S = 7 * 86400;                // a command its badge has not fetched in a week expires

// A queued command's payload, or why not.
export function checkCommand(verb, arg) {
  if (!Object.hasOwn(VERBS, verb)) return { error: `verb: one of ${Object.keys(VERBS).join(", ")}` };
  if (!VERBS[verb]) return arg === undefined || arg === null || arg === "" ? { payload: { verb } } : { error: `${verb} takes no argument` };
  if (verb === "marquee") {
    const text = marqueeText(arg);
    return text ? { payload: { verb, arg: text } } : { error: "marquee: some text to show" };
  }
  const n = Number(arg);
  if (!Number.isInteger(n) || n < INTERVAL_MIN || n > INTERVAL_MAX) return { error: `interval: whole minutes, ${INTERVAL_MIN}..${INTERVAL_MAX}` };
  return { payload: { verb, arg: n } };
}

const num = (v) => (Number.isFinite(Number(v)) ? Math.round(Number(v)) : null);
const str = (v, n) => (typeof v === "string" ? v.slice(0, n) : null);
const CRASHES = new Set(["panic", "int_wdt", "task_wdt", "wdt"]);

async function readReport(request) {
  const len = Number(request.headers.get("content-length") || 0);
  if (len > MAX_REPORT_BODY) throw new Error("body too large");
  const text = await request.text();
  if (text.length > MAX_REPORT_BODY) throw new Error("body too large");
  return JSON.parse(text);
}

// The badge's POST. Its results close their commands; the reply hands over the commands queued since.
export async function postReport(request, env, { fail, normaliseMac }) {
  let b;
  try { b = await readReport(request); } catch (e) { return fail(400, `bad request: ${e.message}`); }
  const mac = normaliseMac(b?.mac);
  if (!mac) return fail(400, "mac: 12 hex digits");
  if (env.PET_LINES_LIMITER) {
    const { success } = await env.PET_LINES_LIMITER.limit({ key: `report:${mac}` });
    if (!success) return fail(429, "slow down");
  }
  const db = env.DB, t = Date.now();
  await db.prepare(`INSERT INTO badges (mac, first_seen, last_seen, fw) VALUES (?1, ?2, ?2, ?3)
     ON CONFLICT (mac) DO UPDATE SET last_seen = ?2, fw = COALESCE(?3, fw)`).bind(mac, t, str(b.fw, 32)).run();
  // A screenshot is large: it goes where device debug bodies go (R2 when bound), the report keeps its id.
  let shotId = null;
  if (typeof b.shot === "string" && b.shot.length) {
    shotId = await storeDeviceDebug(env, { mac, kind: "shot", fw: str(b.fw, 32), via: "wifi", summary: `screenshot, ${b.shot.length} bytes of shot lines`, body: { lines: b.shot } });
  }
  const ota = b.ota && typeof b.ota === "object" ? b.ota : null;
  const r = await db.prepare(
    `INSERT INTO badge_reports (mac, received_at, fw, uptime_s, reset, crashed, heap, minheap, big, vbat_mv, rssi, auto_on, ota_last,
       next_check_s, ota_json, pet_json, crash_json, results_json, state_text, shot_id, log_text)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21)`)
    .bind(mac, t, str(b.fw, 32), num(b.uptime_s), str(b.reset, 16), CRASHES.has(b.reset) ? 1 : 0, num(b.heap), num(b.minheap), num(b.big),
      num(b.vbat_mv), num(b.rssi), ota ? (ota.auto ? 1 : 0) : null, str(ota?.last, 200), num(ota?.next_s),
      ota ? JSON.stringify(ota) : null, b.pet ? JSON.stringify(b.pet) : null, b.crash ? JSON.stringify(b.crash) : null,
      Array.isArray(b.results) ? JSON.stringify(b.results.slice(0, 16)) : null, str(b.state, 16384), shotId, str(b.log, 16384)).run();
  const reportId = r.meta.last_row_id;
  await db.prepare(`DELETE FROM badge_reports WHERE mac = ?1 AND id NOT IN
     (SELECT id FROM badge_reports WHERE mac = ?1 ORDER BY id DESC LIMIT ?2)`).bind(mac, KEEP_REPORTS).run();
  // What the last commands did.
  for (const res of Array.isArray(b.results) ? b.results.slice(0, 16) : []) {
    const id = Number(res?.id);
    if (!Number.isInteger(id) || id <= 0) continue;
    const text = str(res.text, 200);
    await db.prepare(`UPDATE actions SET status = ?3, completed_at = ?4, result_json = ?5
       WHERE id = ?1 AND mac = ?2 AND kind = 'debug' AND status IN ('pending', 'delivered')`)
      .bind(id, mac, res.ok ? "done" : "failed", t, JSON.stringify({ text, report_id: reportId, shot_id: /^shot/.test(text ?? "") ? shotId : undefined })).run();
  }
  // The commands queued since, each handed over once (a reboot must never come twice).
  await db.prepare(`UPDATE actions SET status = 'expired' WHERE mac = ?1 AND kind = 'debug' AND status = 'pending'
     AND expires_at IS NOT NULL AND expires_at < ?2`).bind(mac, t).run();
  const due = (await db.prepare(`SELECT id, payload_json FROM actions WHERE mac = ?1 AND kind = 'debug' AND status = 'pending'
     AND (not_before IS NULL OR not_before <= ?2) ORDER BY id LIMIT 8`).bind(mac, t).all()).results;
  const lines = ["blorb-cmds 1"];
  for (const a of due) {
    const p = JSON.parse(a.payload_json);
    const ok = checkCommand(p.verb, p.arg);   // the allowlist again: a row written some other way is never sent
    if (ok.error) {
      await db.prepare("UPDATE actions SET status = 'failed', completed_at = ?2, result_json = ?3 WHERE id = ?1")
        .bind(a.id, t, JSON.stringify({ text: `not sent: ${ok.error}` })).run();
      continue;
    }
    await db.prepare("UPDATE actions SET status = 'delivered', delivered_at = ?2, delivered_via = 'wifi' WHERE id = ?1").bind(a.id, t).run();
    lines.push(`${a.id} ${ok.payload.verb}${ok.payload.arg !== undefined ? " " + ok.payload.arg : ""}`);
  }
  return new Response(lines.join("\n") + "\n", { headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });
}

const parse = (row) => {
  if (!row) return row;
  const o = { ...row };
  for (const k of Object.keys(row)) if (k.endsWith("_json")) o[k.slice(0, -5)] = row[k] == null ? null : JSON.parse(row[k]), delete o[k];
  return o;
};
const SUMMARY_COLS = `id, mac, received_at, fw, uptime_s, reset, crashed, heap, minheap, big, vbat_mv, rssi, auto_on, ota_last, next_check_s,
  ota_json, pet_json, crash_json, results_json, shot_id`;

// Every badge at a glance: its newest report, and how many of its kept reports followed a crash.
export async function health(db) {
  const rows = (await db.prepare(
    `SELECT b.mac AS badge_mac, b.label, b.fw AS badge_fw, b.last_seen,
       (SELECT COUNT(*) FROM badge_reports x WHERE x.mac = b.mac AND x.crashed = 1) AS crashes,
       (SELECT COUNT(*) FROM badge_reports x WHERE x.mac = b.mac) AS reports,
       (SELECT COUNT(*) FROM actions a WHERE a.mac = b.mac AND a.kind = 'debug' AND a.status IN ('pending', 'delivered')) AS open_commands,
       r.*
     FROM badges b LEFT JOIN badge_reports r ON r.id = (SELECT MAX(id) FROM badge_reports y WHERE y.mac = b.mac)
     ORDER BY COALESCE(r.received_at, 0) DESC, b.last_seen DESC`).all()).results;
  return { now: Date.now(), badges: rows.map((r) => {
    const { id, badge_mac, ...rest } = parse(r);
    return { ...rest, mac: badge_mac, report_id: id ?? null };
  }) };
}

export async function reports(db, mac, limit = KEEP_REPORTS) {
  const rows = (await db.prepare(`SELECT ${SUMMARY_COLS}, state_text, log_text FROM badge_reports WHERE mac = ?1 ORDER BY id DESC LIMIT ?2`)
    .bind(mac, Math.min(KEEP_REPORTS, limit)).all()).results;
  const commands = (await db.prepare("SELECT * FROM actions WHERE mac = ?1 AND kind = 'debug' ORDER BY id DESC LIMIT 50").bind(mac).all()).results;
  return { reports: rows.map(parse), commands: commands.map(parse) };
}

export async function queueCommand(db, mac, b) {
  const c = checkCommand(String(b?.verb ?? ""), b?.arg);
  if (c.error) return c;
  const q = await enqueue(db, { macs: [mac], kind: "debug", payload: c.payload, createdBy: "admin", expiresInS: COMMAND_TTL_S });
  return { ok: true, id: q.ids[0], payload: c.payload };
}
