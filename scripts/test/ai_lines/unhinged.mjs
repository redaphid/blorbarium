#!/usr/bin/env node
// Run r2 (the owner: "the LLM that will produce the most unhinged lines on
// Cloudflare AI that still make sense"). Packs come from ai_lines_eval.mjs
// (prompt v4, wild sampling); this judges them on UNHINGED and MAKES SENSE (and
// voice, specificity), ranks the models by unhinged among those whose
// makes-sense is at least the floor, and writes the short sample sheet of the
// leaders' wildest coherent lines.
//
//   node scripts/test/ai_lines/unhinged.mjs --run r2 --judges @cf/qwen/qwen3.8-27b,@cf/openai/gpt-oss-120b
//        [--floor 3.5] [--top 3] [--out docs/ai-lines/unhinged.html] [--no-judge]
//
// A judge never scores its own model's packs (self-preference): such a row is
// scored by the other judges only.
import { readFileSync, writeFileSync, existsSync, readdirSync, mkdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import catalog from "../../../web/src/catalog.json" with { type: "json" };
import { SITUATIONS } from "../../../web/src/validate.js";
import { restAI } from "./rest_runner.mjs";
import { judgeUnhinged } from "./judge.mjs";
import { mechanics } from "./metrics.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../../..");
const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith("--")) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith("--") ? all[i + 1] : true]);
  return acc;
}, []));
const run = args.run || "r2";
const prompt = args.prompt || "v4";
const judges = String(args.judges || "@cf/qwen/qwen3.8-27b,@cf/openai/gpt-oss-120b").split(",");
const floor = Number(args.floor || 3.5);
const top = Number(args.top || 3);
const dir = join(ROOT, "outputs/ai_lines_eval", run);
mkdirSync(join(dir, "judge"), { recursive: true });
const slug = (m) => m.split("/").pop();
const readJson = (p) => (existsSync(p) ? JSON.parse(readFileSync(p)) : null);
const mean = (xs) => { const v = xs.filter(Number.isFinite); return v.length ? v.reduce((a, b) => a + b, 0) / v.length : NaN; };

// Every pack of this run and prompt on disk: model slug -> pet id -> pack.
const packs = {};
for (const f of readdirSync(join(dir, "packs"))) {
  const m = /^(.+)__(\w+)__([\w-]+)\.json$/.exec(f);
  if (!m || m[2] !== prompt || f.endsWith(".error.json")) continue;
  (packs[m[1]] ??= {})[m[3]] = JSON.parse(readFileSync(join(dir, "packs", f)));
}
const errors = {};
for (const f of readdirSync(join(dir, "packs"))) {
  const m = /^(.+)__(\w+)__([\w-]+)\.error\.json$/.exec(f);
  if (m && m[2] === prompt) errors[m[1]] = (errors[m[1]] ?? 0) + 1;
}

async function pool(items, n, fn) {
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(n, items.length) }, async () => {
    while (next < items.length) { const i = next++; await fn(items[i]); }
  }));
}

if (!args["no-judge"]) {
  const ai = restAI();
  const jobs = [];
  for (const judge of judges) for (const [model, byPet] of Object.entries(packs)) {
    if (slug(judge) === model) continue;   // no judge scores its own model
    for (const [id, pack] of Object.entries(byPet)) jobs.push({ judge, model, id, pack });
  }
  let done = 0;
  await pool(jobs, Number(args.concurrency || 12), async ({ judge, model, id, pack }) => {
    const path = join(dir, "judge", `u__${slug(judge)}__${model}__${prompt}__${id}.json`);
    if (existsSync(path)) return;
    try {
      const s = await judgeUnhinged(ai, judge, { species: pack.pet.species }, pack);
      if (s) writeFileSync(path, JSON.stringify(s, null, 1));
    } catch (e) { console.log(`judge ${slug(judge)} ${model} ${id}: ${e.message}`); }
    if (++done % 20 === 0) console.log(`judged ${done}/${jobs.length}`);
  });
}

// ---- rank ----
const rows = [];
for (const [model, byPet] of Object.entries(packs)) {
  const ids = Object.keys(byPet);
  const scores = ids.flatMap((id) => judges.filter((j) => slug(j) !== model)
    .map((j) => readJson(join(dir, "judge", `u__${slug(j)}__${model}__${prompt}__${id}.json`))).filter(Boolean));
  const mech = ids.map((id) => mechanics(byPet[id], SITUATIONS.length));
  rows.push({
    model, pets: ids.length, failed: errors[model] ?? 0, judged: scores.length,
    unhinged: mean(scores.map((s) => s.unhinged)), sense: mean(scores.map((s) => s.sense)),
    voice: mean(scores.map((s) => s.voice)), specificity: mean(scores.map((s) => s.specificity)),
    coverage: mean(mech.map((m) => m.coverage)), passFirst: mean(mech.map((m) => m.passFirst)),
    retries: mean(mech.map((m) => m.retries)), meanLen: mean(mech.map((m) => m.meanLen)),
    usd: mean(mech.map((m) => m.usd)), seconds: mean(mech.map((m) => m.ms / 1000)),
  });
}
// Ranked: those at or above the makes-sense floor (and with full coverage on what they wrote) by unhinged, then the rest.
const eligible = (r) => r.sense >= floor && r.coverage >= 0.98 && r.failed === 0;
rows.sort((a, b) => (eligible(b) - eligible(a)) || (b.unhinged - a.unhinged) || (b.sense - a.sense));
const f2 = (x, d = 2) => (Number.isFinite(x) ? x.toFixed(d) : "-");
console.log(`\nrun ${run}, prompt ${prompt}, floor makes-sense >= ${floor}; judges ${judges.map(slug).join(", ")} (none scores its own model)`);
console.log("rank | model | unhinged | makes sense | voice | genes | coverage | first-pass | chars/line | $/pet | s/pet | pets (failed) | judged");
rows.forEach((r, i) => console.log([eligible(r) ? i + 1 : "-", r.model, f2(r.unhinged), f2(r.sense), f2(r.voice), f2(r.specificity),
  `${Math.round(r.coverage * 100)}%`, `${Math.round(r.passFirst * 100)}%`, f2(r.meanLen, 1), r.usd.toFixed(4), f2(r.seconds, 0),
  `${r.pets} (${r.failed})`, r.judged].join(" | ")));
writeFileSync(join(dir, `unhinged_${prompt}.json`), JSON.stringify({ run, prompt, judges, floor, rows }, null, 1));

// ---- the sheet: the leaders' wildest coherent lines ----
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
// --leaders a,b,c: the decision's order (after the forced-ranking tie-break, unhinged_rank.mjs).
const leaders = args.leaders
  ? String(args.leaders).split(",").map((m) => rows.find((r) => r.model === m)).filter(Boolean)
  : rows.filter(eligible).slice(0, top);
const ranked = readJson(join(dir, `rank_${prompt}.json`));
const rankTable = ranked ? `<div class="scroll"><table><thead><tr><th>model</th><th>Borda points</th><th>per ranking (max ${ranked.table.length - 1})</th><th>ranked first</th></tr></thead><tbody>${
  ranked.table.map((r) => `<tr class="${leaders[0]?.model === r.model ? "lead" : ""}"><td>${esc(r.model)}</td><td>${r.points}</td><td>${r.perRanking.toFixed(2)}</td><td>${r.firsts} of ${ranked.rankings}</td></tr>`).join("")
}</tbody></table></div><p><small>${ranked.rankings} rankings: ${ranked.pets.length} pets, judges ${ranked.judges.map((j) => esc(slug(j))).join(", ")} (none of them ranked), two shuffles each. Each judge saw the six leaders' lines for one pet side by side, anonymised, and ranked them by most unhinged while still making sense.</small></p>` : "";
const sheetPets = String(args.pets || "grungo-b,gilly-b,mudge-b,octopus-b,hushwing-b").split(",");
const SIT = Object.fromEntries(catalog.situations.map((s) => [s.name, s]));
const picks = (model, id) => {
  const out = new Map();
  for (const j of judges) {
    const s = readJson(join(dir, "judge", `u__${slug(j)}__${model}__${prompt}__${id}.json`));
    for (const w of s?.wildest ?? []) out.set(w.toUpperCase().trim(), true);
  }
  // Only lines really in the pack, with the situation they belong to.
  const pack = packs[model]?.[id];
  const where = {};
  for (const [sit, ls] of Object.entries(pack?.lines ?? {})) for (const l of ls) where[l] = sit;
  return [...out.keys()].filter((l) => where[l]).slice(0, 5).map((l) => ({ line: l, sit: where[l] }));
};
const table = `<div class="scroll"><table><thead><tr><th>rank</th><th>model</th><th>unhinged</th><th>makes sense</th><th>voice</th><th>genes</th><th>coverage</th><th>first-pass valid</th><th>chars / line</th><th>$ / pet</th><th>s / pet</th></tr></thead><tbody>${
  rows.map((r, i) => `<tr class="${eligible(r) ? (i < top ? "lead" : "") : "out"}"><td>${eligible(r) ? i + 1 : "–"}</td><td>${esc(r.model)}</td><td>${f2(r.unhinged)}</td><td>${f2(r.sense)}</td><td>${f2(r.voice)}</td><td>${f2(r.specificity)}</td><td>${Math.round(r.coverage * 100)}%${r.failed ? ` <small>(${r.failed} pets failed)</small>` : ""}</td><td>${Math.round(r.passFirst * 100)}%</td><td>${f2(r.meanLen, 1)}</td><td>${r.usd.toFixed(3)}</td><td>${f2(r.seconds, 0)}</td></tr>`).join("")
}</tbody></table></div>`;
const cards = sheetPets.map((id) => {
  const species = Object.values(packs)[0]?.[id]?.pet?.species ?? id;
  return `<article class="pet"><h3>${esc(id)} <small>${esc(catalog.species[species]?.sketch ?? "")}</small></h3><div class="cols">${leaders.map((r) => {
    const pack = packs[r.model]?.[id];
    if (!pack) return "";
    return `<div class="col"><h4>${esc(r.model)}</h4><ul class="wild">${picks(r.model, id).map((p) => `<li><span class="say">${esc(p.line)}</span> <small title="${esc(SIT[p.sit]?.meaning)}">${esc(p.sit)}</small></li>`).join("")}</ul>
      <details><summary>His bible</summary><p>${esc(pack.bible)}</p></details></div>`;
  }).join("")}</div></article>`;
}).join("");
const body = `<title>Unhinged Pet Lines</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Atkinson+Hyperlegible:wght@400;700&family=Silkscreen&family=Fraunces:opsz,wght@9..144,600&display=swap">
<style>
/* Layout: one column; the ranked table, then each pet's wildest coherent lines from the leading models side by side. */
:root { --bg: #f2f4ef; --panel: #fff; --fg: #1c2420; --muted: #5d6a63; --rule: #d5dcd4; --accent: #b4235a; --lead: #fbe6ee; --screen: #0f1a17; --glow: #6ee7d3;
  --display: "Fraunces", Georgia, serif; --body: "Atkinson Hyperlegible", system-ui, sans-serif; --pixel: "Silkscreen", ui-monospace, monospace; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg: #111714; --panel: #18201c; --fg: #e3ebe6; --muted: #9aa8a0; --rule: #2b3631; --accent: #f472a8; --lead: #3a1c29; --screen: #050a08; --glow: #7ef2de; color-scheme: dark; } }
:root[data-theme="dark"] { --bg: #111714; --panel: #18201c; --fg: #e3ebe6; --muted: #9aa8a0; --rule: #2b3631; --accent: #f472a8; --lead: #3a1c29; --screen: #050a08; --glow: #7ef2de; color-scheme: dark; }
body { background: var(--bg); color: var(--fg); font: 15px/1.5 var(--body); margin: 0; }
main { max-width: 1180px; margin: 0 auto; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 28px 64px; display: grid; gap: 24px; }
main > * { min-width: 0; }
h1, h2, h3 { font-family: var(--display); text-wrap: balance; margin: 0; } h1 { font-size: 2rem; } h2 { font-size: 1.35rem; } h3 { font-size: 1.15rem; }
h3 small, small { color: var(--muted); font-family: var(--body); font-size: .8rem; font-weight: 400; }
h4 { margin: 0 0 6px; font-size: .9rem; color: var(--accent); }
p { margin: 0; max-width: 72ch; }
.scroll { max-width: 100%; overflow-x: auto; border: 1px solid var(--rule); border-radius: 8px; background: var(--panel); }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: .86rem; }
th, td { padding: 7px 10px; border-bottom: 1px solid var(--rule); text-align: left; white-space: nowrap; }
th { color: var(--muted); font-size: .76rem; }
tr.lead td { background: var(--lead); font-weight: 700; } tr.out td { color: var(--muted); }
.pet { background: var(--panel); border: 1px solid var(--rule); border-radius: 10px; padding: 16px; display: grid; gap: 12px; }
.cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 300px), 1fr)); gap: 16px; }
.col { min-width: 0; display: grid; gap: 8px; align-content: start; }
ul.wild { list-style: none; margin: 0; padding: 0; display: grid; gap: 6px; }
.say { display: inline-block; max-width: 100%; overflow-wrap: anywhere; font-family: var(--pixel); font-size: .78rem; background: var(--screen); color: var(--glow); padding: 3px 9px; border-radius: 999px; }
details summary { cursor: pointer; color: var(--accent); font-size: .85rem; } details p { font-size: .88rem; margin-top: 6px; }
summary:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
</style>
<main>
<header style="display:grid;gap:10px">
  <h1>Unhinged pet lines</h1>
  <p>The owner asked for the model on Workers AI that writes the most unhinged lines that still make sense. Every chat model in the catalog wrote packs for the same ${Object.values(packs)[0] ? Object.keys(Object.values(packs)[0]).length : 0} pets with prompt ${esc(prompt)} (unhinged but coherent, wild sampling). Judges ${judges.map((j) => esc(slug(j))).join(" and ")} scored unhinged and makes-sense from 1 to 5; no judge scored its own model. Models are ranked by unhinged among those with makes-sense of at least ${floor} and full coverage.</p>
  ${leaders[0] ? `<p>Winner: <b>${esc(leaders[0].model)}</b>, unhinged ${f2(leaders[0].unhinged)}, makes sense ${f2(leaders[0].sense)}, $${leaders[0].usd.toFixed(3)} and ${f2(leaders[0].seconds, 0)} s per pet.</p>` : ""}
  ${args.note ? `<p>${esc(args.note)}</p>` : ""}
</header>
<section style="display:grid;gap:10px"><h2>Scores</h2><p><small>Each judge scored each pack 1 to 5. Ranked by unhinged among models with makes-sense of at least ${floor} and full coverage; the greyed rows missed one of those (most often coverage: the always-thinking models' calls ran past five minutes and failed).</small></p>${table}</section>
${rankTable ? `<section style="display:grid;gap:10px"><h2>Tie-break: the leaders ranked head to head</h2>${rankTable}</section>` : ""}
<section style="display:grid;gap:14px"><h2>Wildest lines that still make sense</h2><p><small>The judges' picks from each leading model's pack, with the situation the line is said in (hover for when).</small></p>${cards}</section>
</main>`;
const out = join(ROOT, args.out || "docs/ai-lines/unhinged.html");
writeFileSync(out, `<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n${body}\n</html>\n`);
writeFileSync(out.replace(/\.html$/, ".artifact.html"), body + "\n");
console.log(`wrote ${out}`);
