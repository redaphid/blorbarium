#!/usr/bin/env node
// The owner's sample sheet for an eval run: the results table, three pets
// side by side under two models, and every pet under the recommended model
// laid out genes -> traits -> bible -> lines, so a person can judge whether the
// lines fit the genes.
//
//   node scripts/test/ai_lines/sheet.mjs --run r1 --pick kimi-k2.6:v2 --vs gemma-4-26b-a4b-it:v2
//        [--pets mudge-b,gilly-b,hushwing-a] [--out docs/ai-lines/samples.html]
//
// Writes the page twice: --out as a standalone HTML file, and beside it
// <name>.artifact.html without the document skeleton (for publishing).
import { readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import catalog from "../../../web/src/catalog.json" with { type: "json" };
import { JUDGED } from "./judge.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../../..");
const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith("--")) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith("--") ? all[i + 1] : true]);
  return acc;
}, []));
const run = args.run || "r1";
const dir = join(ROOT, "outputs/ai_lines_eval", run);
const summary = JSON.parse(readFileSync(join(dir, "summary.json")));
const [pickModel, pickPrompt] = String(args.pick).split(":");
const [vsModel, vsPrompt] = String(args.vs).split(":");
const sidePets = String(args.pets || "mudge-b,gilly-b,hushwing-a").split(",");
const out = join(ROOT, args.out || "docs/ai-lines/samples.html");
const judges = summary.judges.map((j) => j.split("/").pop());

const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const slug = (m) => m.split("/").pop();
const packOf = (model, prompt, id) => {
  const p = join(dir, "packs", `${model}__${prompt}__${id}.json`);
  return existsSync(p) ? JSON.parse(readFileSync(p)) : null;
};
const judgeOf = (judge, model, prompt, id) => {
  const p = join(dir, "judge", `${judge}__${model}__${prompt}__${id}.json`);
  return existsSync(p) ? JSON.parse(readFileSync(p)) : null;
};
const f2 = (x, d = 2) => (Number.isFinite(x) ? x.toFixed(d) : "–");
const pct = (x) => (Number.isFinite(x) ? `${Math.round(x * 100)}%` : "–");
const SIT = Object.fromEntries(catalog.situations.map((s) => [s.name, s]));

function groupOf(s) {
  if (s.kind === "verdict") return "Shake answers";
  if (s.kind === "prophecy") return "Prophecies (shaken)";
  if (s.when === "dozing") return "Asleep";
  if (["always", "daytime", "night"].includes(s.when)) return "Idle";
  if (/^wall_|^named|phone/.test(s.when)) return "With a phone";
  return "Needs and moments";
}

function linesTable(pack, names) {
  return `<dl class="lines">${names.filter((n) => pack.lines[n]).map((n) =>
    `<div><dt title="${esc(SIT[n]?.meaning)}">${esc(n)}</dt><dd>${pack.lines[n].map((l) => `<span class="say">${esc(l)}</span>`).join("")}</dd></div>`).join("")}</dl>`;
}

function geneFacts(raw) {
  const t = raw.temperament;
  const facts = [
    ["species", raw.species], ["stage", raw.stage], ["power", raw.power], ["loves", raw.lovedStance],
    ["startle gain", raw.startleGain], ["hunger fill", raw.hungerFill.join(", ") || "none"],
    ["melatonin fill", raw.melatoninFill.join(", ") || "none"], ["learn / explore", `${t.learnRate} / ${t.explore}`],
    ["habituation", t.habituation], ["dreams", `${t.dreamEvery / 10}s / ${t.dreamLen / 10}s`],
    ["heirlooms", raw.heirlooms.length], ["sleeping genes", raw.dormant], ["genes", raw.genes],
  ];
  return `<dl class="facts">${facts.map(([k, v]) => `<div><dt>${esc(k)}</dt><dd>${esc(v)}</dd></div>`).join("")}</dl>`;
}

function scores(model, prompt, id) {
  return judges.map((j) => {
    const s = judgeOf(j, model, prompt, id);
    return s ? `<span class="score" title="${esc(s.notes)}">${esc(j.replace(/-\d+$/, ""))}: voice ${s.voice} · genes ${s.specificity} · humour ${s.humour}</span>` : "";
  }).join("");
}

// ---- the results table ----
const rows = summary.summary.slice().sort((a, b) => {
  const score = (s) => Object.values(s.judged).reduce((acc, j) => acc + (j.voice + j.specificity + j.humour + (j.distinctness || 0)), 0);
  return score(b) - score(a);
});
const table = `<div class="scroll"><table class="results">
<thead><tr><th>model</th><th>prompt</th><th>coverage</th><th>first-pass valid</th><th>retries / pet</th><th>chars / line</th>
<th>distinct-2</th><th>same-species overlap</th><th>same-species similarity</th>
${judges.map((j) => `<th>${esc(j)}<br>voice · genes · humour · distinct</th>`).join("")}<th>s / pet</th><th>$ / pet</th></tr></thead>
<tbody>${rows.map((s) => `<tr class="${slug(s.model) === pickModel && s.prompt === pickPrompt ? "pick" : ""}">
<td>${esc(slug(s.model))}</td><td>${esc(s.prompt)}</td><td>${pct(s.coverage)}${s.fallbackTotal ? ` <small>(${s.fallbackTotal} fell back)</small>` : ""}</td>
<td>${pct(s.passFirst)}</td><td>${f2(s.retries, 1)}</td><td>${f2(s.meanLen, 1)}</td><td>${f2(s.distinct2)}</td>
<td>${f2(s.sameSpeciesNgramOverlap)}</td><td>${f2(s.sameSpeciesLineSim)}</td>
${Object.values(s.judged).map((j) => `<td>${f2(j.voice, 1)} · ${f2(j.specificity, 1)} · ${f2(j.humour, 1)} · ${f2(j.distinctness, 1)}</td>`).join("")}
<td>${f2(s.seconds, 0)}</td><td>${f2(s.usdPerPet, 3)}</td></tr>`).join("")}</tbody></table></div>`;

// ---- side by side ----
const sideNames = JUDGED.slice(0, 14);
const side = sidePets.map((id) => {
  const a = packOf(pickModel, pickPrompt, id), b = packOf(vsModel, vsPrompt, id);
  if (!a || !b) return "";
  const col = (pack, model, prompt) => `<div class="col"><h4>${esc(model)} <small>${esc(prompt)}</small></h4>
    <p class="bible">${esc(pack.bible)}</p>${linesTable(pack, sideNames)}<p class="scores">${scores(model, prompt, id)}</p></div>`;
  return `<article class="pair"><header><h3>${esc(id)}</h3><p class="traits-inline">${esc(a.traits.notes.map((n) => n.s).slice(0, 8).join(" · "))}</p></header>
  <div class="cols">${col(a, pickModel, pickPrompt)}${col(b, vsModel, vsPrompt)}</div></article>`;
}).join("");

// ---- every pet, genes -> lines ----
const fixtures = JSON.parse(readFileSync(join(ROOT, "web/test/fixtures/genomes.json")));
const every = fixtures.map((f) => {
  const p = packOf(pickModel, pickPrompt, f.id);
  if (!p) return `<article class="pet"><h3>${esc(f.id)}</h3><p>No pack (the run failed for this pet).</p></article>`;
  const groups = {};
  for (const s of catalog.situations) (groups[groupOf(s)] ??= []).push(s.name);
  const dice = p.provenance.dice ? Object.entries(p.provenance.dice).map(([k, v]) => `${k.replace(/_/g, " ")}: ${v}`).join(" · ") : "";
  return `<article class="pet" id="${esc(f.id)}">
  <header><h3>${esc(f.id)}</h3><p class="sub">${esc(catalog.tables.species[String(catalog.species[f.raw.species].id)].sketch)} · generation ${f.generation}</p></header>
  <div class="flow">
    <section><h4>1 · Genes</h4>${geneFacts(f.raw)}</section>
    <section><h4>2 · Traits the server read from them</h4><ul class="traits">${p.traits.notes.map((n) => `<li>${esc(n.s)}</li>`).join("")}</ul>
      ${dice ? `<p class="dice"><b>Genome dice</b> ${esc(dice)}</p>` : ""}</section>
    <section><h4>3 · Character bible</h4><p class="bible">${esc(p.bible)}</p>
      <p class="catch">${(p.catchphrases ?? []).map((c) => `<span class="say">${esc(c)}</span>`).join("")}</p></section>
  </div>
  <section><h4>4 · Lines (a sample; every situation below)</h4>${linesTable(p, JUDGED)}
  <p class="scores">${scores(pickModel, pickPrompt, f.id)}</p>
  <details><summary>All ${Object.keys(p.lines).length} situations${p.fallback.length ? `, ${p.fallback.length} left to the built-in lines` : ""}</summary>
  ${Object.entries(groups).map(([g, names]) => `<h5>${esc(g)}</h5>${linesTable(p, names)}`).join("")}</details></section>
</article>`;
}).join("");

const pick = summary.summary.find((s) => slug(s.model) === pickModel && s.prompt === pickPrompt);
const body = `<title>Pet Lines Eval</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Atkinson+Hyperlegible:wght@400;700&family=Silkscreen&family=Fraunces:opsz,wght@9..144,600&display=swap">
<style>
/* Layout: one reading column; tables and pairs widen inside their own scrollers. Lines are set like the badge's screen. */
:root { --bg: #f2f4ef; --panel: #ffffff; --fg: #1c2420; --muted: #5d6a63; --rule: #d5dcd4; --accent: #0d7a6c; --screen: #0f1a17; --glow: #6ee7d3; --pick: #e2f2ee;
  --display: "Fraunces", Georgia, serif; --body: "Atkinson Hyperlegible", system-ui, sans-serif; --pixel: "Silkscreen", ui-monospace, monospace; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg: #111714; --panel: #18201c; --fg: #e3ebe6; --muted: #9aa8a0; --rule: #2b3631; --accent: #4fd1bf; --screen: #050a08; --glow: #7ef2de; --pick: #173029; color-scheme: dark; } }
:root[data-theme="dark"] { --bg: #111714; --panel: #18201c; --fg: #e3ebe6; --muted: #9aa8a0; --rule: #2b3631; --accent: #4fd1bf; --screen: #050a08; --glow: #7ef2de; --pick: #173029; color-scheme: dark; }
body { background: var(--bg); color: var(--fg); font: 15px/1.5 var(--body); }
main { max-width: 1180px; margin: 0 auto; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 28px 64px; display: grid; gap: 28px; }
h1, h2, h3 { font-family: var(--display); text-wrap: balance; margin: 0; }
h1 { font-size: 2.1rem; } h2 { font-size: 1.45rem; } h3 { font-size: 1.2rem; }
h4 { margin: 0 0 6px; font-size: .78rem; letter-spacing: .08em; text-transform: uppercase; color: var(--muted); }
h5 { margin: 14px 0 4px; font-size: .85rem; color: var(--accent); }
p { margin: 0; max-width: 72ch; } small { color: var(--muted); }
.lede { display: grid; gap: 10px; }
.scroll { overflow-x: auto; border: 1px solid var(--rule); border-radius: 8px; background: var(--panel); }
table.results { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: .86rem; }
.results th, .results td { padding: 7px 10px; border-bottom: 1px solid var(--rule); text-align: left; white-space: nowrap; vertical-align: top; }
.results th { font-weight: 700; color: var(--muted); font-size: .76rem; }
.results tr.pick td { background: var(--pick); font-weight: 700; }
.say { display: inline-block; font-family: var(--pixel); font-size: .74rem; letter-spacing: .02em; background: var(--screen); color: var(--glow); padding: 3px 8px; border-radius: 999px; margin: 0 6px 5px 0; }
dl.lines { margin: 0; display: grid; gap: 4px; }
dl.lines > div { display: grid; grid-template-columns: 9.5rem minmax(0, 1fr); gap: 8px; align-items: baseline; }
dl.lines dt { font-size: .8rem; color: var(--muted); overflow-wrap: anywhere; }
dl.lines dd { margin: 0; min-width: 0; }
.pair, .pet { background: var(--panel); border: 1px solid var(--rule); border-radius: 10px; padding: 16px; display: grid; gap: 14px; min-width: 0; }
.cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 420px), 1fr)); gap: 18px; }
.col { display: grid; gap: 10px; align-content: start; min-width: 0; }
.col h4 { color: var(--accent); text-transform: none; letter-spacing: 0; font-size: .95rem; }
.bible { font-size: .93rem; }
.traits-inline, .sub, .dice { color: var(--muted); font-size: .85rem; }
.flow { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 300px), 1fr)); gap: 18px; }
.flow section { min-width: 0; }
dl.facts { margin: 0; display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 2px 12px; font-size: .84rem; font-variant-numeric: tabular-nums; }
dl.facts div { display: flex; justify-content: space-between; gap: 8px; border-bottom: 1px dotted var(--rule); }
dl.facts dt { color: var(--muted); } dl.facts dd { margin: 0; text-align: right; }
ul.traits { margin: 0; padding-left: 18px; font-size: .86rem; display: grid; gap: 2px; }
.scores { display: flex; flex-wrap: wrap; gap: 6px 14px; font-size: .8rem; color: var(--muted); }
.score { border: 1px solid var(--rule); border-radius: 999px; padding: 1px 9px; }
details summary { cursor: pointer; color: var(--accent); font-weight: 700; margin-top: 6px; }
details summary:focus-visible, a:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
a { color: var(--accent); }
nav.jump { display: flex; flex-wrap: wrap; gap: 6px 12px; font-size: .85rem; }
</style>
<main>
<header class="lede">
  <h1>Pet lines: model and prompt eval</h1>
  <p>Each blorbarium pet's genes go to Workers AI twice: once to write a short character bible, once to write two to four lines for each of the ${catalog.situations.length} situations a pet speaks in. Every line is checked mechanically (at most ${catalog.maxText} characters, only the badge font's characters, no repeats, every situation covered); failures are asked for again, twice at most, and anything still failing falls back to the built-in lines.</p>
  <p>Run <b>${esc(run)}</b>: ${summary.models.length} models × ${summary.prompts.length} prompt versions × ${summary.pets.length} pets (two per species). Judges: ${judges.map(esc).join(" and ")}, scoring 1 to 5. Recommended: <b>${esc(pickModel)}</b> with prompt <b>${esc(pickPrompt)}</b>${pick ? ` (${pct(pick.coverage)} coverage, $${f2(pick.usdPerPet, 3)} and ${f2(pick.seconds, 0)} s per pet)` : ""}.</p>
</header>
<section><h2>Results</h2><p><small>Coverage: situations with at least one usable line. First-pass valid: lines that passed the checks without a retry. Distinct-2: unique word pairs over all word pairs in one pet's lines (higher is less repetitive). Same-species overlap: shared word pairs between the two pets of one species (lower is more distinct); similarity is the embedding cosine (bge-m3) of their lines. Hover a judge score for the judge's note.</small></p>${table}</section>
<section><h2>Side by side</h2><p><small>${esc(pickModel)} ${esc(pickPrompt)} against ${esc(vsModel)} ${esc(vsPrompt)}, on the same three pets and the same situations.</small></p><div style="display:grid;gap:18px">${side}</div></section>
<section><h2>Every pet, genes to lines (${esc(pickModel)} ${esc(pickPrompt)})</h2>
<p><small>For a spot check: each pet's decoded genes, the traits the server read from them, the bible the first call wrote, and the lines the second wrote. Does a timid pet sound timid, a hybrid mention its borrowed power, a pet who loves being shaken ask for it?</small></p>
<nav class="jump">${fixtures.map((f) => `<a href="#${esc(f.id)}">${esc(f.id)}</a>`).join("")}</nav>
<div style="display:grid;gap:18px;margin-top:12px">${every}</div></section>
</main>`;

mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, `<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n${body.replace("</style>", "body{margin:0}</style>")}\n</html>\n`);
writeFileSync(out.replace(/\.html$/, ".artifact.html"), body + "\n");
console.log(`wrote ${out}`);
