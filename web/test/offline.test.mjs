// node --test web/test/offline.test.mjs
// sw.js keeps every file the page can ask for, so nothing goes missing offline.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";

const PUBLIC = new URL("../public/", import.meta.url);
const sw = readFileSync(new URL("sw.js", PUBLIC), "utf8");
const SHELL = JSON.parse(/const SHELL = (\[[^\]]*\]);/.exec(sw)[1]);

// Every file under web/public, as a path relative to it.
function files(dir = PUBLIC, prefix = "") {
  return readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? files(new URL(`${e.name}/`, dir), `${prefix}${e.name}/`) : [`${prefix}${e.name}`]);
}

test("sw.js lists every file in web/public but itself, .assetsignore and fw/", () => {
  const want = files().filter((f) => f !== "sw.js" && f !== ".assetsignore" && !f.startsWith("fw/")).sort();
  const listed = SHELL.filter((f) => f !== "./").sort();
  assert.deepEqual(listed, want, "add the new file to SHELL in web/public/sw.js");
});

test("the web app manifest's icon and start are files the worker keeps", () => {
  const m = JSON.parse(readFileSync(new URL("manifest.webmanifest", PUBLIC), "utf8"));
  for (const icon of m.icons) assert.ok(SHELL.includes(icon.src), icon.src);
  assert.ok(SHELL.includes(m.start_url), m.start_url);
});
