// D1 access for the creature records (migrations/0001_creatures.sql). Every
// write appends: a creature, personality or pack is never overwritten, only
// superseded by a newer version.
const now = () => Date.now();
const J = (v) => (v === undefined ? null : JSON.stringify(v));

export async function touchBadge(db, mac, fw) {
  const t = now();
  await db.prepare(
    `INSERT INTO badges (mac, first_seen, last_seen, fw) VALUES (?1, ?2, ?2, ?3)
     ON CONFLICT (mac) DO UPDATE SET last_seen = ?2, fw = COALESCE(?3, fw)`).bind(mac, t, fw ?? null).run();
}

export async function findCreature(db, mac, genomeHash) {
  return db.prepare("SELECT * FROM creatures WHERE mac = ?1 AND genome_hash = ?2").bind(mac, genomeHash).first();
}

export async function upsertCreature(db, c) {
  const t = now();
  const have = await findCreature(db, c.mac, c.genomeHash);
  if (have) {
    await db.prepare(
      `UPDATE creatures SET last_seen = ?2, stage = COALESCE(?3, stage), name = COALESCE(?4, name),
       feats = ?5, status = CASE WHEN status = 'egg' AND ?3 IS NOT NULL THEN 'alive' ELSE status END WHERE id = ?1`)
      .bind(have.id, t, c.stage ?? null, c.name || null, c.feats ?? have.feats).run();
    return { ...have, created: false };
  }
  // The previous life on this lineage, if the badge says which lineage it is.
  const parent = c.lineage
    ? await db.prepare("SELECT id FROM creatures WHERE lineage_id = ?1 AND generation < ?2 ORDER BY generation DESC LIMIT 1")
        .bind(c.lineage, c.generation ?? 0).first()
    : null;
  const r = await db.prepare(
    `INSERT INTO creatures (mac, genome_hash, genome_hex, species, generation, lineage_id, parent_id, stage, name, feats,
       status, fw, raw_json, traits_json, first_seen, last_seen)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?15)`)
    .bind(c.mac, c.genomeHash, c.genomeHex, c.species, c.generation ?? null, c.lineage ?? null, parent?.id ?? null,
          c.stage ?? null, c.name || null, c.feats ?? 0, c.status ?? "alive", c.fw ?? null, J(c.raw), J(c.traits), t)
    .run();
  return { id: r.meta.last_row_id, created: true };
}

export async function latestPack(db, creatureId) {
  return db.prepare(
    `SELECT p.*, q.bible, q.catchphrases_json, q.version AS personality_version FROM packs p
     JOIN personalities q ON q.id = p.personality_id
     WHERE p.creature_id = ?1 AND p.status = 'ready' ORDER BY p.version DESC LIMIT 1`).bind(creatureId).first();
}

export async function packByCacheKey(db, key) {
  return db.prepare(
    `SELECT p.*, q.bible, q.catchphrases_json, q.model AS q_model, q.prompt_version FROM packs p
     JOIN personalities q ON q.id = p.personality_id
     WHERE p.cache_key = ?1 AND p.status = 'ready' ORDER BY p.id DESC LIMIT 1`).bind(key).first();
}

const nextVersion = async (db, table, creatureId) =>
  ((await db.prepare(`SELECT MAX(version) AS v FROM ${table} WHERE creature_id = ?1`).bind(creatureId).first())?.v ?? 0) + 1;

// A new personality and its pack, as the next versions for this creature. The
// older ready pack is superseded, and the new one waits for the badge.
export async function storeGeneration(db, creatureId, g) {
  const t = now();
  const pv = await nextVersion(db, "personalities", creatureId);
  const pr = await db.prepare(
    `INSERT INTO personalities (creature_id, version, source, bible, catchphrases_json, edited_by, inputs_json,
       modifiers_json, model, prompt_version, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)`)
    .bind(creatureId, pv, g.source, g.bible, J(g.catchphrases ?? []), g.editedBy ?? null, J(g.inputs ?? {}),
          J(g.modifiers ?? null), g.model, g.prompt, t).run();
  const personalityId = pr.meta.last_row_id;
  const version = await nextVersion(db, "packs", creatureId);
  await db.prepare("UPDATE packs SET status = 'superseded' WHERE creature_id = ?1 AND status = 'ready'").bind(creatureId).run();
  const r = await db.prepare(
    `INSERT INTO packs (creature_id, personality_id, version, cache_key, lines_json, fallback_json, provenance_json, sig,
       status, pending_device, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 'ready', 1, ?9)`)
    .bind(creatureId, personalityId, version, g.cacheKey, J(g.lines), J(g.fallback ?? []), J(g.provenance ?? {}),
          g.sig ?? null, t).run();
  return { packId: r.meta.last_row_id, personalityId, version, personalityVersion: pv };
}

export async function setPackSig(db, packId, sig) {
  await db.prepare("UPDATE packs SET sig = ?2 WHERE id = ?1").bind(packId, sig).run();
}

export async function ackPack(db, creatureId, version) {
  const r = await db.prepare(
    "UPDATE packs SET pending_device = 0, delivered_at = ?3 WHERE creature_id = ?1 AND version = ?2 AND pending_device = 1")
    .bind(creatureId, version, now()).run();
  return r.meta.changes ?? 0;
}

// Packs generated today for a badge's creatures: the per-badge daily cap.
export async function generationsSince(db, mac, since) {
  const r = await db.prepare(
    `SELECT COUNT(*) AS n FROM packs p JOIN creatures c ON c.id = p.creature_id
     WHERE c.mac = ?1 AND p.created_at >= ?2 AND json_extract(p.provenance_json, '$.copiedFrom') IS NULL`)
    .bind(mac, since).first();
  return r?.n ?? 0;
}

export async function openJob(db, creatureId, kind, input) {
  const t = now();
  const r = await db.prepare(
    "INSERT INTO jobs (creature_id, kind, input_json, status, created_at, updated_at) VALUES (?1, ?2, ?3, 'queued', ?4, ?4)")
    .bind(creatureId, kind, J(input ?? null), t).run();
  return r.meta.last_row_id;
}

export async function activeJob(db, creatureId, staleMs = 10 * 60 * 1000) {
  return db.prepare(
    `SELECT * FROM jobs WHERE creature_id = ?1 AND (status = 'queued' OR (status = 'running' AND updated_at > ?2))
     ORDER BY id DESC LIMIT 1`).bind(creatureId, now() - staleMs).first();
}

export async function setJob(db, id, fields) {
  const t = now();
  await db.prepare(
    `UPDATE jobs SET status = ?2, attempts = attempts + ?3, error = ?4, pack_id = COALESCE(?5, pack_id), updated_at = ?6 WHERE id = ?1`)
    .bind(id, fields.status, fields.attempt ? 1 : 0, fields.error ?? null, fields.packId ?? null, t).run();
}

export async function queuedJobs(db, limit, staleMs = 10 * 60 * 1000) {
  const r = await db.prepare(
    `SELECT j.id AS job_id, j.kind, j.input_json, j.attempts, c.id AS creature_id, c.mac, c.genome_hex, c.genome_hash,
       c.stage, c.generation, c.name, c.feats FROM jobs j JOIN creatures c ON c.id = j.creature_id
     WHERE (j.status = 'queued' OR (j.status = 'running' AND j.updated_at < ?2)) AND j.attempts < 3
     ORDER BY j.created_at LIMIT ?1`).bind(limit, now() - staleMs).all();
  return r.results ?? [];
}

// Everything the server holds, for the owner's export (GET /api/creatures).
export async function exportAll(db, { mac } = {}) {
  const where = mac ? "WHERE mac = ?1" : "";
  const bind = (s) => (mac ? s.bind(mac) : s);
  const badges = (await bind(db.prepare(`SELECT * FROM badges ${where}`)).all()).results;
  const creatures = (await bind(db.prepare(`SELECT * FROM creatures ${where} ORDER BY id`)).all()).results;
  const ids = creatures.map((c) => c.id);
  const inList = ids.length ? `(${ids.join(",")})` : "(NULL)";
  const personalities = (await db.prepare(`SELECT * FROM personalities WHERE creature_id IN ${inList} ORDER BY id`).all()).results;
  const packs = (await db.prepare(`SELECT * FROM packs WHERE creature_id IN ${inList} ORDER BY id`).all()).results;
  const parse = (row) => Object.fromEntries(Object.entries(row).map(([k, v]) =>
    [k.replace(/_json$/, ""), k.endsWith("_json") && typeof v === "string" ? JSON.parse(v) : v]));
  return {
    exportedAt: new Date().toISOString(),
    badges, creatures: creatures.map((c) => ({
      ...parse(c),
      personalities: personalities.filter((p) => p.creature_id === c.id).map(parse),
      packs: packs.filter((p) => p.creature_id === c.id).map(parse),
    })),
  };
}
