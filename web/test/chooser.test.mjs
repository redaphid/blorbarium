// node --test web/test/chooser.test.mjs
// The chooser's filters in web/public/ota.js: any dish by either name, nothing else.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { chooserFilters, NUS, SPECIES } = require("../public/ota.js");
const { chooserMatches } = require("../public/ota-mock.js");

const dish = (name) => ({ name, services: [NUS] });

test("the page knows every species the firmware can name a dish after", () => {
  const def = readFileSync(new URL("../../lib/blorb/include/blorb/defs/species.def", import.meta.url), "utf8");
  const slugs = [...def.matchAll(/^BLORB_SPECIES\(\d+,\s*(\w+),/gm)].map((m) => m[1]);
  assert.ok(slugs.length >= 9, `read ${slugs.length} species`);
  assert.deepEqual(SPECIES, slugs);
});

test("with no ?d=, the chooser lists a dish by its old name or its species, and needs the UART service", () => {
  const filters = chooserFilters(null);
  for (const name of ["blorb-1b3c", "blorb-237c", ...SPECIES, "mossby-1b3c"]) {
    assert.ok(chooserMatches(filters, dish(name)), `${name} not listed`);
  }
  for (const name of ["uart-thing", "Mossby", "blorb", "Pixel Buds", ""]) {
    assert.ok(!chooserMatches(filters, dish(name)), `${name} listed`);
  }
  assert.ok(!chooserMatches(filters, { name: "fizzbox", services: [] }), "a device without the UART service listed");
  assert.ok(filters.every((f) => f.services && f.services[0] === NUS));
});

// After 0.7.42 a dish advertises its pet's name, which may be one the owner
// chose: the badge's id (company 0xFFFF, from 0.7.33) finds it whatever it is.
test("with no ?d=, the chooser lists a dish by its id whatever its pet is called", () => {
  const filters = chooserFilters(null);
  const id = { 0xffff: Uint8Array.from([0xd4, 0x05, 0x92, 0x77, 0x1b, 0x30]) };
  for (const name of ["zorp_v", "mudge", "grungo_the_16th", "blorb-1b30"]) {
    assert.ok(chooserMatches(filters, { name, services: [NUS], manufacturerData: id }), `${name} not listed`);
  }
  assert.ok(!chooserMatches(filters, { name: "zorp_v", services: [NUS] }), "a renamed UART device without the id listed");
  assert.ok(!chooserMatches(filters, { name: "zorp_v", services: [], manufacturerData: id }), "the id without the UART service listed");
  assert.ok(!chooserMatches(filters, { name: "zorp_v", services: [NUS], manufacturerData: { 0x004c: id[0xffff] } }), "another company listed");
});

test("?d= names one dish exactly, in either form", () => {
  for (const want of ["blorb-1b3c", "mossby"]) {
    const filters = chooserFilters(want);
    assert.ok(chooserMatches(filters, dish(want)));
    assert.ok(!chooserMatches(filters, dish(want === "mossby" ? "blorb-1b3c" : "mossby")));
  }
});

test("searching wider lists any UART device, still nothing without it", () => {
  const filters = chooserFilters(null, true);
  assert.ok(chooserMatches(filters, dish("uart-thing")));
  assert.ok(!chooserMatches(filters, { name: "blorb-1b3c", services: [] }));
});
