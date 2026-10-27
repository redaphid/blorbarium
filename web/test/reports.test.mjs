// node --test web/test/reports.test.mjs
// Remote debug (src/reports.js, migrations/0003_badge_reports.sql): a badge's
// report is kept (the last KEEP_REPORTS), the dashboard sees every badge's
// health, and the owner's commands are an allowlist, each handed over once.
import { test } from "node:test";
import assert from "node:assert/strict";
import worker from "../src/app.js";
import { KEEP_REPORTS, MARQUEE_MAX, checkCommand, marqueeText } from "../src/reports.js";
import { fakeEnv } from "./support/fake_env.mjs";

const MAC = "7cdfa1b2c3d4";
const call = async (env, path, body, { admin = false, raw = false } = {}) => {
  const r = await worker.fetch(new Request(`https://blorb.test${path}`, {
    method: body === undefined ? "GET" : "POST", body: body === undefined ? undefined : JSON.stringify(body),
    headers: { "content-type": "application/json", ...(admin ? { authorization: "Bearer sekrit" } : {}) } }), env, { waitUntil() {} });
  return { status: r.status, type: r.headers.get("content-type"), body: raw ? await r.text() : await r.json() };
};
const report = (extra = {}) => ({
  v: 1, mac: MAC, fw: "0.7.21", board: "lcd128", uptime_s: 3600, reset: "panic", heap: 90000, minheap: 2100, big: 30000,
  vbat_mv: 3900, rssi: -71, dark: 1,
  ota: { auto: 1, window: "check", next_s: 21600, every_min: 360, absent_streak: 0, server_streak: 0, on_trial: 0, rolled_back: null, last: "up to date, nothing to install" },
  pet: { kind: "creature", stage: "adult", age_min: 4000, hunger_pct: 35 },
  crash: { task: "loopTask", pc: "0x42001234", bt: ["0x42001234", "0x42005678"], corrupt: 0 },
  results: [], log: "[wifi] window check opens\n[lines] kept in flash\n", ...extra });

test("a report is kept and the dashboard shows the badge's health", async () => {
  const env = fakeEnv();
  const r = await call(env, "/api/badge/report", report(), { raw: true });
  assert.equal(r.status, 200);
  assert.match(r.type, /text\/plain/);
  assert.equal(r.body, "blorb-cmds 1\n");
  const h = (await call(env, "/api/admin/health", undefined, { admin: true })).body;
  assert.equal(h.badges.length, 1);
  const b = h.badges[0];
  assert.equal(b.mac, MAC);
  assert.equal(b.fw, "0.7.21");
  assert.equal(b.reset, "panic");
  assert.equal(b.crashes, 1);
  assert.equal(b.minheap, 2100);
  assert.equal(b.auto_on, 1);
  assert.equal(b.ota.every_min, 360);
  assert.equal(b.crash.task, "loopTask");
  const d = (await call(env, `/api/admin/badges/${MAC}/reports`, undefined, { admin: true })).body;
  assert.equal(d.reports.length, 1);
  assert.match(d.reports[0].log_text, /window check opens/);
  assert.equal((await call(env, "/api/admin/health")).status, 401, "the dashboard needs the token");
});

test("only the last reports per badge are kept", async () => {
  const env = fakeEnv();
  for (let i = 0; i < KEEP_REPORTS + 3; i++) await call(env, "/api/badge/report", report({ uptime_s: i }), { raw: true });
  const rows = env.DB.raw.prepare("SELECT uptime_s FROM badge_reports WHERE mac = ? ORDER BY id").all(MAC);
  assert.equal(rows.length, KEEP_REPORTS);
  assert.equal(rows[0].uptime_s, 3);
});

test("a queued command goes out once, in the reply, and its result closes it", async () => {
  const env = fakeEnv();
  await call(env, "/api/badge/report", report(), { raw: true });
  const q = await call(env, `/api/admin/badges/${MAC}/debug`, { verb: "interval", arg: 60 }, { admin: true });
  assert.equal(q.status, 200);
  const q2 = await call(env, `/api/admin/badges/${MAC}/debug`, { verb: "reboot" }, { admin: true });
  const first = await call(env, "/api/badge/report", report(), { raw: true });
  assert.equal(first.body, `blorb-cmds 1\n${q.body.id} interval 60\n${q2.body.id} reboot\n`);
  const again = await call(env, "/api/badge/report", report(), { raw: true });
  assert.equal(again.body, "blorb-cmds 1\n", "each command is handed over once");
  await call(env, "/api/badge/report", report({ reset: "sw", results: [
    { id: q.body.id, ok: true, text: "interval: every 60 min until a restart" },
    { id: q2.body.id, ok: true, text: "restarted (reset reason sw)" }] }), { raw: true });
  const d = (await call(env, `/api/admin/badges/${MAC}/reports`, undefined, { admin: true })).body;
  const byId = Object.fromEntries(d.commands.map((c) => [c.id, c]));
  assert.equal(byId[q.body.id].status, "done");
  assert.match(byId[q.body.id].result.text, /every 60 min/);
  assert.equal(byId[q2.body.id].status, "done");
  // Another badge's report cannot close this badge's commands.
  const q3 = await call(env, `/api/admin/badges/${MAC}/debug`, { verb: "state" }, { admin: true });
  await call(env, "/api/badge/report", report({ mac: "aabbccddeeff", results: [{ id: q3.body.id, ok: false, text: "x" }] }), { raw: true });
  assert.equal(env.DB.raw.prepare("SELECT status FROM actions WHERE id = ?").get(q3.body.id).status, "pending");
});

test("the allowlist: nothing that could stop updates, forget Wi-Fi or flash anything", async () => {
  const env = fakeEnv();
  await call(env, "/api/badge/report", report(), { raw: true });
  for (const [verb, arg] of [["wifi", "off"], ["forget"], ["ota"], ["erase"], ["auto", 0], ["interval", 29], ["interval", 721], ["interval", "60; reboot"], ["reboot", "now"]]) {
    const r = await call(env, `/api/admin/badges/${MAC}/debug`, { verb, arg }, { admin: true });
    assert.equal(r.status, 400, `${verb} ${arg ?? ""}`);
  }
  for (const [verb, arg] of [["check"], ["interval", 30], ["interval", 720], ["state"], ["shot"], ["reboot"], ["marquee", "hi"]]) assert.ok(checkCommand(verb, arg).payload, verb);
  // A debug row written some other way (the generic actions endpoint) is checked again before it is sent.
  const bad = await call(env, "/api/admin/actions", { macs: [MAC], kind: "debug", payload: { verb: "wifi forget" } }, { admin: true });
  assert.equal(bad.status, 400);
  env.DB.raw.prepare("INSERT INTO actions (mac, kind, payload_json, status, created_by, created_at) VALUES (?, 'debug', ?, 'pending', 'x', 1)")
    .run(MAC, JSON.stringify({ verb: "erase" }));
  const r = await call(env, "/api/badge/report", report(), { raw: true });
  assert.equal(r.body, "blorb-cmds 1\n");
  assert.equal(env.DB.raw.prepare("SELECT status FROM actions WHERE kind = 'debug'").get().status, "failed");
});

test("the phone's action pull never takes the badge's debug commands", async () => {
  const env = fakeEnv();
  await call(env, "/api/badge/report", report(), { raw: true });
  await call(env, `/api/admin/badges/${MAC}/debug`, { verb: "check" }, { admin: true });
  const pulled = (await call(env, `/api/badge/actions?mac=${MAC}&via=phone`)).body;
  assert.equal(pulled.actions.length, 0);
  assert.match((await call(env, "/api/badge/report", report(), { raw: true })).body, /^\d+ check$/m);
});

test("a screenshot goes to device debug and the report points at it", async () => {
  const env = fakeEnv();
  const shot = "[hw] shot begin w=240 h=240 rle565\n[hw] shot AAAA\n[hw] shot end bytes=3 crc=00000000\n";
  await call(env, "/api/badge/report", report({ shot }), { raw: true });
  const row = env.DB.raw.prepare("SELECT shot_id FROM badge_reports").get();
  assert.ok(row.shot_id);
  const body = (await call(env, `/api/admin/blob/device/${row.shot_id}`, undefined, { admin: true })).body;
  assert.equal(body.lines, shot);
});

test("bad reports are refused", async () => {
  const env = fakeEnv();
  assert.equal((await call(env, "/api/badge/report", { mac: "nope" })).status, 400);
  assert.equal((await call(env, "/api/badge/report", { ...report(), log: "x".repeat(400 * 1024) })).status, 400);
});

test("a marquee is clamped to what the badge draws, and goes out on one line", async () => {
  assert.equal(marqueeText("Hi kids!\nLove,\tDad \u{1F496}"), "Hi kids! Love, Dad");
  assert.equal(marqueeText("Café crème"), "Cafe creme");
  assert.equal(marqueeText("x".repeat(500)).length, MARQUEE_MAX);
  assert.ok(checkCommand("marquee", " \u{1F496} ").error);
  const env = fakeEnv();
  await call(env, "/api/badge/report", report(), { raw: true });
  const q = await call(env, `/api/admin/badges/${MAC}/debug`, { verb: "marquee", arg: "Goodnight!\n99 reboot" }, { admin: true });
  assert.equal(q.status, 200);
  const r = await call(env, "/api/badge/report", report(), { raw: true });
  assert.equal(r.body, `blorb-cmds 1\n${q.body.id} marquee Goodnight! 99 reboot\n`, "the text cannot start a line of its own");
});

test("a shot sent alone to /api/debug is linked from its command's result", async () => {
  const env = fakeEnv();
  await call(env, "/api/badge/report", report(), { raw: true });
  const q = await call(env, `/api/admin/badges/${MAC}/debug`, { verb: "shot" }, { admin: true });
  await call(env, "/api/badge/report", report(), { raw: true });   // handed over
  const lines = "[hw] shot begin w=120 h=120 rle565\n[hw] shot AAAA\n[hw] shot end bytes=3 crc=00000000\n";
  const up = await call(env, "/api/debug", { mac: MAC, fw: "0.7.25", via: "wifi", items: [{ kind: "shot", summary: "screenshot 120x120", data: { lines } }] });
  assert.equal(up.status, 200);
  const id = up.body.ids[0];
  await call(env, "/api/badge/report", report({ results: [{ id: q.body.id, ok: true, text: `shot: 120x120 uploaded as #${id}` }] }), { raw: true });
  const d = (await call(env, `/api/admin/badges/${MAC}/reports`, undefined, { admin: true })).body;
  const c = d.commands.find((x) => x.id === q.body.id);
  assert.equal(c.status, "done");
  assert.equal(c.result.shot_id, id);
  assert.equal((await call(env, `/api/admin/blob/device/${id}`, undefined, { admin: true })).body.lines, lines);
});
