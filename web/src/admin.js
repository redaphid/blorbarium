// The owner's dashboard API (#41) and the badge's side of its action queue
// (#49). Admin routes need Authorization: Bearer ADMIN_TOKEN (and, deployed,
// can sit behind Cloudflare Access too); badge routes are keyed by MAC.
//
//   GET  /api/admin/overview                     badges, their current pets, pending actions
//   GET  /api/admin/badges/:mac                  a badge's creatures by lineage, its actions and uploads
//   GET  /api/admin/creatures/:id                genes, traits, every personality and pack, stats, diary
//   POST /api/admin/creatures/:id/regenerate     {bible?, push?}: a new personality/pack version (edit or fresh)
//   POST /api/admin/actions                      {macs: [..] | "all", kind, payload, expires_in_s?}
//   POST /api/admin/actions/:id/cancel
//   GET  /api/admin/actions?mac=&status=
//   POST /api/admin/ota                          {release, target: "all" | [macs]}: a rollout, fanned out as actions
//   POST /api/admin/ota/:id                      {state: active | paused | killed}
//   GET  /api/admin/ota
//   GET  /api/admin/analytics?metric=&days=      metric names, or one metric per day and per badge
//   POST /api/admin/badges/:mac                  {label?, settings?}
//   GET  /api/admin/health                       remote debug: every badge with its newest report (reports.js)
//   GET  /api/admin/badges/:mac/reports          remote debug: its last reports and commands
//   POST /api/admin/badges/:mac/debug            remote debug: {verb, arg?}, one allowlisted command
//
//   GET  /api/badge/actions?mac=&via=wifi|phone  pending actions for this badge, marked delivered
//   POST /api/badge/actions/:id                  {mac, ok, result}: what applying it did
import { readBody as readDebugBody } from "./debug.js";
import { health, reports, queueCommand, checkCommand } from "./reports.js";
import { hexToBytes, rawSheet } from "./genome.js";
import { traitSheet } from "./traits.js";

// debug: remote debug's commands (reports.js), handed over only in the reply to a Wi-Fi report.
const KINDS = new Set(["line_pack", "ota", "settings", "command", "debug"]);
const now = () => Date.now();
const J = (v) => JSON.stringify(v ?? null);
const parseRow = (row) => row && Object.fromEntries(Object.entries(row).map(([k, v]) =>
  [k.replace(/_json$/, ""), k.endsWith("_json") && typeof v === "string" ? JSON.parse(v) : v]));

async function audit(db, who, what, details) {
  await db.prepare("INSERT INTO admin_audit (who, what, details_json, at) VALUES (?1, ?2, ?3, ?4)").bind(who, what, J(details), now()).run();
}

async function macsFor(db, target) {
  if (target === "all") return (await db.prepare("SELECT mac FROM badges ORDER BY mac").all()).results.map((r) => r.mac);
  if (!Array.isArray(target)) return [];
  return [...new Set(target.map((m) => String(m).toLowerCase().replace(/[^0-9a-f]/g, "")).filter((m) => m.length === 12))];
}

export async function enqueue(db, { macs, kind, payload, createdBy = "admin", expiresInS, notBefore }) {
  const batch = crypto.randomUUID();
  const t = now();
  const ids = [];
  for (const mac of macs) {
    await db.prepare(
      `INSERT INTO badges (mac, first_seen, last_seen) VALUES (?1, ?2, ?2) ON CONFLICT (mac) DO NOTHING`).bind(mac, t).run();
    const r = await db.prepare(
      `INSERT INTO actions (mac, batch_id, kind, payload_json, status, created_by, created_at, not_before, expires_at)
       VALUES (?1, ?2, ?3, ?4, 'pending', ?5, ?6, ?7, ?8)`)
      .bind(mac, batch, kind, J(payload), createdBy, t, notBefore ?? null, expiresInS ? t + expiresInS * 1000 : null).run();
    ids.push(r.meta.last_row_id);
  }
  return { batch, ids };
}

// ---- admin ----
async function overview(db) {
  const badges = (await db.prepare(
    `SELECT b.*, (SELECT COUNT(*) FROM actions a WHERE a.mac = b.mac AND a.status IN ('pending','delivered')) AS open_actions,
       (SELECT COUNT(*) FROM creatures c WHERE c.mac = b.mac) AS creatures
     FROM badges b ORDER BY b.last_seen DESC`).all()).results;
  const current = (await db.prepare(
    `SELECT c.id, c.mac, c.species, c.name, c.generation, c.stage, c.status, c.lineage_id, c.last_seen,
       (SELECT MAX(version) FROM packs p WHERE p.creature_id = c.id) AS pack_version,
       (SELECT pending_device FROM packs p WHERE p.creature_id = c.id ORDER BY version DESC LIMIT 1) AS pack_pending
     FROM creatures c WHERE c.id IN (SELECT MAX(id) FROM creatures GROUP BY mac)`).all()).results;
  const byMac = Object.fromEntries(current.map((c) => [c.mac, c]));
  const counts = await db.prepare(
    `SELECT (SELECT COUNT(*) FROM creatures) AS creatures, (SELECT COUNT(*) FROM packs) AS packs,
       (SELECT COUNT(*) FROM diary_entries) AS diary, (SELECT COUNT(*) FROM actions WHERE status IN ('pending','delivered')) AS open_actions`).first();
  return { counts, badges: badges.map((b) => ({ ...parseRow(b), current: byMac[b.mac] ?? null })) };
}

async function badgeDetail(db, mac) {
  const badge = parseRow(await db.prepare("SELECT * FROM badges WHERE mac = ?1").bind(mac).first());
  if (!badge) return null;
  const creatures = (await db.prepare(
    `SELECT id, species, name, generation, lineage_id, parent_id, stage, status, genome_hash, first_seen, last_seen,
       (SELECT MAX(version) FROM packs p WHERE p.creature_id = c.id) AS pack_version
     FROM creatures c WHERE mac = ?1 ORDER BY COALESCE(lineage_id, ''), generation, id`).bind(mac).all()).results;
  const actions = (await db.prepare("SELECT * FROM actions WHERE mac = ?1 ORDER BY id DESC LIMIT 100").bind(mac).all()).results.map(parseRow);
  const uploads = (await db.prepare(
    "SELECT id, fw, period_start, period_end, uploaded_at FROM analytics_uploads WHERE mac = ?1 ORDER BY id DESC LIMIT 20")
    .bind(mac).all()).results;
  const installs = (await db.prepare(
    `SELECT i.*, c.species, c.name FROM installs i JOIN creatures c ON c.id = i.creature_id WHERE i.mac = ?1
     ORDER BY i.installed_at DESC LIMIT 100`).bind(mac).all()).results;
  const kinds = (await db.prepare("SELECT kind, COUNT(*) AS n FROM device_debug WHERE mac = ?1 GROUP BY kind ORDER BY kind")
    .bind(mac).all()).results;
  return { badge, creatures, actions, uploads, installs, debugKinds: kinds };
}

function currentTraits(c) {
  try {
    const raw = rawSheet(hexToBytes(c.genome_hex), { stage: c.stage ?? "adult", feats: c.feats ?? 0 });
    return traitSheet(raw, { generation: c.generation ?? 1, name: c.name ?? "" }).text;
  } catch (e) {
    return null;
  }
}

async function creatureDetail(db, id) {
  const c = parseRow(await db.prepare("SELECT * FROM creatures WHERE id = ?1").bind(id).first());
  if (!c) return null;
  const all = async (sql) => (await db.prepare(sql).bind(id).all()).results.map(parseRow);
  return {
    creature: c,
    // creature.traits is the sheet stored at first sight; traitsNow is what the
    // next generation reads (app.js generateFor builds it fresh from the genes).
    traitsNow: currentTraits(c),
    personalities: await all("SELECT * FROM personalities WHERE creature_id = ?1 ORDER BY version DESC"),
    packs: await all("SELECT * FROM packs WHERE creature_id = ?1 ORDER BY version DESC"),
    stats: await all("SELECT * FROM life_stats WHERE creature_id = ?1 ORDER BY id DESC LIMIT 30"),
    diary: await all("SELECT day, entry, model, created_at FROM diary_entries WHERE creature_id = ?1 ORDER BY day DESC LIMIT 60"),
    children: await all("SELECT id, species, generation, mac FROM creatures WHERE parent_id = ?1"),
    jobs: await all("SELECT * FROM jobs WHERE creature_id = ?1 ORDER BY id DESC LIMIT 20"),
    installs: await all("SELECT * FROM installs WHERE creature_id = ?1 ORDER BY installed_at DESC"),
    genDebug: await all(`SELECT id, pack_id, source, ok, error, model, prompt, calls, ms, usd, size, r2_key IS NOT NULL AS in_r2, created_at
      FROM gen_debug WHERE creature_id = ?1 ORDER BY id DESC LIMIT 200`),
  };
}

async function analytics(db, url) {
  const metric = url.searchParams.get("metric");
  const days = Math.min(365, Number(url.searchParams.get("days")) || 30);
  const since = now() - days * 86400000;
  if (!metric) {
    const r = await db.prepare(
      "SELECT metric, COUNT(*) AS n, COUNT(DISTINCT mac) AS badges FROM analytics_metrics WHERE ts >= ?1 GROUP BY metric ORDER BY metric")
      .bind(since).all();
    return { metrics: r.results };
  }
  const perDay = (await db.prepare(
    `SELECT date(ts / 1000, 'unixepoch') AS day, dims_json, SUM(value) AS sum, AVG(value) AS avg, MIN(value) AS min,
       MAX(value) AS max, COUNT(*) AS n FROM analytics_metrics WHERE metric = ?1 AND ts >= ?2
     GROUP BY day, dims_json ORDER BY day`).bind(metric, since).all()).results;
  const perBadge = (await db.prepare(
    `SELECT mac, dims_json, SUM(value) AS sum, AVG(value) AS avg, COUNT(*) AS n, MAX(ts) AS last
     FROM analytics_metrics WHERE metric = ?1 AND ts >= ?2 GROUP BY mac, dims_json ORDER BY mac`).bind(metric, since).all()).results;
  return { metric, days, perDay, perBadge };
}

export async function handleAdmin(request, url, env, { json, fail, readBody, generateFor, normaliseMac }) {
  const db = env.DB;
  const who = "admin";
  const path = url.pathname.replace(/^\/api\/admin/, "");
  const m = (re) => re.exec(path);
  const body = async () => { try { return await readBody(request); } catch { return null; } };
  let r;
  if (request.method === "GET" && path === "/overview") return json(await overview(db));
  if (request.method === "GET" && path === "/health") return json(await health(db));
  if (request.method === "GET" && (r = m(/^\/badges\/([0-9a-f:]+)\/reports$/))) {
    const mac = normaliseMac(r[1]);
    return mac ? json(await reports(db, mac)) : fail(400, "mac");
  }
  if (request.method === "POST" && (r = m(/^\/badges\/([0-9a-f:]+)\/debug$/))) {
    const mac = normaliseMac(r[1]);
    const b = await body();
    if (!mac || !b) return fail(400, "bad request");
    const q = await queueCommand(db, mac, b);
    if (q.error) return fail(400, q.error);
    await audit(db, who, "debug", { mac, ...q.payload });
    return json(q);
  }
  if (request.method === "GET" && (r = m(/^\/badges\/([0-9a-f:]+)$/))) {
    const d = await badgeDetail(db, normaliseMac(r[1]));
    return d ? json(d) : fail(404, "unknown badge");
  }
  if (request.method === "POST" && (r = m(/^\/badges\/([0-9a-f:]+)$/))) {
    const b = await body();
    const mac = normaliseMac(r[1]);
    if (!b || !mac) return fail(400, "bad request");
    await db.prepare("UPDATE badges SET label = COALESCE(?2, label), settings_json = COALESCE(?3, settings_json) WHERE mac = ?1")
      .bind(mac, typeof b.label === "string" ? b.label.slice(0, 60) : null, b.settings ? J(b.settings) : null).run();
    await audit(db, who, "badge", { mac, ...b });
    return json({ ok: true });
  }
  if (request.method === "GET" && (r = m(/^\/creatures\/(\d+)$/))) {
    const d = await creatureDetail(db, Number(r[1]));
    return d ? json(d) : fail(404, "unknown creature");
  }
  if (request.method === "POST" && (r = m(/^\/creatures\/(\d+)\/regenerate$/))) {
    const b = (await body()) ?? {};
    const c = await db.prepare("SELECT * FROM creatures WHERE id = ?1").bind(Number(r[1])).first();
    if (!c) return fail(404, "unknown creature");
    const bible = typeof b.bible === "string" && b.bible.trim() ? b.bible.trim().slice(0, 4000) : undefined;
    const row = await generateFor(env, c, { source: bible ? "admin" : "admin", bible, editedBy: bible ? who : undefined });
    let action = null;
    if (b.push) action = await enqueue(db, { macs: [c.mac], kind: "line_pack", createdBy: who,
      payload: { pack_id: row.id, version: row.version, genome_hash: c.genome_hash } });
    await audit(db, who, "regenerate", { creature: c.id, edited: !!bible, pack: row.id, push: !!b.push });
    return json({ ok: true, pack_id: row.id, version: row.version, action });
  }
  if (request.method === "GET" && path === "/actions") {
    const mac = normaliseMac(url.searchParams.get("mac"));
    const status = url.searchParams.get("status");
    const rows = (await db.prepare(
      `SELECT * FROM actions WHERE (?1 IS NULL OR mac = ?1) AND (?2 IS NULL OR status = ?2) ORDER BY id DESC LIMIT 300`)
      .bind(mac, status).all()).results;
    return json({ actions: rows.map(parseRow) });
  }
  if (request.method === "POST" && path === "/actions") {
    const b = await body();
    if (!b || !KINDS.has(b.kind) || typeof b.payload !== "object") return fail(400, `kind: ${[...KINDS].join(", ")}; payload: an object`);
    if (b.kind === "line_pack" && !b.payload.pack_id) return fail(400, "a line_pack action names a pack_id");
    if (b.kind === "debug" && checkCommand(String(b.payload.verb ?? ""), b.payload.arg).error) return fail(400, checkCommand(String(b.payload.verb ?? ""), b.payload.arg).error);
    const macs = await macsFor(db, b.macs);
    if (!macs.length) return fail(400, "macs: 'all' or a list of MACs");
    const q = await enqueue(db, { macs, kind: b.kind, payload: b.payload, createdBy: who, expiresInS: Number(b.expires_in_s) || null });
    await audit(db, who, "actions", { kind: b.kind, payload: b.payload, macs });
    return json({ ok: true, ...q });
  }
  if (request.method === "POST" && (r = m(/^\/actions\/(\d+)\/cancel$/))) {
    const res = await db.prepare(
      "UPDATE actions SET status = 'cancelled', completed_at = ?2 WHERE id = ?1 AND status IN ('pending','delivered')")
      .bind(Number(r[1]), now()).run();
    await audit(db, who, "cancel", { action: Number(r[1]) });
    return json({ ok: true, cancelled: res.meta.changes });
  }
  if (request.method === "GET" && path === "/ota") {
    return json({ rollouts: (await db.prepare("SELECT * FROM ota_rollouts ORDER BY id DESC").all()).results.map(parseRow) });
  }
  if (request.method === "POST" && path === "/ota") {
    const b = await body();
    if (!b || typeof b.release !== "string" || !b.release.trim()) return fail(400, "release: the version or manifest path under /fw/");
    const macs = await macsFor(db, b.target);
    if (!macs.length) return fail(400, "target: 'all' or a list of MACs");
    const t = now();
    const ro = await db.prepare(
      "INSERT INTO ota_rollouts (release, target_json, state, created_by, created_at, updated_at) VALUES (?1, ?2, 'active', ?3, ?4, ?4)")
      .bind(b.release.trim(), J(b.target === "all" ? "all" : macs), who, t).run();
    const id = ro.meta.last_row_id;
    const q = await enqueue(db, { macs, kind: "ota", payload: { rollout_id: id, release: b.release.trim() }, createdBy: who });
    await audit(db, who, "ota", { rollout: id, release: b.release, macs });
    return json({ ok: true, rollout_id: id, ...q });
  }
  if (request.method === "POST" && (r = m(/^\/ota\/(\d+)$/))) {
    const b = await body();
    if (!b || !["active", "paused", "killed"].includes(b.state)) return fail(400, "state: active, paused or killed");
    const id = Number(r[1]);
    await db.prepare("UPDATE ota_rollouts SET state = ?2, updated_at = ?3 WHERE id = ?1").bind(id, b.state, now()).run();
    // A kill switch cancels what has not been applied; a pause only holds delivery (badge route below).
    if (b.state === "killed") {
      await db.prepare(
        `UPDATE actions SET status = 'cancelled', completed_at = ?2 WHERE kind = 'ota' AND status IN ('pending','delivered')
         AND json_extract(payload_json, '$.rollout_id') = ?1`).bind(id, now()).run();
    }
    await audit(db, who, "ota-state", { rollout: id, state: b.state });
    return json({ ok: true });
  }
  if (request.method === "GET" && path === "/analytics") return json(await analytics(db, url));
  // Device debug uploads, filterable: ?mac= &kind= &creature= &since= &until= (unix ms) &limit=
  if (request.method === "GET" && path === "/debug") {
    const q = url.searchParams;
    const rows = (await db.prepare(
      `SELECT id, mac, creature_id, kind, fw, via, summary, size, r2_key IS NOT NULL AS in_r2, received_at FROM device_debug
       WHERE (?1 IS NULL OR mac = ?1) AND (?2 IS NULL OR kind = ?2) AND (?3 IS NULL OR creature_id = ?3)
         AND received_at >= ?4 AND received_at <= ?5 ORDER BY received_at DESC LIMIT ?6`)
      .bind(normaliseMac(q.get("mac")), q.get("kind") || null, q.get("creature") ? Number(q.get("creature")) : null,
            Number(q.get("since")) || 0, Number(q.get("until")) || 8.64e15, Math.min(1000, Number(q.get("limit")) || 200)).all()).results;
    return json({ items: rows });
  }
  // One stored body (gen or device), from D1 or R2.
  if (request.method === "GET" && (r = m(/^\/blob\/(gen|device)\/(\d+)$/))) {
    const b = await readDebugBody(env, r[1], Number(r[2]));
    return b === null ? fail(404, "no such body") : json(b);
  }
  return fail(404, "no such admin endpoint");
}

// ---- the badge's side (#49) ----
export async function handleBadge(request, url, env, { json, fail, readBody, normaliseMac }) {
  const db = env.DB;
  const t = now();
  if (request.method === "GET" && url.pathname === "/api/badge/actions") {
    const mac = normaliseMac(url.searchParams.get("mac"));
    if (!mac) return fail(400, "mac");
    const via = url.searchParams.get("via") === "phone" ? "phone" : "wifi";
    await db.prepare("UPDATE actions SET status = 'expired' WHERE mac = ?1 AND status = 'pending' AND expires_at IS NOT NULL AND expires_at < ?2")
      .bind(mac, t).run();
    // A paused or killed rollout's OTA actions wait (or were cancelled); everything else due is handed over.
    const rows = (await db.prepare(
      `SELECT a.* FROM actions a LEFT JOIN ota_rollouts r ON a.kind = 'ota' AND r.id = json_extract(a.payload_json, '$.rollout_id')
       WHERE a.mac = ?1 AND a.status IN ('pending', 'delivered') AND (a.not_before IS NULL OR a.not_before <= ?2)
         AND (a.kind != 'ota' OR r.state = 'active') AND a.kind != 'debug' ORDER BY a.id LIMIT 20`).bind(mac, t).all()).results;
    for (const a of rows) {
      if (a.status === "pending") {
        await db.prepare("UPDATE actions SET status = 'delivered', delivered_at = ?2, delivered_via = ?3 WHERE id = ?1").bind(a.id, t, via).run();
      }
    }
    return json({ actions: rows.map((a) => ({ id: a.id, kind: a.kind, payload: JSON.parse(a.payload_json), created_at: a.created_at })) });
  }
  let r;
  if (request.method === "POST" && (r = /^\/api\/badge\/actions\/(\d+)$/.exec(url.pathname))) {
    let b;
    try { b = await readBody(request); } catch { return fail(400, "bad request"); }
    const mac = normaliseMac(b?.mac);
    if (!mac) return fail(400, "mac");
    const res = await db.prepare(
      `UPDATE actions SET status = ?3, completed_at = ?4, result_json = ?5 WHERE id = ?1 AND mac = ?2 AND status IN ('pending','delivered')`)
      .bind(Number(r[1]), mac, b.ok ? "done" : "failed", t, J(b.result ?? null)).run();
    if (!res.meta.changes) return fail(404, "no open action with that id for this badge");
    const a = await db.prepare("SELECT kind, payload_json FROM actions WHERE id = ?1").bind(Number(r[1])).first();
    if (b.ok && a?.kind === "line_pack") {
      const p = JSON.parse(a.payload_json);
      const c = await db.prepare("SELECT creature_id FROM packs WHERE id = ?1").bind(p.pack_id).first();
      if (c) await db.prepare("INSERT INTO installs (mac, creature_id, pack_id, version, via, installed_at) VALUES (?1, ?2, ?3, ?4, 'action', ?5)")
        .bind(mac, c.creature_id, p.pack_id, p.version, t).run();
    }
    return json({ ok: true });
  }
  return fail(404, "no such badge endpoint");
}
