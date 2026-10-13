// node --test web/test/ota.test.mjs
// The sender in web/public/ota.js against the mock dish, with no browser.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { Updater, tokenStore, checkTrial } = require("../public/ota.js");
const { MockDish, mockRelease } = require("../public/ota-mock.js");

const pause = (ms) => new Promise((r) => setTimeout(r, ms));
const DEVICE = "blorb-7a3f";

// localStorage as one browser profile has it: two tabs, or a reload, share it.
function profile() {
  const m = new Map();
  return { getItem: (k) => m.get(k) ?? null, setItem: (k, v) => m.set(k, String(v)) };
}

// A dish that one page at a time hears from (its events go to the updater running).
function newDish({ confirmMs = 30000, trialMs = 300, faults = {} } = {}) {
  let listener = null;
  const dish = new MockDish({
    confirmMs, bootMs: 200, trialMs, faults,
    emit: (t) => setTimeout(() => listener && listener.event(t), 5),
    onDrop: () => listener && listener.dropped(),
  });
  dish.listen = (u) => { listener = u; };
  return dish;
}

// A page on a phone: a port shaped like the page's Link + transport, and its
// own token storage. closeAfter: the page is closed once that command is answered.
function newPage(dish, { storage = profile(), closeAfter = null } = {}) {
  let closed = false;
  const sent = [];
  const live = () => {
    if (closed) throw new Error("page closed");
    if (!dish.connected) throw new Error("disconnected");
  };
  const port = {
    async send(cmd) {
      live();
      if (`#fff ${cmd}`.length > 200) throw new Error(`line over 200 bytes: ${cmd.length + 5}`);
      sent.push(cmd);
      await pause(2);
      live();
      const [verb, ...rest] = cmd.split(" ");
      const reply = verb === "OTA" ? dish.command(rest.join(" ")) : verb === "HELLO" ? `OK fw=${dish.fw}` : "ERR 404 UNKNOWN_VERB";
      if (closeAfter && cmd.startsWith(closeAfter)) closed = true;
      if (reply.startsWith("ERR ")) throw Object.assign(new Error(reply.slice(4)), { code: parseInt(reply.slice(4), 10) });
      return { ok: reply.slice(3), lines: [] };
    },
    async writeData(bytes) { live(); await new Promise(setImmediate); dish.data(bytes); },
    async reconnect() { if (closed) throw new Error("page closed"); await dish.reconnect(); },
  };
  const tokens = tokenStore(DEVICE, storage);
  return {
    port, tokens, sent,
    async run(release, { press = true, trialWaitMs, hear = true, onState = () => {} } = {}) {
      const states = [];
      const updater = new Updater({
        port, release, tokens, trialWaitMs,
        onChange: ({ state }) => {
          if (states.at(-1) !== state) states.push(state);
          if (state === "confirm" && press) setTimeout(() => dish.press(), 50);
          onState(state);
        },
      });
      if (hear) dish.listen(updater);
      const outcome = await updater.run();
      return { outcome, states };
    },
  };
}

async function update({ press = true, faults = {}, confirmMs = 30000 } = {}) {
  const release = await mockRelease();
  const dish = newDish({ confirmMs, faults });
  const page = newPage(dish);
  const { outcome, states } = await page.run(release, { press });
  return { outcome, dish, states, release, page };
}

// Waits out the dish's restart and its trial verdict.
async function settled(dish) {
  if (dish.booting) await dish.booting;
  await pause(dish.trialMs + 100);
  if (dish.booting) await dish.booting;
}

test("an update survives a drop, lost writes and a lost at=, then restarts into the new version", { timeout: 60000 }, async () => {
  const { outcome, dish, states, release, page } = await update({ faults: { drop: true, gaps: true, lostAck: true } });
  assert.deepEqual(outcome, { ok: true, kind: "updated", fw: "0.4.0" });
  assert.equal(dish.stats.flashed, release.manifest.sha256, "the dish hashed the bytes it wrote");
  assert.equal(dish.stats.drops, 1);
  assert.equal(dish.stats.lostWrites, 3);
  assert.ok(dish.stats.gaps >= 1, "a lost write raised a gap event");
  assert.equal(dish.stats.lostAcks, 1);
  assert.equal(dish.stats.polls, 1, "only the lost at= needed a STATUS poll; each gap was answered at once");
  assert.equal(dish.stats.reboots, 1);
  assert.equal(dish.stats.confirms, 1, "the page told the new image it got through");
  assert.ok(states.includes("resuming"), `resumed after the drop: ${states}`);
  assert.deepEqual(states.slice(-5), ["finishing", "rebooting", "verifying", "trial", "done"]);
  const begin = page.sent.find((c) => c.startsWith("OTA BEGIN "));
  const token = page.tokens("0.4.0");
  assert.match(token, /^[A-Za-z0-9_-]{22}$/);
  assert.equal(begin, `OTA BEGIN lcd128 0.4.0 0 1 ${release.manifest.size} ${release.manifest.sha256} ${release.manifest.sig} ${token}`);
  assert.ok(`#fff ${begin}`.length <= 200, `BEGIN is ${begin.length + 5} bytes`);
  assert.ok(page.sent.includes(`OTA REBOOT ${token}`) && page.sent.includes(`OTA CONFIRM ${token}`));
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
  const { outcome, dish, page } = await update({ faults: { rollback: true } });
  assert.deepEqual(outcome, { ok: true, kind: "rolled_back", fw: "0.3.0" });
  assert.equal(dish.stats.reboots, 2, "into the new image on trial, then back to the old one");
  const { status } = await checkTrial(page.port, page.tokens);
  assert.equal(status.rolledback, "0.4.0", "STATUS names the version that went back");
});

test("a page reloaded after REBOOT confirms with its stored token and the update keeps", { timeout: 60000 }, async () => {
  const release = await mockRelease();
  const dish = newDish({ trialMs: 1500 });
  const storage = profile();
  const first = await newPage(dish, { storage, closeAfter: "OTA REBOOT" }).run(release);
  assert.deepEqual(first.outcome, { ok: false, reason: "unreachable" }, "the first visit closed before the trial");
  assert.equal(dish.stats.confirms, 0);
  await dish.reconnect();
  const again = newPage(dish, { storage });
  const { status, confirm } = await checkTrial(again.port, again.tokens);
  assert.equal(status.trial, "1");
  assert.equal(status.fw, "0.4.0");
  assert.equal(confirm, "confirmed");
  await settled(dish);
  assert.equal(dish.fw, "0.4.0", "kept the new version");
  assert.equal(dish.trial, false);
  assert.equal(dish.stats.reboots, 1);
  const after = (await checkTrial(again.port, again.tokens)).status;
  assert.equal(after.rolledback, "none");
});

test("a visit with another token cannot confirm, and the image rolls back", { timeout: 60000 }, async () => {
  const release = await mockRelease();
  const dish = newDish({ trialMs: 1500 });
  await newPage(dish, { closeAfter: "OTA REBOOT" }).run(release);
  await dish.reconnect();
  const stranger = newPage(dish);
  const { status, confirm } = await checkTrial(stranger.port, stranger.tokens);
  assert.equal(status.trial, "1");
  assert.equal(confirm, "not_yours");
  assert.equal(dish.stats.refusedConfirms, 1);
  await settled(dish);
  assert.equal(dish.fw, "0.3.0", "rolled back without its own page's CONFIRM");
  assert.equal(dish.stats.confirms, 0);
  await dish.reconnect();
  assert.equal((await checkTrial(stranger.port, stranger.tokens)).status.rolledback, "0.4.0");
});

test("BEGIN while an earlier update is on trial confirms it, waits it out, then updates", { timeout: 60000 }, async () => {
  const dish = newDish({ trialMs: 1500 });
  const storage = profile();
  await newPage(dish, { storage, closeAfter: "OTA REBOOT" }).run(await mockRelease({ version: "0.3.5", size: 128 * 1024 }));
  await dish.reconnect();
  assert.equal(dish.fw, "0.3.5");
  assert.equal(dish.trial, true);
  const page = newPage(dish, { storage });
  const { outcome, states } = await page.run(await mockRelease());
  assert.deepEqual(outcome, { ok: true, kind: "updated", fw: "0.4.0" });
  assert.deepEqual(states.slice(0, 5), ["checking", "begin", "trial", "begin", "confirm"], `${states}`);
  assert.ok(page.sent.includes(`OTA CONFIRM ${page.tokens("0.3.5")}`), "confirmed the earlier update with its own token");
  assert.equal(dish.stats.confirms, 2);
  assert.equal(dish.stats.reboots, 2);
  assert.equal(dish.rolledback, "none");
});

test("an update still on trial at the wait's end is unconfirmed, never updated", { timeout: 60000 }, async () => {
  const release = await mockRelease();
  const dish = newDish({ trialMs: 4000 });
  const { outcome } = await newPage(dish).run(release, { trialWaitMs: 500 });
  assert.deepEqual(outcome, { ok: true, kind: "unconfirmed", fw: "0.4.0" });
  assert.equal(dish.trial, true, "the dish was still on trial when the page gave up waiting");
  assert.equal(dish.stats.confirms, 1);
});

test("another phone's BEGIN or ABORT during a session gets NOT_YOURS and the transfer carries on", { timeout: 60000 }, async () => {
  const release = await mockRelease();
  const other = await mockRelease({ version: "0.4.1", size: 128 * 1024 });
  const dish = newDish();
  const owner = newPage(dish);
  let rivals = null;
  const { outcome } = await owner.run(release, {
    onState: (state) => {
      if (state !== "sending" || rivals) return;
      const sameImage = newPage(dish);
      const otherImage = newPage(dish);
      rivals = Promise.all([
        sameImage.run(release, { hear: false, press: false }).then((r) => ({ ...r, page: sameImage })),
        otherImage.run(other, { hear: false, press: false }).then((r) => ({ ...r, page: otherImage })),
      ]);
    },
  });
  assert.ok(rivals, "the rivals ran while the owner was sending");
  const [same, diff] = await rivals;
  assert.deepEqual(same.outcome, { ok: false, reason: "NOT_YOURS" }, "the same image with another token");
  assert.deepEqual(diff.outcome, { ok: false, reason: "NOT_YOURS" }, "another image: its ABORT was refused");
  assert.ok(diff.page.sent.includes(`OTA ABORT ${diff.page.tokens("0.4.1")}`), "it asked once, with its own token");
  assert.deepEqual(outcome, { ok: true, kind: "updated", fw: "0.4.0" });
  assert.equal(dish.stats.flashed, release.manifest.sha256);
  assert.equal(dish.stats.reboots, 1);
});

test("a token is 16 random bytes, one per dish and version, kept in storage", () => {
  const storage = profile();
  const a = tokenStore(DEVICE, storage);
  const t = a("0.4.0");
  assert.match(t, /^[A-Za-z0-9_-]{22}$/);
  assert.equal(tokenStore(DEVICE, storage)("0.4.0"), t, "a reload reuses it");
  assert.notEqual(a("0.4.1"), t);
  assert.notEqual(tokenStore("blorb-0000", storage)("0.4.0"), t);
  const broken = { getItem() { throw new Error("denied"); }, setItem() { throw new Error("denied"); } };
  const b = tokenStore("blorb-9999", broken);
  assert.equal(b("0.4.0"), b("0.4.0"), "without storage it lasts as long as the page");
});
