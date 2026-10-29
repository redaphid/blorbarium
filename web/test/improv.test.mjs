// node --test web/test/improv.test.mjs
// The page's Improv Wi-Fi client (web/public/improv.js) against the packets
// the dish's lib/wifiota/src/improv.cpp sends, with no browser.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { Client, DROPPED, clockOffset, waitOnline, askDebug, wifiReason, packet, wifiSettings, parseResult, SERVICE, CURRENT_STATE, ERROR_STATE, RPC_COMMAND, RPC_RESULT, STATE, ERROR, COMMAND } =
  require("../public/improv.js");

const bytes = (s) => [...new TextEncoder().encode(s)];

test("the Wi-Fi settings command is the spec's bytes", () => {
  // command 1, length 7, "ab" (2), "pass" (4), then the low byte of the sum
  const p = wifiSettings("ab", "pass");
  const body = [1, 8, 2, ...bytes("ab"), 4, ...bytes("pass")];
  assert.deepEqual([...p], [...body, body.reduce((a, b) => a + b, 0) & 0xff]);
  assert.deepEqual([...wifiSettings("open", "")].slice(0, 8), [1, 6, 4, ...bytes("open"), 0]);
});

test("an SSID of 1..32 bytes and a password of up to 64, in UTF-8 bytes", () => {
  assert.throws(() => wifiSettings("", "x"), { field: "ssid" });
  assert.throws(() => wifiSettings("x".repeat(33), ""), { field: "ssid" });
  assert.throws(() => wifiSettings("é".repeat(17), ""), { field: "ssid" }, "34 bytes");
  assert.throws(() => wifiSettings("net", "x".repeat(65)), { field: "password" });
  assert.equal(wifiSettings("x".repeat(32), "y".repeat(64)).length, 3 + 1 + 32 + 1 + 64);
});

test("a result reads back its strings and refuses a bad checksum or length", () => {
  const url = "https://blorb.hypnodroid.com/?d=puffkin";
  const r = packet(COMMAND.wifiSettings, Uint8Array.of(url.length, ...bytes(url)));
  assert.deepEqual(parseResult(r), { command: 1, strings: [url] });
  assert.deepEqual(parseResult(packet(COMMAND.deviceInfo, new Uint8Array(0))), { command: 3, strings: [] });
  const bad = Uint8Array.from(r);
  bad[bad.length - 1] ^= 1;
  assert.equal(parseResult(bad), null);
  assert.equal(parseResult(r.subarray(0, r.length - 2)), null);
  assert.equal(parseResult(packet(1, Uint8Array.of(9, 1, 2))), null, "a string past the end");
});

// The dish's service as Web Bluetooth shows it, answering as improv.cpp does.
function fakeDish({ joins = true, asks = false, says = true, drops = false, cutsWrite = false } = {}) {
  const chars = {};
  const note = (uuid, value) => {
    const v = Uint8Array.from(value);
    chars[uuid].value = v;
    if (chars[uuid].notifying) for (const f of chars[uuid].listeners) f({ target: { value: new DataView(v.buffer) } });
  };
  for (const uuid of [CURRENT_STATE, ERROR_STATE, RPC_COMMAND, RPC_RESULT]) {
    chars[uuid] = {
      listeners: [], notifying: false, value: Uint8Array.of(uuid === CURRENT_STATE ? (asks ? STATE.authorizationRequired : STATE.authorized) : 0),
      addEventListener(_, f) { this.listeners.push(f); },
      async startNotifications() { this.notifying = true; },
      async readValue() { return new DataView(this.value.buffer); },
    };
  }
  const written = [];
  // Opening the service asks; a hand says yes a moment later (or never).
  if (asks && says) chars[CURRENT_STATE].startNotifications = async function () {
    this.notifying = true;
    setTimeout(() => note(CURRENT_STATE, [STATE.authorized]), 5);
  };
  chars[RPC_COMMAND].writeValueWithResponse = async (p) => {
    written.push([...p]);
    if (p[0] === COMMAND.wifiSettings && chars[CURRENT_STATE].value[0] === STATE.authorizationRequired)
      return note(ERROR_STATE, [ERROR.notAuthorized]);
    if (p[0] === COMMAND.wifiSettings && cutsWrite) throw new Error("GATT Server is disconnected.");
    if (p[0] === COMMAND.wifiSettings && drops) return note(CURRENT_STATE, [STATE.provisioning]);   // 0.7.35: then it drops the link
    if (p[0] === COMMAND.wifiSettings) {
      note(CURRENT_STATE, [STATE.provisioning]);
      setTimeout(() => {
        if (!joins) return note(ERROR_STATE, [ERROR.unableToConnect]), note(CURRENT_STATE, [STATE.authorized]);
        note(CURRENT_STATE, [STATE.provisioned]);
        const url = "https://blorb.hypnodroid.com/?d=puffkin";
        note(RPC_RESULT, packet(COMMAND.wifiSettings, Uint8Array.of(url.length, ...bytes(url))));
      }, 5);
    }
  };
  return { written, service: { uuid: SERVICE, async getCharacteristic(uuid) { return chars[uuid]; } } };
}

test("join resolves with the URL the dish answers", async () => {
  const dish = fakeDish();
  const c = new Client(dish.service);
  assert.equal(await c.join("House", "hunter22"), "https://blorb.hypnodroid.com/?d=puffkin");
  assert.equal(c.state, STATE.provisioned);
  assert.deepEqual(dish.written[0], [...wifiSettings("House", "hunter22")]);
});

test("join rejects with unable to connect when the dish could not join", async () => {
  const c = new Client(fakeDish({ joins: false }).service);
  await assert.rejects(c.join("House", "wrong"), { code: ERROR.unableToConnect });
  assert.equal(c.state, STATE.authorized);
});

test("a dish that asks first: the page is told, waits for the yes, then sends", async () => {
  const dish = fakeDish({ asks: true });
  const c = new Client(dish.service);
  let asked = 0;
  assert.equal(await c.join("House", "hunter22", { onAsk: () => ++asked }), "https://blorb.hypnodroid.com/?d=puffkin");
  assert.equal(asked, 1);
  assert.equal(dish.written.length, 1, "nothing sent before the yes");
});

test("nobody says yes: rejected as not authorized, and the network never sent", async () => {
  const dish = fakeDish({ asks: true, says: false });
  const c = new Client(dish.service);
  await assert.rejects(c.authorized(null, 30), { code: ERROR.notAuthorized });
  assert.equal(dish.written.length, 0);
});

test("a bad name throws before the dish is asked", async () => {
  const dish = fakeDish({ asks: true });
  const c = new Client(dish.service);
  let asked = 0;
  await assert.rejects(c.join("", "x", { onAsk: () => ++asked }), { field: "ssid" });
  assert.equal(asked, 0);
});

// ---- 0.7.35: the dish drops Bluetooth to join; the site says whether it checked in ----

test("a dish that drops Bluetooth to join: onSent, then DROPPED on the disconnect", async () => {
  const dish = fakeDish({ drops: true });
  const c = new Client(dish.service);
  let sent = 0;
  const join = c.join("House", "hunter22", { onSent: () => ++sent });
  await new Promise((r) => setTimeout(r, 10));
  assert.equal(sent, 1, "told once, though the write's answer and provisioning both came");
  assert.equal(c.state, STATE.provisioning);
  c.dropped();
  assert.equal(await join, DROPPED);
  c.dropped();   // a second drop changes nothing
});

test("the drop cuts the write off: still DROPPED, and without a drop the write's error", async () => {
  const c = new Client(fakeDish({ cutsWrite: true }).service);
  const join = c.join("House", "hunter22");
  await new Promise((r) => setTimeout(r, 10));
  c.dropped();
  assert.equal(await join, DROPPED);
  const lone = new Client(fakeDish({ cutsWrite: true }).service);
  await assert.rejects(lone.join("House", "hunter22"), /disconnected/);
});

test("a drop with no join in flight, or before the yes, is not a DROPPED join", async () => {
  const c = new Client(fakeDish({ asks: true, says: false }).service);
  c.dropped();
  await assert.rejects(c.authorized(null, 20), { code: ERROR.notAuthorized });
});

test("clockOffset reads the site's Date header", () => {
  assert.equal(clockOffset("Wed, 29 Oct 2026 12:00:10 GMT", Date.parse("2026-10-29T12:00:00Z")), 10000);
  assert.equal(clockOffset(null, 5), 0);
  assert.equal(clockOffset("garbage", 5), 0);
});

// A fake clock: sleep moves it on at once.
const clock = () => { let t = 1000; return { now: () => t, sleep: async (ms) => { t += ms; } }; };

test("waitOnline asks every 3 s until the badge checks in", async () => {
  const k = clock();
  const seen = [];
  const r = await waitOnline({ ...k, since: 500, check: async (since) => { seen.push([k.now(), since]); return k.now() >= 10000 ? { joined: true, at: 9000 } : { joined: false, at: 100 }; } });
  assert.deepEqual(r, { joined: true, at: 9000, asked: 4, failed: 0 });
  assert.deepEqual(seen.map((x) => x[0]), [1000, 4000, 7000, 10000]);
  assert.ok(seen.every((x) => x[1] === 500));
});

test("waitOnline gives up after two minutes, and rides out failed asks", async () => {
  const k = clock();
  let n = 0;
  const r = await waitOnline({ ...k, since: 0, check: async () => { if (++n % 2) throw new Error("offline"); return { joined: false, at: null }; } });
  assert.equal(r.joined, false);
  assert.equal(r.asked, 41, "at 0, 3, ... 120 s");
  assert.equal(r.failed, 21);
});

test("waitOnline: a last_seen the site already had is not this join", async () => {
  const k = clock();
  const r = await waitOnline({ ...k, since: 0, before: 700, forMs: 9000, check: async () => ({ joined: true, at: k.now() >= 7000 ? 7000 : 700 }) });
  assert.deepEqual(r, { joined: true, at: 7000, asked: 3, failed: 0 });
  const stopped = await waitOnline({ ...k, since: 0, stop: () => true, check: async () => ({ joined: true, at: 1 }) });
  assert.equal(stopped.stopped, true);
});

// The debug link's characteristic, answering WIFI as docs/debug-ble.md shows, in 20-byte pieces.
function fakeDebugChar(reply) {
  const listeners = new Set(), written = [];
  const ch = {
    written,
    addEventListener: (_, f) => listeners.add(f), removeEventListener: (_, f) => listeners.delete(f),
    async startNotifications() {},
    async writeValueWithResponse(bytes) {
      const text = new TextDecoder().decode(bytes);
      written.push(text);
      const id = /^#([0-9a-f]+) /.exec(text)[1];
      const out = ["#99 OK not ours", "! DEBUG power=usb", ...reply.map((l) => `#${id} ${l}`)].join("\n") + "\n";
      setTimeout(() => {
        for (let i = 0; i < out.length; i += 20) {
          const v = new TextEncoder().encode(out.slice(i, i + 20));
          for (const f of listeners) f({ target: { value: new DataView(v.buffer) } });
        }
      }, 1);
    },
    listeners,
  };
  return ch;
}

const WIFI_REPLY = [
  "+ [wifi] window off, phase idle, auto on",
  "+ [wifi] network #1: ssid 19 B, password 12 B, wpa2 ch 11 rssi -61 in the last scan; last join: could not join in 20 s: AUTH_EXPIRE",
  "+ [wifi] networks 1 of 3; last outcome: FAILED could not join in 20 s",
  "OK lines=3",
];

test("askDebug sends one WIFI and gathers its lines, whatever the pieces", async () => {
  const ch = fakeDebugChar(WIFI_REPLY);
  const r = await askDebug(ch, "WIFI");
  assert.deepEqual(ch.written, ["#f1 WIFI\n"]);
  assert.equal(r.ok, true);
  assert.equal(r.text, "OK lines=3");
  assert.equal(r.lines.length, 3);
  assert.equal(ch.listeners.size, 0, "it lets go of the characteristic");
  const no = await askDebug(fakeDebugChar(["ERR 404 UNKNOWN_VERB"]), "WIFI");
  assert.deepEqual({ ok: no.ok, lines: no.lines }, { ok: false, lines: [] });
  await assert.rejects(askDebug(fakeDebugChar([]), "WIFI", { ms: 20 }), /TIMEOUT/);
});

test("wifiReason: the networks line and network #1, masked, and nothing typed", () => {
  const lines = WIFI_REPLY.slice(0, 3).map((l) => l.slice(2));
  assert.deepEqual(wifiReason(lines), [
    "networks 1 of 3; last outcome: FAILED could not join in 20 s",
    "network #1: ssid 19 B, password 12 B, wpa2 ch 11 rssi -61 in the last scan; last join: could not join in 20 s: AUTH_EXPIRE",
  ]);
  assert.deepEqual(wifiReason(["[wifi] networks 1 of 3; last outcome: joined Grandma Net"], ["Grandma Net", "pw"]),
    ["networks 1 of 3; last outcome: joined \u2026"]);
  assert.deepEqual(wifiReason(["[wifi] window off"]), []);
});
