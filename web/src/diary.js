// #38's uploads and #39's diary. A badge (or the phone, for it) uploads its
// pet's life stats, and with them its system analytics (#50); once a day the
// cron writes each pet with new stats a short diary entry in his own voice.
import catalog from "./catalog.json" with { type: "json" };
import { call, newMeter } from "./pipeline.js";
import { parseJsonReply } from "./validate.js";

const MAX_ENTRY = 700;

// Numeric leaves of a JSON object as dotted paths; an array of numbers is a
// histogram, one row per bucket.
export function flatten(obj, prefix = "", out = []) {
  if (obj == null) return out;
  if (Array.isArray(obj)) {
    obj.forEach((v, i) => {
      if (typeof v === "number" && Number.isFinite(v)) out.push({ metric: prefix, dims: { bucket: i }, value: v });
      else if (v && typeof v === "object") flatten(v, `${prefix}[${i}]`, out);
    });
    return out;
  }
  if (typeof obj !== "object") return out;
  for (const [k, v] of Object.entries(obj)) {
    const key = prefix ? `${prefix}.${k}` : k;
    if (typeof v === "number" && Number.isFinite(v)) out.push({ metric: key, dims: null, value: v });
    else if (typeof v === "boolean") out.push({ metric: key, dims: null, value: v ? 1 : 0 });
    else if (v && typeof v === "object") flatten(v, key, out);
  }
  return out;
}

export async function storeUpload(db, { mac, creatureId, statsVersion, stats, parents, analytics, fw, periodStart, periodEnd }) {
  const t = Date.now();
  let statsId = null, uploadId = null;
  if (stats && typeof stats === "object") {
    const r = await db.prepare(
      `INSERT INTO life_stats (mac, creature_id, stats_version, stats_json, parents_json, uploaded_at)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6)`).bind(mac, creatureId, statsVersion ?? 1, JSON.stringify(stats),
       parents ? JSON.stringify(parents) : null, t).run();
    statsId = r.meta.last_row_id;
  }
  if (analytics && typeof analytics === "object") {
    const r = await db.prepare(
      `INSERT INTO analytics_uploads (mac, creature_id, fw, period_start, period_end, payload_json, uploaded_at)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)`).bind(mac, creatureId ?? null, fw ?? null, periodStart ?? null,
       periodEnd ?? null, JSON.stringify(analytics), t).run();
    uploadId = r.meta.last_row_id;
    const rows = flatten(analytics).slice(0, 2000);
    for (const m of rows) {
      await db.prepare("INSERT INTO analytics_metrics (upload_id, mac, ts, metric, dims_json, value) VALUES (?1, ?2, ?3, ?4, ?5, ?6)")
        .bind(uploadId, mac, periodEnd ?? t, m.metric, m.dims ? JSON.stringify(m.dims) : null, m.value).run();
    }
  }
  return { statsId, uploadId };
}

// What changed since the stats the last entry was written from: counters as deltas.
export function statsDelta(now, before) {
  const out = {};
  for (const { metric, value } of flatten(now)) {
    const prev = before ? flatten(before).find((m) => m.metric === metric)?.value ?? 0 : 0;
    if (value !== prev) out[metric] = before ? value - prev : value;
  }
  return out;
}

function diaryMessages({ species, name, stage, bible, delta, totals, lastEntry, day }) {
  const notes = catalog.species[species]?.notes ?? "";
  return [
    { role: "system", content: "You write the diary of a tiny pet who lives on a kid's badge, in the pet's own voice. Reply with JSON only." },
    { role: "user", content:
`Who he is (his character bible):
${bible}

His species, in the game writer's words:
<<<
${notes.slice(0, 1800)}
>>>

He is ${name ? `called ${name}, ` : ""}${stage ? `${stage}, ` : ""}and today is ${day}.
What happened since his last entry (counters from the badge; a number is how many more times):
${Object.entries(delta).map(([k, v]) => `- ${k}: ${v}`).join("\n") || "- nothing much"}
His life so far: ${Object.entries(totals).slice(0, 30).map(([k, v]) => `${k} ${v}`).join(", ")}
${lastEntry ? `His previous entry, for continuity: ${lastEntry}` : "This is his first entry."}

Write today's diary entry: 50 to 90 words, first person, in his voice, about what actually happened (the numbers above, turned into moments; never list them). Written for a kid of 6 to 13 with a wink for a parent; funny, dark, sad, cheeky or weird as his character is.

Reply as JSON: {"entry": "..."}` },
  ];
}

// The newest stats of each creature with stats newer than its last diary entry.
export async function creaturesDueForDiary(db, limit) {
  const r = await db.prepare(
    `SELECT c.id, c.species, c.name, c.stage, s.id AS stats_id, s.stats_json, s.uploaded_at,
       (SELECT d.stats_id FROM diary_entries d WHERE d.creature_id = c.id ORDER BY d.day DESC LIMIT 1) AS last_stats_id,
       (SELECT d.entry FROM diary_entries d WHERE d.creature_id = c.id ORDER BY d.day DESC LIMIT 1) AS last_entry,
       (SELECT q.bible FROM personalities q WHERE q.creature_id = c.id ORDER BY q.version DESC LIMIT 1) AS bible
     FROM creatures c JOIN life_stats s ON s.id = (SELECT MAX(id) FROM life_stats WHERE creature_id = c.id)
     WHERE c.status != 'dead' OR c.died_at > ?2
     LIMIT ?1`).bind(limit * 4, Date.now() - 2 * 24 * 3600 * 1000).all();
  return (r.results ?? []).filter((c) => c.bible && c.stats_id !== c.last_stats_id).slice(0, limit);
}

export async function writeDiaryEntry(env, c, day, { model }) {
  const before = c.last_stats_id
    ? await env.DB.prepare("SELECT stats_json FROM life_stats WHERE id = ?1").bind(c.last_stats_id).first()
    : null;
  const now = JSON.parse(c.stats_json);
  const delta = statsDelta(now, before ? JSON.parse(before.stats_json) : null);
  const totals = Object.fromEntries(flatten(now).map((m) => [m.metric, m.value]));
  let messages = diaryMessages({ species: c.species, name: c.name, stage: c.stage, bible: c.bible, delta, totals,
                                 lastEntry: c.last_entry, day });
  const meter = newMeter();
  for (let attempt = 0; attempt < 3; attempt++) {
    const reply = await call(env.AI, model, messages, { maxTokens: 500, meter });
    const entry = String(parseJsonReply(reply)?.entry ?? "").trim();
    if (entry && entry.length <= MAX_ENTRY) {
      await env.DB.prepare(
        `INSERT INTO diary_entries (creature_id, day, entry, stats_id, model, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)
         ON CONFLICT (creature_id, day) DO UPDATE SET entry = ?3, stats_id = ?4, model = ?5, created_at = ?6`)
        .bind(c.id, day, entry, c.stats_id, model, Date.now()).run();
      return entry;
    }
    messages = [...messages, { role: "assistant", content: reply || "(empty)" },
      { role: "user", content: `That was not usable (${entry ? `${entry.length} characters; at most ${MAX_ENTRY}` : "no entry"}). Reply as JSON: {"entry": "..."}` }];
  }
  throw new Error("no usable diary entry");
}

// The cron's diary pass: a few at a time, so one slow model call does not hold up the rest.
export async function runDiaries(env, { limit = 40, day = new Date().toISOString().slice(0, 10), model } = {}) {
  model ??= env.DIARY_MODEL || env.LINES_MODEL;
  const due = await creaturesDueForDiary(env.DB, limit);
  let written = 0;
  for (let i = 0; i < due.length; i += 4) {
    const res = await Promise.allSettled(due.slice(i, i + 4).map((c) => writeDiaryEntry(env, c, day, { model })));
    written += res.filter((r) => r.status === "fulfilled").length;
    for (const r of res) if (r.status === "rejected") console.error(JSON.stringify({ msg: "diary failed", error: String(r.reason) }));
  }
  return written;
}

export async function diaryFor(db, creatureId, limit = 30) {
  const r = await db.prepare("SELECT day, entry FROM diary_entries WHERE creature_id = ?1 ORDER BY day DESC LIMIT ?2")
    .bind(creatureId, limit).all();
  return r.results ?? [];
}
