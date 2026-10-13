// node --test web/test/ota.test.mjs
// The sender in web/public/ota.js against the mock dish, with no browser.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { Updater } = require("../public/ota.js");
const { MockDish, mockRelease } = require("../public/ota-mock.js");

const pause = (ms) => new Promise((r) => setTimeout(r, ms));

// The dish behind a port shaped like the page's Link + transport.
async function update({ press = true, faults = {}, confirmMs = 30000 } = {}) {
  const release = await mockRelease();
  let updater;
  const dish = new MockDish({
    confirmMs, bootMs: 200, trialMs: 300, faults,
    emit: (t) => setTimeout(() => updater.event(t), 5),
    onDrop: () => updater.dropped(),
  });
  const live = () => { if (!dish.connected) throw new Error("disconnected"); };
  const port = {
    async send(cmd) {
      live();
      await pause(2);
      live();
      const [verb, ...rest] = cmd.split(" ");
      const reply = verb === "OTA" ? dish.command(rest.join(" ")) : verb === "HELLO" ? `OK fw=${dish.fw}` : "ERR 404 UNKNOWN_VERB";
      if (reply.startsWith("ERR ")) throw Object.assign(new Error(reply.slice(4)), { code: parseInt(reply.slice(4), 10) });
      return { ok: reply.slice(3), lines: [] };
    },
    async writeData(bytes) { live(); await new Promise(setImmediate); dish.data(bytes); },
    reconnect: () => dish.reconnect(),
  };
  const states = [];
  updater = new Updater({
    port, release,
    onChange: ({ state }) => {
      if (states.at(-1) !== state) states.push(state);
      if (state === "confirm" && press) setTimeout(() => dish.press(), 50);
    },
  });
  const outcome = await updater.run();
  return { outcome, dish, states, release };
}

test("an update survives a drop, lost writes and a lost at=, then restarts into the new version", { timeout: 60000 }, async () => {
  const { outcome, dish, states, release } = await update({ faults: { drop: true, gaps: true, lostAck: true } });
  assert.deepEqual(outcome, { ok: true, kind: "updated", fw: "0.4.0" });
  assert.equal(dish.stats.flashed, release.manifest.sha256, "the dish hashed the bytes it wrote");
  assert.equal(dish.stats.drops, 1);
  assert.equal(dish.stats.lostWrites, 3);
  assert.ok(dish.stats.gaps >= 1, "a lost write raised a gap event");
  assert.equal(dish.stats.lostAcks, 1);
  assert.equal(dish.stats.polls, 1, "only the lost at= needed a STATUS poll; each gap was answered at once");
  assert.equal(dish.stats.reboots, 1);
  assert.ok(states.includes("resuming"), `resumed after the drop: ${states}`);
  assert.deepEqual(states.slice(-5), ["finishing", "rebooting", "verifying", "trial", "done"]);
});

test("nobody presses the button", { timeout: 60000 }, async () => {
  const { outcome, dish } = await update({ press: false, confirmMs: 300 });
  assert.deepEqual(outcome, { ok: false, reason: "no_confirm" });
  assert.equal(dish.fw, "0.3.0");
  assert.equal(dish.stats.reboots, 0);
});

test("a byte corrupted on the way in ends in bad_hash and no restart", { timeout: 60000 }, async () => {
  const { outcome, dish } = await update({ faults: { corrupt: true } });
  assert.deepEqual(outcome, { ok: false, reason: "bad_hash" });
  assert.equal(dish.stats.flashed, null);
  assert.equal(dish.stats.reboots, 0);
  assert.equal(dish.fw, "0.3.0");
});

test("a new image that does not keep itself reports the rollback", { timeout: 60000 }, async () => {
  const { outcome, dish } = await update({ faults: { rollback: true } });
  assert.deepEqual(outcome, { ok: true, kind: "rolled_back", fw: "0.3.0" });
  assert.equal(dish.stats.reboots, 2, "into the new image on trial, then back to the old one");
});
