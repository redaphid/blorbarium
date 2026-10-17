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

  // Whether Chrome's chooser lists a device for these filters: any one filter
  // whose every part matches (services AND name AND namePrefix).
  function chooserMatches(filters, ad) {
    return filters.some((f) => (!f.services || f.services.every((u) => ad.services.includes(u))) &&
      (f.name === undefined || f.name === ad.name) &&
      (f.namePrefix === undefined || (ad.name || "").startsWith(f.namePrefix)));
  }

  // faults: drop (lose the link once near 40%), gaps (lose three data writes),
  // lostAck (swallow one at= event), corrupt (flip a byte on the way in),
  // rollback (the new image does not keep itself).
  // onTrial: { from, token } starts the dish already on trial with fw, as a page
  // that comes back after the restart finds it.
  // name: what it advertises; renamed: what it advertises once it runs a newer
  // firmware than it shipped with (blorb-1b3c becomes mossby), and back on a rollback.
  class MockDish {
    constructor({ board = "lcd128", fw = "0.3.0", name = "blorb-7a3f", renamed = null, confirmMs = 30000, bootMs = 1200, trialMs = 3000, faults = {}, onTrial = null, emit = () => {}, onDrop = () => {} } = {}) {
      Object.assign(this, { board, fw, name, renamed, confirmMs, bootMs, trialMs, faults, emit, onDrop });
      this.shipped = { fw, name };
      this.timeKnown = false;
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
      if (onTrial) { this.trial = true; this.startTrial(onTrial.from, onTrial.token); }
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

    restart() {
      const old = this.fw, token = this.s.token;
      for (const name in this.timers) this.timer(name);
      this.boot(this.s.version, true, () => this.startTrial(old, token));
    }

    // On trial in the running image. It keeps itself once the page holding the
    // token that switched it in has sent CONFIRM; with no CONFIRM, or with the
    // rollback fault, its trial fails and it restarts into `old`, as the bootloader would.
    startTrial(old, token) {
      this.confirmed = false;
      this.trialToken = token;
      this.timer("trial", this.trialMs, () => {
        if (this.faults.rollback || !this.confirmed) { this.rolledback = this.fw; this.boot(old, false); }
        else this.trial = false;
      });
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
        // No battery: a restart forgets the wall time until a page sends TIME.
        Object.assign(this, { fw, trial, state: "idle", last: "none", s: null, booting: null, timeKnown: false });
        if (this.renamed) this.name = isNewer(fw, this.shipped.fw) ? this.renamed : this.shipped.name;
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

  // ---- the debug link (docs/debug-ble.md), for ?mock=1&debug=true ----
  // Its gates as the dish has them: harm needs this connection's unlock (a
  // BOOT press, debugPress here), and writes wait out a trial or an update.
  const DEBUG_REACH = { HELP: "look", STATUS: "look", UNLOCK: "look", LOCK: "look", THINK: "show", FORTUNE: "show",
    LOOK: "show", FIRE: "write", WALL: "write", SCALE: "write", HATCH: "harm", STAGE: "harm", WARP: "harm",
    CHEM: "harm", DIE: "harm", SPECIES: "harm", WIPE: "harm" };
  const STAGES = ["baby", "child", "adult", "elder"];

  MockDish.prototype.debugState = function () {
    return this.dbg ||= { unlock: "locked", phase: "egg", stage: "-", age: 0, species: "grungo", look: "own",
      scale: 1, time: "unknown", heard: [] };
  };

  // One debug line, "#<id> VERB args"; the reply lines, each with its "#<id> ".
  MockDish.prototype.debug = function (line) {
    const d = this.debugState();
    const m = /^#([0-9a-fA-F]{1,6}) (\S+) ?(.*)$/.exec(line);
    if (!m) return ["#0 ERR 400 BAD_ID"];
    const [, id, verb, rest] = m;
    const args = rest.trim() ? rest.trim().split(/\s+/) : [];
    d.heard.push(line);
    const out = (...l) => l.map((x) => `#${id} ${x}`);
    const reach = DEBUG_REACH[verb];
    if (!reach) return out("ERR 404 UNKNOWN_VERB");
    if (reach === "write" || reach === "harm") {
      if (this.trial) return out("ERR 409 TRIAL");
      if (this.state !== "idle") return out("ERR 409 OTA_ACTIVE");
    }
    if (reach === "harm" && d.unlock !== "open") return out("ERR 423 LOCKED");
    const where = () => `OK phase=${d.phase} stage=${d.stage} age=${d.age}`;
    switch (verb) {
      case "HELP": return out(...Object.entries(DEBUG_REACH).map(([v, r]) => `+ ${v} ${r} `), `OK ${Object.keys(DEBUG_REACH).length}`);
      case "STATUS": return out(`OK unlock=${d.unlock} scale=${d.scale} trial=${this.trial ? 1 : 0} ota=${this.state === "idle" ? 0 : 1} sealed=0 phase=${d.phase} stage=${d.stage} age=${d.age} species=${d.species} look=${d.look} time=${d.time} dropped=0 fight=${d.fight || "off"} last=${d.fightLast || "none"}`);
      case "UNLOCK":
        if (this.trial) return out("ERR 409 TRIAL");
        if (d.unlock === "open") return out("OK unlock=open");
        d.unlock = "asking";
        this.timer("debugUnlock", 30000, () => { if (d.unlock === "asking") { d.unlock = "locked"; this.debugEmit?.("DEBUG unlock=lapsed"); } });
        return out("OK unlock=asking ms=30000");
      case "LOCK": d.unlock = "locked"; return out("OK unlock=locked");
      case "THINK": return d.phase === "creature" ? out(`OK think=${args[0]} prophecy=${/^p_/.test(args[0]) ? 1 : 0}`) : out("ERR 409 NO_CREATURE");
      case "FORTUNE": return d.phase === "creature" ? out(`OK fortune=${args[0] || "p_feast"}`) : out("ERR 409 NO_CREATURE");
      case "LOOK": d.look = args[0] === "off" ? "own" : args[0]; return out(`OK look=${d.look}`);
      case "FIRE": return out(`OK fire=${args[0]}`);
      case "WALL": d.time = args[0] === "clear" ? "unknown" : "known"; return out(d.time === "known" ? `OK time=known wall=${args[0]} tz=${args[1] || 0} night=0` : "OK time=unknown");
      case "SCALE": d.scale = Number(args[0]); return d.scale >= 1 && d.scale <= 60 ? out(`OK scale=${d.scale}`) : out("ERR 416 OUT_OF_RANGE");
      case "HATCH": if (d.phase !== "egg") return out("ERR 409 NOT_EGG"); Object.assign(d, { phase: "creature", stage: "baby", age: 18000 }); return out(where());
      case "STAGE": {
        const s = STAGES.indexOf(args[0]);
        if (s < 0) return out("ERR 400 BAD_ARGS");
        if (d.phase === "clutch") return out("ERR 409 NO_CREATURE");
        if (d.phase === "creature" && STAGES.indexOf(d.stage) > s) return out("ERR 409 PAST_STAGE");
        Object.assign(d, { phase: "creature", stage: args[0], age: 18000 + s * 864000 });
        return out(where());
      }
      case "WARP": d.age += Number(args[0]) * 600; return out(where());
      case "CHEM": return d.phase === "creature" ? out(`OK ${args.length / 2}`) : out("ERR 409 NO_CREATURE");
      case "DIE": if (d.phase !== "creature") return out("ERR 409 NO_CREATURE"); Object.assign(d, { phase: "clutch", stage: "-" }); return out(`OK phase=clutch eggs=3 vigil=${args[0] === "remains" ? 1 : 18000}`);
      case "SPECIES": if (d.phase !== "creature") return out("ERR 409 NO_CREATURE"); d.species = args[0]; return out(`OK species=${d.species}`);
      case "WIPE":
        setTimeout(() => { Object.assign(d, { unlock: "locked", phase: "egg", stage: "-", age: 0, species: args[0] || "grungo" }); this.boot(this.fw, false); }, 500);
        return out(`OK wiping found=${args[0] || "nest"}`);
    }
    return out("ERR 404 UNKNOWN_VERB");
  };

  // BOOT pressed while the unlock asks.
  MockDish.prototype.debugPress = function () {
    const d = this.debugState();
    if (d.unlock !== "asking") return;
    d.unlock = "open";
    this.timer("debugUnlock");
    this.debugEmit?.("DEBUG unlock=open");
  };

  const api = { MockDish, mockRelease, chooserMatches };
  globalThis.BlorbOtaMock = api;
  if (typeof module !== "undefined") module.exports = api;
})();
