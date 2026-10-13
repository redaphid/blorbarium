// ?mock=1: the dish's side of docs/ota.md, faithful enough to test the page
// against, plus a radio that misbehaves on cue. Loaded only in mock mode.
"use strict";
(function () {
  const { sha256, isNewer, imageId } = globalThis.BlorbOta || require("./ota.js");
  const SLOT = 0x1e0000;
  const CHUNK = 240;
  const AT_EVERY = 4096;

  // A deterministic stand-in image with a real SHA-256. The mock dish does not
  // check the signature: real verification is on the dish.
  async function mockRelease({ board = "lcd128", version = "0.4.0", size = 256 * 1024 } = {}) {
    const image = new Uint8Array(size);
    let x = 0x9e3779b9;
    for (let i = 0; i < size; i++) { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; image[i] = x & 255; }
    const manifest = { board, version, flags: 0, fmt: 1, size, sha256: await sha256(image), sig: "A".repeat(86), image: "mock.bin" };
    return { manifest, image: async () => image.slice() };
  }

  // faults: drop (lose the link once near 40%), gaps (lose three data writes),
  // lostAck (swallow one at= event), corrupt (flip a byte on the way in),
  // rollback (the new image does not keep itself).
  class MockDish {
    constructor({ board = "lcd128", fw = "0.3.0", confirmMs = 30000, bootMs = 1200, trialMs = 3000, faults = {}, emit = () => {}, onDrop = () => {} } = {}) {
      Object.assign(this, { board, fw, confirmMs, bootMs, trialMs, faults, emit, onDrop });
      this.state = "idle";
      this.last = "none";
      this.rolledback = "none";
      this.trialToken = null;
      this.s = null;
      this.connected = true;
      this.booting = null;
      this.lastGap = -Infinity;
      this.timers = {};
      this.dropAt = faults.drop ? 0.4 : Infinity;
      this.loseWritesAt = faults.gaps ? [0.15, 0.55, 0.85] : [];
      this.loseAckAt = faults.lostAck ? 0.7 : Infinity;
      this.corruptAt = faults.corrupt ? 0.5 : Infinity;
      this.stats = { drops: 0, lostWrites: 0, gaps: 0, lostAcks: 0, polls: 0, reboots: 0, confirms: 0, refusedConfirms: 0, ignored: 0, flashed: null };
    }

    say(text) { if (this.connected) this.emit(text); }
    timer(name, ms, fn) { clearTimeout(this.timers[name]); this.timers[name] = ms == null ? undefined : setTimeout(fn, ms); }
    to(state, last) {
      this.state = state;
      if (state === "idle") { this.last = last; this.s = null; this.timer("confirm"); this.timer("idle"); this.timer("ready"); }
      this.say(state === "idle" ? `OTA state=idle last=${last}` : state === "receiving" ? `OTA state=receiving at=${this.s.at}` : `OTA state=${state}`);
    }
    where() {
      if (this.state === "idle") return `state=idle board=${this.board} fw=${this.fw} chunk=${CHUNK} trial=${this.trial ? 1 : 0} last=${this.last} rolledback=${this.rolledback}`;
      return `state=${this.state} at=${this.s.at} size=${this.s.size} id=${this.s.id} chunk=${CHUNK}`;
    }

    // One OTA request's arguments ("BEGIN ...", "STATUS"); returns the reply after "#<id> ".
    command(args) {
      const [sub, ...a] = args.trim().split(/\s+/);
      if (sub === "STATUS") { if (this.state === "receiving" && this.s.at > 0) this.stats.polls++; return `OK ${this.where()}`; }
      // The session, and the trial it leads to, belong to the token its BEGIN carried.
      if (sub === "ABORT") {
        if (!this.s) return "OK state=idle";
        if (a[0] !== this.s.token) return "ERR 403 NOT_YOURS";
        this.to("idle", "aborted");
        return "OK state=idle";
      }
      if (sub === "REBOOT") {
        if (this.s && a[0] !== this.s.token) return "ERR 403 NOT_YOURS";
        if (this.state !== "ready") return "ERR 409 NOT_READY";
        setTimeout(() => this.restart(), 300);
        return "OK rebooting";
      }
      if (sub === "CONFIRM") {
        if (!this.trial) return "OK trial=0";
        if (a[0] !== this.trialToken) { this.stats.refusedConfirms++; return "ERR 403 NOT_YOURS"; }
        this.confirmed = true;
        this.stats.confirms++;
        return "OK trial=1";
      }
      if (sub === "BEGIN") return this.begin(a);
      return "ERR 400 BAD_ARGS";
    }

    // In the dish's order: an open session first, then the cheap refusals.
    begin(a) {
      const [board, version, flags, fmt, size, sha, sig, token] = a;
      if (a.length !== 8 || !/^\d+\.\d+\.\d+$/.test(version) || !/^\d+$/.test(flags) || !/^\d+$/.test(fmt) ||
          !/^\d+$/.test(size) || !/^[A-Za-z0-9_-]{43}$/.test(sha) || !/^[A-Za-z0-9_-]{86}$/.test(sig) ||
          !/^[A-Za-z0-9_-]{22}$/.test(token)) return "ERR 400 BAD_ARGS";
      const id = imageId(sha);
      if (this.s && this.s.id !== id) return "ERR 409 OTA_ACTIVE";
      if (this.s && this.s.token !== token) return "ERR 403 NOT_YOURS";
      if (this.s) return `OK ${this.where()}`;
      if (board !== this.board) return "ERR 403 WRONG_BOARD";
      if (!(Number(flags) & 1) && !isNewer(version, this.fw)) return "ERR 409 NOT_NEWER";
      if (Number(size) === 0 || Number(size) > SLOT) return "ERR 413 TOO_BIG";
      if (this.trial) return "ERR 409 TRIAL";
      this.s = { id, version, token, size: Number(size), sha, at: 0, buf: new Uint8Array(Number(size)) };
      this.to("confirm");
      this.timer("confirm", this.confirmMs, () => this.to("idle", "no_confirm"));
      return `OK ${this.where()}`;
    }

    press() {
      if (this.state !== "confirm") return;
      this.timer("confirm");
      this.to("receiving");
      this.timer("idle", 300000, () => this.to("idle", "timeout"));
    }

    // One write on the data characteristic, as it reached the dish.
    data(bytes) {
      const s = this.s;
      const off = new DataView(bytes.buffer, bytes.byteOffset).getUint32(0, true);
      if (s && off >= this.loseWritesAt[0] * s.size) { this.loseWritesAt.shift(); this.stats.lostWrites++; return; }
      if (this.state !== "receiving" || off !== s.at) {
        this.stats.ignored++;
        const now = Date.now();
        if (this.state === "receiving" && off > s.at && now - this.lastGap >= 200) {
          this.lastGap = now;
          this.stats.gaps++;
          this.say(`OTA at=${s.at} gap`);
        }
        return;
      }
      const payload = bytes.subarray(4);
      if (s.at + payload.length > s.size) return this.to("idle", "overflow");
      s.buf.set(payload, s.at);
      if (s.at >= this.corruptAt * s.size) { s.buf[s.at] ^= 0xff; this.corruptAt = Infinity; }
      const before = s.at;
      s.at += payload.length;
      this.timer("idle", 300000, () => this.to("idle", "timeout"));
      if (Math.floor(s.at / AT_EVERY) > Math.floor(before / AT_EVERY) && s.at < s.size) {
        if (s.at >= this.loseAckAt * s.size) { this.loseAckAt = Infinity; this.stats.lostAcks++; }
        else this.say(`OTA at=${s.at}`);
      }
      if (s.at >= this.dropAt * s.size) { this.dropAt = Infinity; this.stats.drops++; setTimeout(() => this.drop(), 0); }
      if (s.at === s.size) this.check(s);
    }

    async check(s) {
      const ok = await sha256(s.buf) === s.sha;
      if (this.s !== s) return;
      if (!ok) return this.to("idle", "bad_hash");
      this.stats.flashed = s.sha;
      this.timer("idle");
      this.to("ready");
      this.timer("ready", 20000, () => this.restart());
    }

    drop() {
      if (!this.connected) return;
      this.connected = false;
      this.onDrop();
    }

    // Into the new image, on trial. It keeps itself once a page has sent
    // CONFIRM; with no CONFIRM, or with the rollback fault, its trial fails and
    // it restarts into the old one, as the bootloader would.
    restart() {
      const version = this.s.version, old = this.fw;
      for (const name in this.timers) this.timer(name);
      this.confirmed = false;
      this.trialToken = this.s.token;
      this.boot(version, true, () => this.timer("trial", this.trialMs, () => {
        if (this.faults.rollback || !this.confirmed) { this.rolledback = version; this.boot(old, false); }
        else this.trial = false;
      }));
    }

    // A lineage-writing twist (rename) waits for the trial: a rollback would undo it.
    twist(args) {
      if (this.trial && /^rename /.test(args)) return "ERR 409 TRIAL";
      return null;
    }

    boot(fw, trial, then = () => {}) {
      this.stats.reboots++;
      this.drop();
      this.booting = new Promise((resolve) => setTimeout(() => {
        Object.assign(this, { fw, trial, state: "idle", last: "none", s: null, booting: null });
        then();
        resolve();
      }, this.bootMs));
    }

    // The page coming back to the same device.
    async reconnect() {
      await new Promise((r) => setTimeout(r, 300));
      if (this.booting) await this.booting;
      this.connected = true;
    }
  }

  const api = { MockDish, mockRelease };
  globalThis.BlorbOtaMock = api;
  if (typeof module !== "undefined") module.exports = api;
})();
