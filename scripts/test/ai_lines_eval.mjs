#!/usr/bin/env node
// The AI lines eval (#37): every candidate model and prompt version over the
// fixed 20 fixture pets (web/test/fixtures/genomes.json, two per species, from
// tools/trait_sheet.cpp), through the Worker's own pipeline (web/src/pipeline.js)
// against Workers AI's REST API. Writes every pack, the automatic metrics, the
// judge's scores and a summary under outputs/ai_lines_eval/<run>/ (gitignored);
// scripts/test/ai_lines/sheet.mjs turns a run into the owner's sample sheet.
//
//   node scripts/test/ai_lines_eval.mjs --run r1 --models kimi,gemma --prompts v2 [--pets mudge-a,mudge-b]
//        [--judges @cf/deepseek-ai/deepseek-v4-pro-0813] [--concurrency 6] [--no-judge]
//
// Re-running a run name reuses the packs and scores already on disk, so a
// crash or a new judge costs only what is missing.
import { mkdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { hexToBytes, rawSheet, genomeHash } from "../../web/src/genome.js";
import { generatePack, MODELS } from "../../web/src/pipeline.js";
import { SITUATIONS } from "../../web/src/validate.js";
import { restAI } from "./ai_lines/rest_runner.mjs";
import { mechanics, overlap, allLines, cosine } from "./ai_lines/metrics.mjs";
import { judgePack, judgePair } from "./ai_lines/judge.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../..");
const SHORT = {
  kimi: "@cf/moonshotai/kimi-k2.6", deepseek: "@cf/deepseek-ai/deepseek-v4.1-flash-1017",
  gemma: "@cf/google/gemma-4-26b-a4b-it", mistral: "@cf/mistralai/mistral-small-3.1-24b-instruct",
  llama: "@cf/meta/llama-3.3-70b-instruct-fp8-fast", gptoss: "@cf/openai/gpt-oss-120b", qwen: "@cf/qwen/qwen3.8-27b",
};
const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith("--")) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith("--") ? all[i + 1] : true]);
  return acc;
}, []));
const run = args.run || "default";
const models = String(args.models || "kimi,deepseek,gemma,mistral,llama,gptoss").split(",").map((m) => SHORT[m] ?? m);
const prompts = String(args.prompts || "v2").split(",");
const judges = String(args.judges || "@cf/deepseek-ai/deepseek-v4-pro-0813").split(",");
const concurrency = Number(args.concurrency || 6);
const fixtures = JSON.parse(readFileSync(join(ROOT, "web/test/fixtures/genomes.json")));
const pets = args.pets ? fixtures.filter((f) => String(args.pets).split(",").includes(f.id)) : fixtures;
const dir = join(ROOT, "outputs/ai_lines_eval", run);
for (const d of ["packs", "judge", "embed"]) mkdirSync(join(dir, d), { recursive: true });
const ai = restAI();
const slug = (m) => m.split("/").pop();

async function pool(items, n, fn) {
  const out = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(n, items.length) }, async () => {
    while (next < items.length) {
      const i = next++;
      out[i] = await fn(items[i], i);
    }
  }));
  return out;
}
const readJson = (p) => (existsSync(p) ? JSON.parse(readFileSync(p)) : null);
const writeJson = (p, v) => writeFileSync(p, JSON.stringify(v, null, 1));

async function petOf(f) {
  const bytes = hexToBytes(f.genome);
  return { id: f.id, species: f.raw.species, raw: rawSheet(bytes, { stage: f.raw.stage }), generation: f.generation,
           hash: await genomeHash(bytes), name: "" };
}

// ---- 1. packs ----
const jobs = [];
// Interleaved, so the pool spreads over models instead of queueing on one model's capacity.
for (const f of pets) for (const prompt of prompts) for (const model of models) jobs.push({ model, prompt, f });
console.log(`run ${run}: ${jobs.length} packs (${models.length} models x ${prompts.length} prompts x ${pets.length} pets)`);
let done = 0;
await pool(jobs, concurrency, async ({ model, prompt, f }) => {
  const path = join(dir, "packs", `${slug(model)}__${prompt}__${f.id}.json`);
  if (existsSync(path)) { done++; return; }
  const pet = await petOf(f);
  try {
    const pack = await generatePack(ai, pet, { model, prompt });
    writeJson(path, { pet: { id: f.id, species: pet.species, generation: f.generation, raw: pet.raw }, ...pack });
    console.log(`[${++done}/${jobs.length}] ${slug(model)} ${prompt} ${f.id}: ${Object.keys(pack.lines).length} situations, ${pack.fallback.length} fallback, ${(pack.provenance.ms / 1000).toFixed(0)} s`);
  } catch (e) {
    writeJson(path.replace(/\.json$/, ".error.json"), { error: String(e.message ?? e) });
    console.log(`[${++done}/${jobs.length}] ${slug(model)} ${prompt} ${f.id}: FAILED ${e.message}`);
  }
});

const packOf = (model, prompt, id) => readJson(join(dir, "packs", `${slug(model)}__${prompt}__${id}.json`));

// ---- 2. embeddings of each pack's lines, for distinctness between same-species pets ----
async function embedOf(model, prompt, id) {
  const path = join(dir, "embed", `${slug(model)}__${prompt}__${id}.json`);
  const have = readJson(path);
  if (have) return have;
  const pack = packOf(model, prompt, id);
  if (!pack) return null;
  const r = await ai.run("@cf/baai/bge-m3", { text: [pack.bible, allLines(pack).join(". ")] });
  const v = { bible: r.data[0], lines: r.data[1] };
  writeJson(path, v);
  return v;
}

// ---- 3. judge ----
if (!args["no-judge"]) {
  const jj = [];
  for (const judge of judges) for (const model of models) for (const prompt of prompts) for (const f of pets) jj.push({ judge, model, prompt, f });
  await pool(jj, concurrency, async ({ judge, model, prompt, f }) => {
    const path = join(dir, "judge", `${slug(judge)}__${slug(model)}__${prompt}__${f.id}.json`);
    if (existsSync(path)) return;
    const pack = packOf(model, prompt, f.id);
    if (!pack) return;
    try {
      const s = await judgePack(ai, judge, { species: f.raw.species }, pack);
      if (s) writeJson(path, s);
    } catch (e) { console.log(`judge ${slug(judge)} ${f.id}: ${e.message}`); }
  });
  const pairs = [];
  for (const judge of judges) for (const model of models) for (const prompt of prompts) {
    const bySpecies = {};
    for (const f of pets) (bySpecies[f.raw.species] ??= []).push(f.id);
    for (const ids of Object.values(bySpecies)) if (ids.length === 2) pairs.push({ judge, model, prompt, ids });
  }
  await pool(pairs, concurrency, async ({ judge, model, prompt, ids }) => {
    const path = join(dir, "judge", `${slug(judge)}__${slug(model)}__${prompt}__pair_${ids.join("+")}.json`);
    if (existsSync(path)) return;
    const a = packOf(model, prompt, ids[0]), b = packOf(model, prompt, ids[1]);
    if (!a || !b) return;
    try {
      const s = await judgePair(ai, judge, a.pet, a, b.pet, b);
      if (s) writeJson(path, s);
    } catch (e) { console.log(`pair judge ${ids}: ${e.message}`); }
  });
}

// ---- 4. summary ----
const mean = (xs) => { const v = xs.filter((x) => Number.isFinite(x)); return v.length ? v.reduce((a, b) => a + b, 0) / v.length : NaN; };
const summary = [];
for (const model of models) for (const prompt of prompts) {
  const packs = pets.map((f) => [f, packOf(model, prompt, f.id)]).filter(([, p]) => p);
  const failed = pets.length - packs.length;
  const mech = packs.map(([, p]) => mechanics(p, SITUATIONS.length));
  const reasons = {};
  for (const m of mech) for (const [k, v] of Object.entries(m.rejectReasons)) reasons[k] = (reasons[k] ?? 0) + v;
  const bySpecies = {};
  for (const [f, p] of packs) (bySpecies[f.raw.species] ??= []).push([f, p]);
  const ngramOverlap = [], embedSim = [], bibleSim = [];
  for (const pair of Object.values(bySpecies)) {
    if (pair.length !== 2) continue;
    ngramOverlap.push(overlap(allLines(pair[0][1]), allLines(pair[1][1])));
    const ea = await embedOf(model, prompt, pair[0][0].id), eb = await embedOf(model, prompt, pair[1][0].id);
    if (ea && eb) { embedSim.push(cosine(ea.lines, eb.lines)); bibleSim.push(cosine(ea.bible, eb.bible)); }
  }
  const judged = {};
  for (const judge of judges) {
    const s = pets.map((f) => readJson(join(dir, "judge", `${slug(judge)}__${slug(model)}__${prompt}__${f.id}.json`))).filter(Boolean);
    const pairsJ = Object.values(bySpecies).filter((p) => p.length === 2)
      .map((p) => readJson(join(dir, "judge", `${slug(judge)}__${slug(model)}__${prompt}__pair_${p.map(([f]) => f.id).join("+")}.json`)))
      .filter(Boolean);
    judged[slug(judge)] = { n: s.length, voice: mean(s.map((x) => x.voice)), specificity: mean(s.map((x) => x.specificity)),
                            humour: mean(s.map((x) => x.humour)), distinctness: mean(pairsJ.map((x) => x.distinctness)) };
  }
  summary.push({
    model, prompt, packs: packs.length, failed,
    jsonFirstPass: mean(mech.map((m) => m.jsonFirstPass)), passFirst: mean(mech.map((m) => m.passFirst)),
    coverage: mean(mech.map((m) => m.coverage)), fallbackTotal: mech.reduce((a, m) => a + m.fallback, 0),
    retries: mean(mech.map((m) => m.retries)), perSituation: mean(mech.map((m) => m.perSituation)),
    meanLen: mean(mech.map((m) => m.meanLen)), distinct2: mean(mech.map((m) => m.distinct2)),
    repeatedOpenings: mean(mech.map((m) => m.repeatedOpenings)),
    sameSpeciesNgramOverlap: mean(ngramOverlap), sameSpeciesLineSim: mean(embedSim), sameSpeciesBibleSim: mean(bibleSim),
    seconds: mean(mech.map((m) => m.ms / 1000)), usdPerPet: mean(mech.map((m) => m.usd)), rejectReasons: reasons, judged,
  });
}
writeJson(join(dir, "summary.json"), { run, models, prompts, judges, pets: pets.map((p) => p.id), summary });
const f2 = (x) => (Number.isFinite(x) ? x.toFixed(2) : "-");
console.log("\nmodel | prompt | packs | json1 | pass1 | coverage | retries | len | distinct2 | sameSpp overlap | sameSpp sim | s/pet | $/pet | " +
  judges.map((j) => `${slug(j)} V/S/H/D`).join(" | "));
for (const s of summary) {
  console.log([slug(s.model), s.prompt, `${s.packs}/${s.packs + s.failed}`, f2(s.jsonFirstPass), f2(s.passFirst), f2(s.coverage),
    f2(s.retries), f2(s.meanLen), f2(s.distinct2), f2(s.sameSpeciesNgramOverlap), f2(s.sameSpeciesLineSim), f2(s.seconds),
    s.usdPerPet.toFixed(4), ...Object.values(s.judged).map((j) => `${f2(j.voice)}/${f2(j.specificity)}/${f2(j.humour)}/${f2(j.distinctness)}`)].join(" | "));
}
console.log(`\nwrote ${join(dir, "summary.json")}`);
