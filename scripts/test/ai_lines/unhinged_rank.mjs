#!/usr/bin/env node
// Run r2's tie-break: absolute 1-5 scores bunch the leaders within a quarter
// point, so each judge sees the leaders' lines for one pet side by side,
// anonymised and shuffled, and ranks them by "most unhinged while still making
// sense". Two shuffles per pet and judge cancel position bias; Borda points
// (n-1 for first ... 0 for last) are summed over every ranking.
//
//   node scripts/test/ai_lines/unhinged_rank.mjs --run r2 --models a,b,c,d,e [--judges ...]
import { readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { restAI } from "./rest_runner.mjs";
import { call } from "../../../web/src/pipeline.js";
import { parseJsonReply } from "../../../web/src/validate.js";
import { JUDGED } from "./judge.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../../..");
const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith("--")) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith("--") ? all[i + 1] : true]);
  return acc;
}, []));
const run = args.run || "r2";
const prompt = args.prompt || "v4";
const models = String(args.models).split(",");
const judges = String(args.judges || "@cf/qwen/qwen3.8-27b,@cf/openai/gpt-oss-120b").split(",");
const pets = String(args.pets || "grungo-b,mossby-b,puffkin-b,gilly-b,mothwick-b,mudge-b,fizz-b,hushwing-b,tinker-b,octopus-b").split(",");
const dir = join(ROOT, "outputs/ai_lines_eval", run);
mkdirSync(join(dir, "rank"), { recursive: true });
const ai = restAI();
const LABELS = "ABCDEFGH";

// A fixed shuffle per (pet, judge, round), so a re-run asks the same question.
function shuffled(xs, seed) {
  const a = [...xs];
  let s = seed;
  for (let i = a.length - 1; i > 0; i--) { s = (s * 1103515245 + 12345) >>> 0; const j = s % (i + 1); [a[i], a[j]] = [a[j], a[i]]; }
  return a;
}
const hash = (str) => [...str].reduce((h, c) => (h * 31 + c.charCodeAt(0)) >>> 0, 7);

const points = Object.fromEntries(models.map((m) => [m, 0]));
const firsts = Object.fromEntries(models.map((m) => [m, 0]));
let rankings = 0;
const jobs = [];
for (const id of pets) for (const judge of judges) for (const round of [0, 1]) jobs.push({ id, judge, round });
await Promise.all(jobs.map(async ({ id, judge, round }) => {
  const path = join(dir, "rank", `${judge.split("/").pop()}__${id}__${round}.json`);
  let result = existsSync(path) ? JSON.parse(readFileSync(path)) : null;
  if (!result) {
    const packs = models.map((m) => [m, JSON.parse(readFileSync(join(dir, "packs", `${m}__${prompt}__${id}.json`)))]);
    const order = shuffled(packs, hash(`${id}${judge}${round}`));
    const blocks = order.map(([, p], i) => `WRITER ${LABELS[i]}:\n` +
      JUDGED.filter((s) => p.lines[s]).map((s) => `${s}: ${p.lines[s].join(" | ")}`).join("\n")).join("\n\n");
    const messages = [
      { role: "system", content: "You are a strict judge of comic writing for a virtual-pet game. Reply with JSON only." },
      { role: "user", content: `${order.length} writers wrote lines for the same pet (a ${order[0][1].pet.species}) in the same situations. The goal: the most UNHINGED lines (wild, absurd, chaotic, surprising) that still MAKE SENSE (a kid gets the joke or the picture at once, and the line fits its situation). Unhinged but nonsensical loses; sensible but tame loses.\n\n${blocks}\n\nRank all ${order.length} writers from best to worst at that goal.\nReply as JSON: {"ranking": ["X", "Y", ...], "why": "one sentence"}` },
    ];
    for (let attempt = 0; attempt < 3 && !result; attempt++) {
      try {
        const j = parseJsonReply(await call(ai, judge, messages, { maxTokens: 3000, temperature: 0.2 }));
        const r = (j?.ranking ?? []).map((x) => String(x).trim().toUpperCase().replace(/^WRITER\s*/, ""));
        if (r.length === order.length && new Set(r).size === r.length && r.every((x) => LABELS.slice(0, order.length).includes(x))) {
          result = { ranking: r.map((x) => order[LABELS.indexOf(x)][0]), why: j.why ?? "" };
        }
      } catch (e) { console.log(`rank ${judge} ${id}: ${e.message}`); }
    }
    if (result) writeFileSync(path, JSON.stringify(result, null, 1));
  }
  if (!result) return;
  rankings++;
  result.ranking.forEach((m, i) => { points[m] += result.ranking.length - 1 - i; });
  firsts[result.ranking[0]]++;
}));
const table = models.map((m) => ({ model: m, points: points[m], perRanking: points[m] / rankings, firsts: firsts[m] }))
  .sort((a, b) => b.points - a.points);
console.log(`\n${rankings} rankings (${pets.length} pets x ${judges.length} judges x 2 shuffles); Borda points, max ${(models.length - 1)} per ranking`);
for (const r of table) console.log(`${r.model} | ${r.points} pts | ${r.perRanking.toFixed(2)} per ranking | ranked first ${r.firsts}x`);
writeFileSync(join(dir, `rank_${prompt}.json`), JSON.stringify({ run, prompt, judges, pets, rankings, table }, null, 1));
