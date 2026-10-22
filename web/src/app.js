// blorb.hypnodroid.com's Worker (entry: worker.js): the static site (public/) as before, plus the
// AI personality lines API (#37). Only /api/* reaches this code
// (wrangler.jsonc run_worker_first); everything else is the assets.
//
//   POST /api/pet-lines        {mac, genome, stage?, name?, generation?, lineage?, feats?, fw?, wait?}
//                              -> 200 {bible, lines, version, text, sig, ...} | 202 {status: "queued"}
//   GET  /api/pet-lines?mac=&genome_hash=   the newest pack, or 202 while it is being written
//   POST /api/pet-lines/ack    {mac, genome_hash, version}: the badge stored it
//   POST /api/stats            {mac, genome | genome_hash, stats_version?, stats?, parents?, analytics?, fw?}  (#38, #50)
//   GET  /api/diary?mac=&genome_hash=   the pet's diary, newest first (#39, for the phone #42)
//   GET  /api/pet?genome_hash=          the phone's view: bible, sample lines, diary, family (#42)
//   POST /api/personality      {genome, bible}: lines rewritten from the owner's edited text (#43)
//   GET  /api/creatures[?mac=] everything the server holds (Authorization: Bearer ADMIN_TOKEN)
//
// Crons (wrangler.jsonc triggers): every 10 minutes, the packs a POST queued
// instead of waiting for; once a day (DIARY_CRON), each pet's diary entry.
import { hexToBytes, rawSheet, genomeHash } from "./genome.js";
import { traitSheet } from "./traits.js";
import { generatePack, DEFAULT_MODEL, DEFAULT_PROMPT } from "./pipeline.js";
import { packText, signPack } from "./pack.js";
import * as store from "./store.js";
import { storeUpload, runDiaries, diaryFor, statsDelta } from "./diary.js";
import { handleAdmin, handleBadge, enqueue } from "./admin.js";
import { ADMIN_HTML } from "./admin_page.js";
import { firmware } from "./firmware.js";
import { storeGenDebug, storeDeviceDebug, recordInstall, purgeOld } from "./debug.js";
import { newMeter } from "./pipeline.js";

const MAX_BODY = 64 * 1024;
export const DIARY_CRON = "0 4 * * *";   // daily, 04:00 UTC: after a night's stats, before the family's morning
const DAY_MS = 24 * 60 * 60 * 1000;

const json = (body, status = 200, headers = {}) =>
  new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json", ...headers } });
const fail = (status, error) => json({ error }, status);

export function normaliseMac(mac) {
  const m = String(mac ?? "").toLowerCase().replace(/[^0-9a-f]/g, "");
  return m.length === 12 ? m : null;
}

const settings = (env) => ({
  model: env.LINES_MODEL || DEFAULT_MODEL,
  prompt: env.LINES_PROMPT || DEFAULT_PROMPT,
  dailyCap: Number(env.MAX_GENERATIONS_PER_DAY || 6),
});
const cacheKey = (hash, s) => `${hash}:${s.prompt}:${s.model}`;

async function readBody(request) {
  const len = Number(request.headers.get("content-length") || 0);
  if (len > MAX_BODY) throw new Error("body too large");
  const text = await request.text();
  if (text.length > MAX_BODY) throw new Error("body too large");
  return JSON.parse(text);
}

// What the badge gets: the pack, its signed text, and where it came from.
function packReply(row, creature, extra = {}) {
  const lines = JSON.parse(row.lines_json);
  const prov = JSON.parse(row.provenance_json);
  return {
    creature_id: creature.id, genome_hash: creature.genome_hash, version: row.version,
    model: prov.model, prompt: prov.prompt, bible: row.bible,
    catchphrases: JSON.parse(row.catchphrases_json ?? "[]"), lines, fallback: JSON.parse(row.fallback_json),
    text: packText({ genomeHash: creature.genome_hash, version: row.version, lines }), sig: row.sig, ...extra,
  };
}

// Writes and stores one generation for a creature; returns the stored pack row.
// `bible` given (an edit from the phone or the dashboard): the lines are written
// from it and call 1 is skipped. Only a genes-only pack is shared by cache key.
export async function generateFor(env, creature, { source = "genes", s = settings(env), bible, catchphrases, editedBy, carried,
                                                  life, currentBible, personality, lately, avoid } = {}) {
  // personality ({id, version, bible, catchphrases}): new lines from that stored personality, which is kept as is.
  if (personality) { bible = personality.bible; catchphrases = personality.catchphrases; }
  const bytes = hexToBytes(creature.genome_hex);
  const raw = rawSheet(bytes, { stage: creature.stage ?? "adult", feats: creature.feats ?? 0 });
  const full = creature.parent_id === undefined
    ? await env.DB.prepare("SELECT parent_id FROM creatures WHERE id = ?1").bind(creature.id).first() : creature;
  const history = await store.familyHistory(env.DB, { parent_id: full?.parent_id ?? null }, carried);
  const pet = { raw, generation: creature.generation ?? 1, name: creature.name ?? "", hash: creature.genome_hash,
                bible, catchphrases, history, life, currentBible, lately, avoid };
  pet.traits = traitSheet(raw, { generation: pet.generation, name: pet.name });
  // Every raw reply is kept for the debug store, whether the generation works or not.
  const meter = newMeter({ trace: true });
  let pack;
  try {
    pack = await generatePack(env.AI, pet, { model: s.model, prompt: s.prompt, meter });
  } catch (e) {
    await storeGenDebug(env, { creatureId: creature.id, source, ok: false, error: String(e?.message ?? e), model: s.model,
      prompt: s.prompt, meter: { ...meter, trace: undefined }, trace: meter.trace }).catch(() => {});
    throw e;
  }
  const { log, ...provenance } = pack.provenance;
  const stored = await store.storeGeneration(env.DB, creature.id, {
    source, bible: pack.bible, catchphrases: pack.catchphrases, model: s.model, prompt: s.prompt, editedBy,
    inputs: { dice: provenance.dice, traits: pet.traits.text, history: history || undefined, life: life || undefined,
              stage: raw.stage },
    cacheKey: source === "genes" && !history ? cacheKey(creature.genome_hash, s) : `${creature.genome_hash}:${source}:${Date.now()}`,
    lines: pack.lines, fallback: pack.fallback,
    provenance: { ...provenance, attempts: log, ...(personality ? { nightly: true, lately: lately || undefined } : {}) },
    personalityId: personality?.id, personalityVersion: personality?.version,
  });
  const sig = await signPack(packText({ genomeHash: creature.genome_hash, version: stored.version, lines: pack.lines }),
                             env.PACK_SIGNING_KEY);
  if (sig) await store.setPackSig(env.DB, stored.packId, sig);
  await storeGenDebug(env, { creatureId: creature.id, packId: stored.packId, source, ok: true, model: s.model, prompt: s.prompt,
    meter: provenance.meter, trace: { replies: meter.trace, checks: log, rejected: provenance.rejected, fallback: pack.fallback } })
    .catch((e) => console.error(JSON.stringify({ msg: "debug store failed", error: String(e?.message ?? e) })));
  return store.latestPack(env.DB, creature.id);
}

async function postPetLines(request, env) {
  let body;
  try { body = await readBody(request); } catch (e) { return fail(400, `bad request: ${e.message}`); }
  const mac = normaliseMac(body.mac);
  if (!mac) return fail(400, "mac: 12 hex digits");
  if (env.PET_LINES_LIMITER) {
    const { success } = await env.PET_LINES_LIMITER.limit({ key: mac });
    if (!success) return fail(429, "slow down");
  }
  let bytes, raw;
  try {
    bytes = hexToBytes(body.genome);
    raw = rawSheet(bytes, { stage: body.stage ?? "adult", feats: Number(body.feats) || 0 });
  } catch (e) {
    return fail(400, String(e.message));
  }
  const s = settings(env);
  const hash = await genomeHash(bytes);
  const name = typeof body.name === "string" ? body.name.slice(0, 15) : "";
  const generation = Number.isInteger(body.generation) ? body.generation : null;
  await store.touchBadge(env.DB, mac, body.fw ?? null);
  const traits = traitSheet(raw, { generation: generation ?? 1, name });
  const up = await store.upsertCreature(env.DB, {
    mac, genomeHash: hash, genomeHex: body.genome.toLowerCase(), species: raw.species, generation,
    lineage: body.lineage ?? null, stage: raw.stage, name, feats: Number(body.feats) || 0, fw: body.fw ?? null,
    raw, traits: traits.text, status: body.egg ? "egg" : "alive",
    parentHash: /^[0-9a-f]{64}$/.test(String(body.parent_genome_hash ?? "")) ? body.parent_genome_hash : null,
  });
  const creature = await store.findCreature(env.DB, mac, hash);

  const have = await store.latestPack(env.DB, creature.id);
  if (have) return json(packReply(have, creature, { cached: true }));

  // The same genome already written elsewhere (a re-flashed badge, a restored
  // keepsake): its pack becomes this creature's first, and costs nothing.
  const twin = await store.packByCacheKey(env.DB, cacheKey(hash, s));
  if (twin) {
    await store.storeGeneration(env.DB, creature.id, {
      source: "genes", bible: twin.bible, catchphrases: JSON.parse(twin.catchphrases_json ?? "[]"), model: s.model,
      prompt: s.prompt, inputs: { copiedFrom: twin.id }, cacheKey: twin.cache_key, lines: JSON.parse(twin.lines_json),
      fallback: JSON.parse(twin.fallback_json), provenance: { ...JSON.parse(twin.provenance_json), copiedFrom: twin.id },
      sig: null,
    });
    const row = await store.latestPack(env.DB, creature.id);
    const sig = await signPack(packText({ genomeHash: hash, version: row.version, lines: JSON.parse(row.lines_json) }), env.PACK_SIGNING_KEY);
    if (sig) { await store.setPackSig(env.DB, row.id, sig); row.sig = sig; }
    return json(packReply(row, creature, { cached: true }));
  }

  const busy = await store.activeJob(env.DB, creature.id);
  if (busy) return json({ status: busy.status, job: busy.id, retry_after_s: 30 }, 202, { "retry-after": "30" });
  if (await store.generationsSince(env.DB, mac, Date.now() - DAY_MS) >= s.dailyCap) {
    return fail(429, "this badge has had its packs for today");
  }
  const job = await store.openJob(env.DB, creature.id, body.egg ? "hatch" : "lines", body.parents ? { parents: body.parents } : null);
  if (body.wait === false) return json({ status: "queued", job, retry_after_s: 60 }, 202, { "retry-after": "60" });
  await store.setJob(env.DB, job, { status: "running", attempt: true });
  try {
    const row = await generateFor(env, creature, { s, source: body.egg ? "hatch" : "genes", carried: body.parents });
    await store.setJob(env.DB, job, { status: "done", packId: row.id });
    return json(packReply(row, creature, { cached: false, created: up.created }));
  } catch (e) {
    // Left queued, so the cron tries again; the badge keeps its built-in lines meanwhile.
    await store.setJob(env.DB, job, { status: "queued", error: String(e.message ?? e) });
    return json({ status: "queued", job, error: "generation failed; it will be retried", retry_after_s: 600 }, 202);
  }
}

async function getPetLines(url, env) {
  const mac = normaliseMac(url.searchParams.get("mac"));
  const hash = String(url.searchParams.get("genome_hash") ?? "").toLowerCase();
  if (!mac || !/^[0-9a-f]{64}$/.test(hash)) return fail(400, "mac and genome_hash");
  const creature = await store.findCreature(env.DB, mac, hash);
  if (!creature) return fail(404, "unknown creature");
  // &version=: one exact version (a line_pack action from the dashboard may name an older one).
  const version = Number(url.searchParams.get("version"));
  if (version) {
    const exact = await env.DB.prepare(
      `SELECT p.*, q.bible, q.catchphrases_json FROM packs p JOIN personalities q ON q.id = p.personality_id
       WHERE p.creature_id = ?1 AND p.version = ?2`).bind(creature.id, version).first();
    return exact ? json(packReply(exact, creature, { cached: true })) : fail(404, "no such version");
  }
  const row = await store.latestPack(env.DB, creature.id);
  if (row) return json(packReply(row, creature, { cached: true }));
  const busy = await store.activeJob(env.DB, creature.id);
  return busy ? json({ status: busy.status, job: busy.id, retry_after_s: 30 }, 202) : fail(404, "no pack yet");
}

async function ackPetLines(request, env) {
  let body;
  try { body = await readBody(request); } catch (e) { return fail(400, `bad request: ${e.message}`); }
  const mac = normaliseMac(body.mac);
  const creature = mac && (await store.findCreature(env.DB, mac, String(body.genome_hash ?? "").toLowerCase()));
  if (!creature) return fail(404, "unknown creature");
  const n = await store.ackPack(env.DB, creature.id, Number(body.version));
  const pack = await env.DB.prepare("SELECT id FROM packs WHERE creature_id = ?1 AND version = ?2").bind(creature.id, Number(body.version)).first();
  if (pack) await recordInstall(env.DB, { mac, creatureId: creature.id, packId: pack.id, version: Number(body.version), via: body.via ?? "wifi" });
  return json({ ok: true, acknowledged: n });
}

async function isAdmin(request, env) {
  const want = env.ADMIN_TOKEN;
  const got = (request.headers.get("authorization") ?? "").replace(/^Bearer\s+/i, "");
  if (!want || !got) return false;
  const a = new TextEncoder().encode(got), b = new TextEncoder().encode(want);
  if (a.byteLength !== b.byteLength) return false;
  if (crypto.subtle.timingSafeEqual) return crypto.subtle.timingSafeEqual(a, b);
  let d = 0;   // node (the tests) has no timingSafeEqual on subtle: the same, by hand
  for (let i = 0; i < a.length; i++) d |= a[i] ^ b[i];
  return d === 0;
}

// The creature an upload or a read names: by genome hash, or by the genome itself.
async function creatureOf(env, mac, body) {
  let hash = String(body.genome_hash ?? "").toLowerCase();
  if (!hash && body.genome) {
    try { hash = await genomeHash(hexToBytes(body.genome)); } catch { return null; }
  }
  return /^[0-9a-f]{64}$/.test(hash) ? store.findCreature(env.DB, mac, hash) : null;
}

async function postStats(request, env) {
  let body;
  try { body = await readBody(request); } catch (e) { return fail(400, `bad request: ${e.message}`); }
  const mac = normaliseMac(body.mac);
  if (!mac) return fail(400, "mac: 12 hex digits");
  if (env.PET_LINES_LIMITER) {
    const { success } = await env.PET_LINES_LIMITER.limit({ key: `stats:${mac}` });
    if (!success) return fail(429, "slow down");
  }
  await store.touchBadge(env.DB, mac, body.fw ?? null);
  const creature = await creatureOf(env, mac, body);
  if (body.stats && !creature) return fail(404, "unknown creature: POST /api/pet-lines first");
  const r = await storeUpload(env.DB, { mac, creatureId: creature?.id ?? null, statsVersion: Number(body.stats_version) || 1,
    stats: body.stats, parents: body.parents, analytics: body.analytics, fw: body.fw,
    periodStart: body.period_start, periodEnd: body.period_end });
  return json({ ok: true, stats_id: r.statsId, analytics_id: r.uploadId });
}

// Device debug (the owner: "store ... debug information in Cloudflare"): boot
// lines, reset and crash reasons, OTA results, remote debug command results
// (#56), logs. Small bodies inline in D1, large ones in R2.
const DEBUG_KINDS = /^[a-z][a-z0-9_]{0,31}$/;
async function postDebug(request, env) {
  let body;
  try { body = await readBody(request); } catch (e) { return fail(400, `bad request: ${e.message}`); }
  const mac = normaliseMac(body.mac);
  if (!mac) return fail(400, "mac: 12 hex digits");
  const items = Array.isArray(body.items) ? body.items : [{ kind: body.kind, summary: body.summary, data: body.data }];
  if (!items.length || items.length > 50 || !items.every((i) => DEBUG_KINDS.test(String(i?.kind ?? "")))) {
    return fail(400, "kind: a short lower-case word (boot, reset, crash, ota, command, log); at most 50 items");
  }
  if (env.PET_LINES_LIMITER) {
    const { success } = await env.PET_LINES_LIMITER.limit({ key: `debug:${mac}` });
    if (!success) return fail(429, "slow down");
  }
  await store.touchBadge(env.DB, mac, body.fw ?? null);
  const creature = await creatureOf(env, mac, body);
  const ids = [];
  for (const i of items) {
    ids.push(await storeDeviceDebug(env, { mac, creatureId: creature?.id ?? null, kind: i.kind, fw: body.fw ?? null,
      via: body.via === "phone" ? "phone" : "wifi", summary: i.summary ?? null, body: i.data ?? null }));
  }
  return json({ ok: true, ids });
}

async function getDiary(url, env) {
  const mac = normaliseMac(url.searchParams.get("mac"));
  const creature = mac && (await creatureOf(env, mac, { genome_hash: url.searchParams.get("genome_hash") }));
  if (!creature) return fail(404, "unknown creature");
  return json({ creature_id: creature.id, entries: await diaryFor(env.DB, creature.id) });
}

// The phone (#42, #43). Web Bluetooth never shows the badge's MAC, so the phone
// names the pet by its genome, which it reads from the badge: the hash to look,
// the whole genome to change anything.
const SAMPLE = ["peckish", "crunch", "heard_you", "warm_hands", "sky_down", "moon", "p_doom", "p_lucky", "rim_walk",
  "brave", "dream_feast", "verdict_yes"];
async function newestByHash(env, hash) {
  return env.DB.prepare("SELECT * FROM creatures WHERE genome_hash = ?1 ORDER BY last_seen DESC LIMIT 1").bind(hash).first();
}
async function getPet(url, env) {
  const hash = String(url.searchParams.get("genome_hash") ?? "").toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(hash)) return fail(400, "genome_hash: 64 hex digits");
  const c = await newestByHash(env, hash);
  if (!c) return fail(404, "this pet has not been online yet");
  const pack = await store.latestPack(env.DB, c.id);
  const lines = pack ? JSON.parse(pack.lines_json) : {};
  const kin = async (sql, v) => (await env.DB.prepare(sql).bind(v).all()).results ?? [];
  const parent = c.parent_id ? await env.DB.prepare(
    `SELECT c.id, c.species, c.name, c.generation, c.status, c.genome_hash,
       (SELECT bible FROM personalities q WHERE q.creature_id = c.id ORDER BY version DESC LIMIT 1) AS bible
     FROM creatures c WHERE c.id = ?1`).bind(c.parent_id).first() : null;
  return json({
    species: c.species, name: c.name, generation: c.generation, stage: c.stage, status: c.status,
    bible: pack?.bible ?? null, catchphrases: pack ? JSON.parse(pack.catchphrases_json ?? "[]") : [],
    version: pack?.version ?? null, personality_version: pack?.personality_version ?? null,
    lines: Object.fromEntries(SAMPLE.filter((k) => lines[k]).map((k) => [k, lines[k]])),
    diary: await diaryFor(env.DB, c.id, 14),
    family: {
      parent, children: await kin("SELECT species, name, generation, status, genome_hash FROM creatures WHERE parent_id = ?1 ORDER BY generation", c.id),
    },
  });
}
async function postPersonality(request, env) {
  let body;
  try { body = await readBody(request); } catch (e) { return fail(400, `bad request: ${e.message}`); }
  let bytes;
  try { bytes = hexToBytes(body.genome); } catch (e) { return fail(400, e.message); }
  const bible = typeof body.bible === "string" ? body.bible.trim() : "";
  if (bible.length < 20 || bible.length > 4000) return fail(400, "bible: 20 to 4000 characters");
  const hash = await genomeHash(bytes);
  const c = await newestByHash(env, hash);
  if (!c) return fail(404, "this pet has not been online yet");
  if (env.PET_LINES_LIMITER) {
    const { success } = await env.PET_LINES_LIMITER.limit({ key: `edit:${c.mac}` });
    if (!success) return fail(429, "slow down");
  }
  const s = settings(env);
  if (await store.generationsSince(env.DB, c.mac, Date.now() - DAY_MS) >= s.dailyCap) return fail(429, "this badge has had its packs for today");
  const row = await generateFor(env, c, { s, source: "edit", bible, editedBy: "phone" });
  // The badge picks it up at its next Wi-Fi window (or the phone pushes it, once the firmware takes packs over BLE).
  await enqueue(env.DB, { macs: [c.mac], kind: "line_pack", createdBy: "phone",
    payload: { pack_id: row.id, version: row.version, genome_hash: c.genome_hash } });
  return json(packReply(row, c, { cached: false }));
}

// #47: after the diaries, decide per pet whether his life has moved on enough
// to rewrite him. Yes when he has grown into a new stage since his personality
// was written, when he has won or lost fights or gone hungry since, or when a
// week of new diary has piled up. A revision is a new personality and pack
// version (history kept) waiting for the badge's next nightly window.
const FIGHTS = /fight|duel|won|lost|win|loss|starv|sick|hurt|died|death/i;
export async function revisionReason(db, c) {
  const p = await db.prepare(
    "SELECT created_at, inputs_json FROM personalities WHERE creature_id = ?1 ORDER BY version DESC LIMIT 1").bind(c.id).first();
  if (!p) return null;
  const inputs = JSON.parse(p.inputs_json || "{}");
  if (c.stage && inputs.stage && c.stage !== inputs.stage) return `grew from ${inputs.stage} to ${c.stage}`;
  const since = (await db.prepare(
    "SELECT stats_json FROM life_stats WHERE creature_id = ?1 AND uploaded_at > ?2 ORDER BY id").bind(c.id, p.created_at).all()).results ?? [];
  const before = await db.prepare(
    "SELECT stats_json FROM life_stats WHERE creature_id = ?1 AND uploaded_at <= ?2 ORDER BY id DESC LIMIT 1").bind(c.id, p.created_at).first();
  if (since.length) {
    const d = statsDelta(JSON.parse(since.at(-1).stats_json), before ? JSON.parse(before.stats_json) : null);
    const big = Object.entries(d).filter(([k, v]) => FIGHTS.test(k) && v > 0);
    if (big.length) return big.map(([k, v]) => `${k} +${v}`).join(", ");
  }
  const entries = (await db.prepare("SELECT COUNT(*) AS n FROM diary_entries WHERE creature_id = ?1 AND created_at > ?2")
    .bind(c.id, p.created_at).first())?.n ?? 0;
  return entries >= 7 ? `${entries} diary entries since he was last written` : null;
}

// #47 as the owner put it: "generate new lines, using the same generated
// personality, every night and push them down." From 04:00 UTC, every living
// creature with a personality gets a fresh pack written from its STORED bible,
// with its latest stats and diary as fresh material, and none of the lines of
// its last three packs. The bible changes only on the life triggers
// (revisionReason: a new stage, fights or hunger, a week of diary) or an
// owner's edit (#43). A creature is done for the night once it has a pack
// newer than tonight's 04:00, so the 10-minute cron finishes whatever one run
// had no time for.
export const NIGHT_START_HOUR = 4, NIGHT_END_HOUR = 10;   // UTC
export function nightStart(t = Date.now()) {
  const d = new Date(t);
  const start = Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), NIGHT_START_HOUR);
  return t >= start ? start : start - 24 * 3600 * 1000;
}

async function nightlyOne(env, c) {
  const cur = await env.DB.prepare(
    "SELECT id, version, bible, catchphrases_json FROM personalities WHERE creature_id = ?1 ORDER BY version DESC LIMIT 1")
    .bind(c.id).first();
  if (!cur) return null;
  const stats = await env.DB.prepare("SELECT stats_json FROM life_stats WHERE creature_id = ?1 ORDER BY id DESC LIMIT 1").bind(c.id).first();
  const diary = (await diaryFor(env.DB, c.id, 7)).reverse();
  const recent = [stats ? `His counters: ${stats.stats_json.slice(0, 800)}` : "",
    ...diary.map((d) => `Diary ${d.day}: ${d.entry}`)].filter(Boolean).join("\n");
  const avoid = await store.recentLines(env.DB, c.id, 3);
  const why = await revisionReason(env.DB, c);
  const row = why
    // His life moved on: the bible is rewritten too (a new personality version).
    ? await generateFor(env, c, { source: "nightly", life: [`Why now: ${why}.`, recent].join("\n"), currentBible: cur.bible, avoid })
    // Every other night: the same personality, new lines.
    : await generateFor(env, c, { source: "nightly", lately: recent, avoid,
        personality: { id: cur.id, version: cur.version, bible: cur.bible, catchphrases: JSON.parse(cur.catchphrases_json ?? "[]") } });
  await enqueue(env.DB, { macs: [c.mac], kind: "line_pack", createdBy: "nightly",
    payload: { pack_id: row.id, version: row.version, genome_hash: c.genome_hash } });
  return { creature: c.id, version: row.version, revised: !!why };
}

export async function runNightly(env, { limit = 12, concurrency = 4, now = Date.now() } = {}) {
  const since = nightStart(now);
  const due = (await env.DB.prepare(
    `SELECT c.* FROM creatures c WHERE c.status = 'alive'
       AND EXISTS (SELECT 1 FROM personalities q WHERE q.creature_id = c.id)
       AND NOT EXISTS (SELECT 1 FROM packs p WHERE p.creature_id = c.id AND p.created_at >= ?1)
     ORDER BY c.last_seen DESC LIMIT ?2`).bind(since, limit).all()).results ?? [];
  const done = [];
  for (let i = 0; i < due.length; i += concurrency) {
    const res = await Promise.allSettled(due.slice(i, i + concurrency).map((c) => nightlyOne(env, c)));
    res.forEach((r, k) => {
      if (r.status === "fulfilled" && r.value) done.push(r.value);
      else if (r.status === "rejected") console.error(JSON.stringify({ msg: "nightly pack failed", creature: due[i + k].id, error: String(r.reason?.message ?? r.reason) }));
    });
  }
  return done;
}

export async function runQueuedJobs(env, limit = 3) {
  const jobs = await store.queuedJobs(env.DB, limit);
  for (const j of jobs) {
    await store.setJob(env.DB, j.job_id, { status: "running", attempt: true });
    try {
      const input = (j.input_json && JSON.parse(j.input_json)) || {};
      const row = await generateFor(env, { id: j.creature_id, genome_hex: j.genome_hex, genome_hash: j.genome_hash,
        stage: j.stage, generation: j.generation, name: j.name, feats: j.feats },
        { source: j.kind === "lines" ? "genes" : j.kind, carried: input.parents, bible: input.bible, editedBy: input.editedBy });
      await store.setJob(env.DB, j.job_id, { status: "done", packId: row.id });
    } catch (e) {
      await store.setJob(env.DB, j.job_id, { status: j.attempts + 1 >= 3 ? "failed" : "queued", error: String(e.message ?? e) });
    }
  }
  return jobs.length;
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    try {
      if (url.pathname === "/api/pet-lines" && request.method === "POST") return await postPetLines(request, env, ctx);
      if (url.pathname === "/api/pet-lines" && request.method === "GET") return await getPetLines(url, env);
      if (url.pathname === "/api/pet-lines/ack" && request.method === "POST") return await ackPetLines(request, env);
      if (url.pathname === "/api/stats" && request.method === "POST") return await postStats(request, env);
      if (url.pathname === "/api/debug" && request.method === "POST") return await postDebug(request, env);
      if (url.pathname === "/api/pet" && request.method === "GET") return await getPet(url, env);
      if (url.pathname === "/api/personality" && request.method === "POST") return await postPersonality(request, env);
      if (url.pathname === "/api/diary" && request.method === "GET") return await getDiary(url, env);
      if (url.pathname === "/api/creatures" && request.method === "GET") {
        if (!(await isAdmin(request, env))) return fail(401, "admin token");
        return json(await store.exportAll(env.DB, { mac: normaliseMac(url.searchParams.get("mac")) }));
      }
      // The dashboard page carries no data; everything it shows comes from /api/admin/* with the token.
      if (url.pathname === "/admin" || url.pathname === "/admin/") {
        return new Response(ADMIN_HTML, { headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store",
          "x-robots-tag": "noindex", "content-security-policy": "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'" } });
      }
      const helpers = { json, fail, readBody, generateFor, normaliseMac };
      if (url.pathname.startsWith("/api/admin/")) {
        if (!(await isAdmin(request, env))) return fail(401, "admin token");
        return await handleAdmin(request, url, env, helpers);
      }
      if (url.pathname.startsWith("/api/badge/")) return await handleBadge(request, url, env, helpers);
      if (url.pathname.startsWith("/api/")) return fail(404, "no such endpoint");
      if (url.pathname.startsWith("/fw/")) return await firmware(request, env, decodeURIComponent(url.pathname.slice(4)));
      return env.ASSETS.fetch(request);
    } catch (e) {
      console.error(JSON.stringify({ msg: "unhandled", path: url.pathname, error: String(e?.stack ?? e) }));
      return fail(500, "internal error");
    }
  },
  // A cron run awaits its work (it has minutes, not the request's waitUntil budget).
  async scheduled(controller, env) {
    if (controller.cron === (env.DIARY_CRON || DIARY_CRON)) {
      await runDiaries(env);
      await runNightly(env);
      await purgeOld(env);   // keeps everything unless RETENTION_DAYS is set
    } else {
      await runQueuedJobs(env);
      // The night's packs a 04:00 run had no time for.
      const hour = new Date(controller.scheduledTime ?? Date.now()).getUTCHours();
      if (hour >= NIGHT_START_HOUR && hour < NIGHT_END_HOUR) await runNightly(env, { limit: 6 });
    }
  },
};
