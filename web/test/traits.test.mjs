// node --test web/test/traits.test.mjs
// The Worker's genome decoder (web/src/genome.js) against the engine's own
// (tools/trait_sheet.cpp, whose output scripts/test/ai_lines/build_trait_sheet.sh
// writes to web/test/fixtures/genomes.json and web/src/baselines.json).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { hexToBytes, rawSheet, parseGenome } from "../src/genome.js";
import { traitSheet } from "../src/traits.js";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const baselines = JSON.parse(readFileSync(new URL("../src/baselines.json", import.meta.url)));

test("the decoder reads every fixture genome as the engine does", () => {
  assert.equal(fixtures.length, 2 * Object.keys(baselines).length, "two fixture pets per species");
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
