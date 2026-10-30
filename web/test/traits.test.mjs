// node --test web/test/traits.test.mjs
// The Worker's genome decoder (web/src/genome.js) against the engine's own
// (tools/trait_sheet.cpp, whose output scripts/test/ai_lines/build_trait_sheet.sh
// writes to web/test/fixtures/genomes.json and web/src/baselines.json).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { hexToBytes, rawSheet, parseGenome } from "../src/genome.js";
import { traitSheet, REGION_WORDS, regionWords } from "../src/traits.js";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const baselines = JSON.parse(readFileSync(new URL("../src/baselines.json", import.meta.url)));
const catalog = JSON.parse(readFileSync(new URL("../src/catalog.json", import.meta.url)));
const SPECIES = Object.values(catalog.tables.species).map((s) => s.name);
const REGIONS = Object.values(catalog.tables.regions);

test("the decoder reads every fixture genome as the engine does", () => {
  assert.equal(fixtures.length, 20);
  for (const f of fixtures) {
    const raw = rawSheet(hexToBytes(f.genome), { stage: f.raw.stage, feats: f.feats });
    assert.deepEqual(raw, f.raw, f.id);
  }
});

test("every species' starter decodes to its baseline at every stage", () => {
  for (const [species, stages] of Object.entries(baselines)) {
    for (const [stage, want] of Object.entries(stages)) {
      // The baselines carry no genome; decode a fixture of that species at that
      // stage and check the facts a species fixes (the rest is per pet).
      const f = fixtures.find((x) => x.raw.species === species && !["gilly-b", "tinker-b"].includes(x.id));
      const raw = rawSheet(hexToBytes(f.genome), { stage });
      for (const k of ["species", "element", "style", "voice", "stage"]) assert.equal(raw[k], want[k], `${species} ${stage} ${k}`);
    }
  }
});

test("damaged genomes are refused, not guessed at", () => {
  const good = hexToBytes(fixtures[0].genome);
  assert.throws(() => parseGenome(good.subarray(0, good.length - 3)), /past the end/);
  const twice = new Uint8Array([...good.subarray(0, 8 + good[1]), ...good.subarray(0, 8 + good[1])]);
  assert.throws(() => parseGenome(twice), /twice/);
  assert.throws(() => hexToBytes("xyz"), /not hex/);
});

test("the trait sheet says how this pet differs from its species", () => {
  const byId = Object.fromEntries(fixtures.map((f) => [f.id, f]));
  const sheet = (id) => traitSheet(rawSheet(hexToBytes(byId[id].genome), { stage: byId[id].raw.stage }),
                                   { generation: byId[id].generation });
  const hybrid = sheet("gilly-b");
  assert.match(hybrid.text, /zap/i);
  assert.match(hybrid.text, /hybrid/i);
  const heir = sheet("grungo-b");
  assert.ok(heir.facts.heirlooms.length === 3);
  assert.match(heir.text, /ancestor/i);
  const a = sheet("mudge-a").text, b = sheet("mudge-b").text;
  assert.notEqual(a, b);
});

// The owner (2026-10-30): "the pets seem like they all think they have cloaks".
// Region ids are the firmware's colour slots, not body parts; the sheet the AI
// reads names what each slot is on that species' art.
test("every colour region has a plain name on every species in the catalog", () => {
  assert.ok(SPECIES.length >= 10 && REGIONS.includes("cloak"));
  for (const species of SPECIES) {
    for (const region of REGIONS) {
      const words = REGION_WORDS[species]?.[region];
      assert.equal(typeof words, "string", `${species} ${region} has no words`);
      assert.ok(words.trim().length > 0, `${species} ${region}`);
      assert.doesNotMatch(words, /cloak/i, `${species} ${region}`);
      assert.equal(regionWords(species, region), words);
    }
  }
  assert.doesNotMatch(regionWords("someday", "cloak"), /cloak/i);   // a species not mapped yet
});

test("no species' trait sheet says cloak, whatever its colour genes do", () => {
  const sheet = (raw) => traitSheet(raw, { generation: 2, name: "Blip" }).text;
  for (const f of fixtures) assert.doesNotMatch(sheet(rawSheet(hexToBytes(f.genome), { stage: f.raw.stage })), /cloak/i, f.id);
  for (const species of SPECIES) {
    const f = fixtures.find((x) => x.raw.species === species);
    for (const stage of Object.keys(baselines[species])) {
      const raw = rawSheet(hexToBytes(f.genome), { stage });
      const base = baselines[species][stage];
      for (const region of REGIONS) {
        // That region alone shifted round the wheel and brighter, plus a second gene for it.
        const shifted = base.palette.map((p) => (p.region === region ? { ...p, hue: p.hue + 40, val: p.val + 30 } : p));
        const extra = { ...(base.palette.find((p) => p.region === region) ?? base.palette[0]), region };
        const text = sheet({ ...raw, palette: [...shifted, extra], marks: base.marks, size: base.size });
        assert.doesNotMatch(text, /cloak/i, `${species} ${stage} ${region}`);
        assert.ok(text.includes(REGION_WORDS[species][region]), `${species} ${stage} ${region}: ${text}`);
      }
    }
  }
});
