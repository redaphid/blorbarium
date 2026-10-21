// node --test web/test/improv.test.mjs
// The page's Improv Wi-Fi client (web/public/improv.js) against the packets
// the dish's lib/wifiota/src/improv.cpp sends, with no browser.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { Client, packet, wifiSettings, parseResult, SERVICE, CURRENT_STATE, ERROR_STATE, RPC_COMMAND, RPC_RESULT, STATE, ERROR, COMMAND } =
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
function fakeDish({ joins = true, asks = false, says = true } = {}) {
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
