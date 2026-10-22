// blorb.hypnodroid.com's Worker: the static site (public/) as before, plus the
// AI personality lines API (#37). Only /api/* reaches this code
// (wrangler.jsonc run_worker_first); everything else is the assets.
//
//   POST /api/pet-lines        {mac, genome, stage?, name?, generation?, lineage?, feats?, fw?, wait?}
//                              -> 200 {bible, lines, version, text, sig, ...} | 202 {status: "queued"}
//   GET  /api/pet-lines?mac=&genome_hash=   the newest pack, or 202 while it is being written
//   POST /api/pet-lines/ack    {mac, genome_hash, version}: the badge stored it
//   POST /api/stats            {mac, genome | genome_hash, stats_version?, stats?, parents?, analytics?, fw?}  (#38, #50)
//   GET  /api/diary?mac=&genome_hash=   the pet's diary, newest first (#39, for the phone #42)
//   GET  /api/creatures[?mac=] everything the server holds (Authorization: Bearer ADMIN_TOKEN)
//
// Crons (wrangler.jsonc triggers): every 10 minutes, the packs a POST queued
// instead of waiting for; once a day (DIARY_CRON), each pet's diary entry.
import { hexToBytes, rawSheet, genomeHash } from "./genome.js";
import { traitSheet } from "./traits.js";
import { generatePack, DEFAULT_MODEL, DEFAULT_PROMPT } from "./pipeline.js";
import { packText, signPack } from "./pack.js";
import * as store from "./store.js";
import { storeUpload, runDiaries, diaryFor } from "./diary.js";

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
export async function generateFor(env, creature, { source = "genes", s = settings(env) } = {}) {
  const bytes = hexToBytes(creature.genome_hex);
  const raw = rawSheet(bytes, { stage: creature.stage ?? "adult", feats: creature.feats ?? 0 });
  const pet = { raw, generation: creature.generation ?? 1, name: creature.name ?? "", hash: creature.genome_hash };
  pet.traits = traitSheet(raw, { generation: pet.generation, name: pet.name });
  const pack = await generatePack(env.AI, pet, { model: s.model, prompt: s.prompt });
  const { log, ...provenance } = pack.provenance;
  const stored = await store.storeGeneration(env.DB, creature.id, {
    source, bible: pack.bible, catchphrases: pack.catchphrases, model: s.model, prompt: s.prompt,
    inputs: { dice: provenance.dice, traits: pet.traits.text }, cacheKey: cacheKey(creature.genome_hash, s),
    lines: pack.lines, fallback: pack.fallback, provenance: { ...provenance, attempts: log },
  });
  const sig = await signPack(packText({ genomeHash: creature.genome_hash, version: stored.version, lines: pack.lines }),
                             env.PACK_SIGNING_KEY);
  if (sig) await store.setPackSig(env.DB, stored.packId, sig);
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
  const job = await store.openJob(env.DB, creature.id, "lines", null);
  if (body.wait === false) return json({ status: "queued", job, retry_after_s: 60 }, 202, { "retry-after": "60" });
  await store.setJob(env.DB, job, { status: "running", attempt: true });
  try {
    const row = await generateFor(env, creature, { s });
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

async function getDiary(url, env) {
  const mac = normaliseMac(url.searchParams.get("mac"));
  const creature = mac && (await creatureOf(env, mac, { genome_hash: url.searchParams.get("genome_hash") }));
  if (!creature) return fail(404, "unknown creature");
  return json({ creature_id: creature.id, entries: await diaryFor(env.DB, creature.id) });
}

export async function runQueuedJobs(env, limit = 3) {
  const jobs = await store.queuedJobs(env.DB, limit);
  for (const j of jobs) {
    await store.setJob(env.DB, j.job_id, { status: "running", attempt: true });
    try {
      const row = await generateFor(env, { id: j.creature_id, genome_hex: j.genome_hex, genome_hash: j.genome_hash,
        stage: j.stage, generation: j.generation, name: j.name, feats: j.feats }, { source: j.kind === "lines" ? "genes" : j.kind });
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
      if (url.pathname === "/api/diary" && request.method === "GET") return await getDiary(url, env);
      if (url.pathname === "/api/creatures" && request.method === "GET") {
        if (!(await isAdmin(request, env))) return fail(401, "admin token");
        return json(await store.exportAll(env.DB, { mac: normaliseMac(url.searchParams.get("mac")) }));
      }
      if (url.pathname.startsWith("/api/")) return fail(404, "no such endpoint");
      return env.ASSETS.fetch(request);
    } catch (e) {
      console.error(JSON.stringify({ msg: "unhandled", path: url.pathname, error: String(e?.stack ?? e) }));
      return fail(500, "internal error");
    }
  },
  // A cron run awaits its work (it has minutes, not the request's waitUntil budget).
  async scheduled(controller, env) {
    if (controller.cron === (env.DIARY_CRON || DIARY_CRON)) await runDiaries(env);
    else await runQueuedJobs(env);
  },
};
