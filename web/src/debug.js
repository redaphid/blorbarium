// Debug and history storage (migrations/0002_debug.sql). A body larger than
// INLINE_MAX goes to the R2 bucket bound as DEBUG and the row keeps its key;
// a small one (or any, when no bucket is bound, as in the tests) stays inline.
// Nothing is deleted unless RETENTION_DAYS is set.
export const INLINE_MAX = 16 * 1024;

async function putBody(env, key, body) {
  const text = JSON.stringify(body);
  const size = new TextEncoder().encode(text).byteLength;
  if (size > INLINE_MAX && env.DEBUG) {
    await env.DEBUG.put(key, text, { httpMetadata: { contentType: "application/json" } });
    return { size, r2Key: key, inline: null };
  }
  return { size, r2Key: null, inline: text };
}

// One generation's raw trail: the model replies before validation, the checks'
// verdicts, and the error if it failed.
export async function storeGenDebug(env, { creatureId, packId = null, source, ok, error = null, model, prompt, meter, trace }) {
  const t = Date.now();
  const key = `gen/${creatureId ?? "none"}/${t}-${packId ?? "failed"}.json`;
  const b = await putBody(env, key, { creatureId, packId, source, ok, error, model, prompt, meter, trace });
  await env.DB.prepare(
    `INSERT INTO gen_debug (creature_id, pack_id, source, ok, error, model, prompt, calls, ms, usd, size, r2_key, body_json, created_at)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)`)
    .bind(creatureId ?? null, packId, source ?? null, ok ? 1 : 0, error, model ?? null, prompt ?? null, meter?.calls ?? null,
          meter?.ms ?? null, meter?.usd ?? null, b.size, b.r2Key, b.inline, t).run();
}

export async function storeDeviceDebug(env, { mac, creatureId = null, kind, fw = null, via = null, summary = null, body }) {
  const t = Date.now();
  const key = `device/${mac}/${t}-${kind}.json`;
  const b = await putBody(env, key, body ?? null);
  const r = await env.DB.prepare(
    `INSERT INTO device_debug (mac, creature_id, kind, fw, via, summary, size, r2_key, body_json, received_at)
     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)`)
    .bind(mac, creatureId, kind, fw, via, summary ? String(summary).slice(0, 200) : null, b.size, b.r2Key, b.inline, t).run();
  return r.meta.last_row_id;
}

export async function recordInstall(db, { mac, creatureId, packId = null, version, via }) {
  await db.prepare("INSERT INTO installs (mac, creature_id, pack_id, version, via, installed_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)")
    .bind(mac, creatureId, packId, version, via ?? null, Date.now()).run();
}

// A stored body, whichever place it is in.
export async function readBody(env, table, id) {
  const row = await env.DB.prepare(`SELECT r2_key, body_json FROM ${table === "device" ? "device_debug" : "gen_debug"} WHERE id = ?1`)
    .bind(id).first();
  if (!row) return null;
  if (row.body_json != null) return JSON.parse(row.body_json);
  if (row.r2_key && env.DEBUG) {
    const o = await env.DEBUG.get(row.r2_key);
    return o ? JSON.parse(await o.text()) : null;
  }
  return null;
}

// Retention: everything is kept unless RETENTION_DAYS is set; then debug rows
// (and their R2 bodies) older than that go. Packs, personalities and installs
// are history and are never purged.
export async function purgeOld(env) {
  const days = Number(env.RETENTION_DAYS);
  if (!days || days <= 0) return 0;
  const before = Date.now() - days * 86400000;
  let n = 0;
  for (const [table, col] of [["gen_debug", "created_at"], ["device_debug", "received_at"]]) {
    const rows = (await env.DB.prepare(`SELECT id, r2_key FROM ${table} WHERE ${col} < ?1`).bind(before).all()).results ?? [];
    for (const r of rows) if (r.r2_key && env.DEBUG) await env.DEBUG.delete(r.r2_key);
    n += (await env.DB.prepare(`DELETE FROM ${table} WHERE ${col} < ?1`).bind(before).run()).meta.changes ?? 0;
  }
  return n;
}
