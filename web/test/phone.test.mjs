// node --test web/test/phone.test.mjs
// The phone's view of its pet (#42) and the owner's edit of his personality (#43).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker from "../src/worker.js";
import { runDiaries } from "../src/diary.js";
import { fakeEnv } from "./support/fake_env.mjs";

const fixtures = JSON.parse(readFileSync(new URL("./fixtures/genomes.json", import.meta.url)));
const MAC = "7cdfa1b2c3d4";
const req = async (env, path, body) => {
  const r = await worker.fetch(new Request(`https://blorb.test${path}`, body === undefined ? {} :
    { method: "POST", body: JSON.stringify(body), headers: { "content-type": "application/json" } }), env, { waitUntil() {} });
  return { status: r.status, body: await r.json() };
};

test("the phone finds its pet by genome hash: bible, sample lines, journal, family", async () => {
  const env = fakeEnv();
  const parent = (await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[0].genome, lineage: "L", generation: 2 })).body;
  const child = (await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[1].genome, lineage: "L", generation: 7, name: "Gub" })).body;
  await req(env, "/api/stats", { mac: MAC, genome_hash: child.genome_hash, stats: { feeds: 2 } });
  await runDiaries(env, { day: "2099-05-05" });
  const p = await req(env, `/api/pet?genome_hash=${child.genome_hash}`);
  assert.equal(p.status, 200);
  assert.equal(p.body.name, "Gub");
  assert.match(p.body.bible, /grumpy test pet/);
  assert.ok(Object.keys(p.body.lines).length > 5);
  assert.equal(p.body.diary[0].day, "2099-05-05");
  assert.equal(p.body.family.parent.generation, 2);
  const up = await req(env, `/api/pet?genome_hash=${parent.genome_hash}`);
  assert.equal(up.body.family.children[0].name, "Gub");
  assert.equal((await req(env, `/api/pet?genome_hash=${"0".repeat(64)}`)).status, 404);
});

test("the owner's edited personality rewrites the lines, keeps history, and queues the pack for the badge", async () => {
  const env = fakeEnv();
  const first = (await req(env, "/api/pet-lines", { mac: MAC, genome: fixtures[4].genome })).body;
  const calls = env.AI.calls.length;
  const r = await req(env, "/api/personality", { genome: fixtures[4].genome, bible: "He thinks he is a famous opera singer and sings every line." });
  assert.equal(r.status, 200);
  assert.equal(r.body.version, 2);
  assert.equal(env.AI.calls.length - calls, 4, "lines only: the owner's words are the bible");
  const db = env.DB.raw;
  const ps = db.prepare("SELECT version, source, edited_by, bible FROM personalities ORDER BY version").all();
  assert.deepEqual(ps.map((p) => p.source), ["genes", "edit"]);
  assert.equal(ps[1].edited_by, "phone");
  const a = db.prepare("SELECT kind, payload_json FROM actions").get();
  assert.equal(a.kind, "line_pack");
  assert.equal(JSON.parse(a.payload_json).version, 2);
  assert.equal((await req(env, `/api/pet?genome_hash=${first.genome_hash}`)).body.personality_version, 2);
  assert.equal((await req(env, "/api/personality", { genome: fixtures[4].genome, bible: "short" })).status, 400);
  assert.equal((await req(env, "/api/personality", { genome: fixtures[5].genome, bible: "A pet the server never met." })).status, 404);
});

test("the page's personality box script parses, and the service worker leaves /api alone", () => {
  const html = readFileSync(new URL("../public/index.html", import.meta.url), "utf8");
  const box = html.slice(html.indexOf('<section id="selfBox"'), html.indexOf("</section>", html.indexOf('<section id="selfBox"')));
  const script = box.slice(box.indexOf("<script>") + 8, box.indexOf("</script>"));
  assert.doesNotThrow(() => new Function(script));
  assert.match(readFileSync(new URL("../public/sw.js", import.meta.url), "utf8"), /path\.startsWith\("api\/"\)/);
});
