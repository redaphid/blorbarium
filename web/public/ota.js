// The page's side of docs/ota.md: check a release, then drive one update
// through an explicit state machine. No UI text here; the page owns that.
"use strict";
(function () {
  const WINDOW = 6144;          // bytes in flight past the last at= heard
  const STALL_MS = 2000;        // no progress for this long: ask STATUS
  const REBOOT_WAIT_MS = 15000; // how long to wait for the restart's drop

  function kv(text) {
    const out = {};
    for (const m of String(text).matchAll(/([A-Za-z_]+)=(\S*)/g)) out[m[1]] = m[2];
    return out;
  }

  function semver(v) {
    const m = /^(\d+)\.(\d+)\.(\d+)$/.exec(v || "");
    return m ? [Number(m[1]), Number(m[2]), Number(m[3])] : null;
  }
  function isNewer(a, b) {
    const x = semver(a), y = semver(b);
    if (!x || !y) return false;
    for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] > y[i];
    return false;
  }

  function b64url(bytes) {
    let s = "";
    for (const b of bytes) s += String.fromCharCode(b);
    return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
  }
  async function sha256(bytes) {
    return b64url(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)));
  }
  // The dish names an image by the first four bytes of its hash, in hex.
  function imageId(sha) {
    const bin = atob(sha.replace(/-/g, "+").replace(/_/g, "/"));
    let hex = "";
    for (let i = 0; i < 4; i++) hex += bin.charCodeAt(i).toString(16).padStart(2, "0");
    return hex;
  }

  // The manifest comes off the network, so it is checked here and trusted after.
  function parseManifest(json, board) {
    const m = json || {};
    const ok = m.board === board && semver(m.version) && Number.isInteger(m.flags) && m.flags >= 0 &&
      Number.isInteger(m.size) && m.size > 0 && /^[A-Za-z0-9_-]{43}$/.test(m.sha256 || "") &&
      /^[A-Za-z0-9_-]{86}$/.test(m.sig || "") && typeof m.image === "string" && m.image.length > 0;
    if (!ok) throw new Error("bad manifest");
    return { board: m.board, version: m.version, flags: m.flags, size: m.size, sha256: m.sha256, sig: m.sig, image: m.image };
  }

  // A release on the site: fw/<board>/manifest.json, the image beside it.
  async function fetchRelease(board, base, fetchFn = fetch) {
    const url = new URL(`fw/${board}/manifest.json`, base);
    const res = await fetchFn(url, { cache: "no-cache" });
    if (!res.ok) throw new Error(`manifest ${res.status}`);
    const manifest = parseManifest(await res.json(), board);
    return {
      manifest,
      async image() {
        const r = await fetchFn(new URL(manifest.image, url));
        if (!r.ok) throw new Error(`image ${r.status}`);
        return new Uint8Array(await r.arrayBuffer());
      },
    };
  }

  async function checkImage(manifest, bytes) {
    if (bytes.length !== manifest.size) throw new Error(`size ${bytes.length}, want ${manifest.size}`);
    if (await sha256(bytes) !== manifest.sha256) throw new Error("hash mismatch");
  }

  function packet(image, off, n) {
    const p = new Uint8Array(4 + n);
    new DataView(p.buffer).setUint32(0, off, true);
    p.set(image.subarray(off, off + n), 4);
    return p;
  }

  class Failure extends Error {
    constructor(reason) { super(reason); this.reason = reason; }
  }

  // Where a dropped connection sends each state. A drop before the restart is
  // resumed with the same BEGIN; a drop after REBOOT is the restart itself.
  const ON_DROP = {
    begin: "resuming", confirm: "resuming", sending: "resuming",
    finishing: "rebooting", rebooting: "rebooting", verifying: "rebooting",
  };

  // port: { send(cmd, timeoutMs) -> {ok, lines} | rejects {code, message},
  //         writeData(Uint8Array), reconnect() }  (reconnect gives up after ~2 min)
  // The owner forwards "OTA ..." events to event() and a lost link to dropped().
  class Updater {
    constructor({ port, release, onChange = () => {} }) {
      this.port = port;
      this.release = release;
      this.m = release.manifest;
      this.onChange = onChange;
      this.state = "idle";
      this.lost = false;
      this.dish = { state: "idle", at: 0, last: "none" };
      this.rewind = null;
      this.chunk = 0;
      this.aborted = false;
      this.outcome = null;
      this.wake = null;
    }

    event(text) {
      if (!/^OTA\b/.test(text)) return;
      this.absorb(text);
      if ("at" in kv(text) && /\bgap\b/.test(text)) this.rewind = this.dish.at;
      if (this.wake) this.wake();
    }
    dropped() { this.lost = true; if (this.wake) this.wake(); }

    absorb(text) {
      const k = kv(text);
      if (k.state) this.dish.state = k.state;
      if (k.at !== undefined) this.dish.at = Number(k.at);
      if (k.last) this.dish.last = k.last;
      if (k.chunk) this.chunk = Number(k.chunk);
      return k;
    }

    // Resolves after ms, or sooner on any event or drop.
    sleep(ms) {
      return new Promise((resolve) => {
        const done = () => { clearTimeout(t); this.wake = null; resolve(); };
        const t = setTimeout(done, ms);
        this.wake = done;
      });
    }

    set(state) {
      this.state = state;
      this.onChange({ state, at: this.dish.at, size: this.m.size, outcome: this.outcome });
    }
    finish(kind, fw) { this.outcome = { ok: true, kind, fw }; return "done"; }
    fail(reason) { this.outcome = { ok: false, reason }; return "failed"; }

    async status() { return this.absorb((await this.port.send("OTA STATUS")).ok); }

    async reconnect() {
      try { await this.port.reconnect(); } catch { throw new Failure("unreachable"); }
      this.lost = false;
    }

    async run() {
      let state = "checking";
      for (;;) {
        this.set(state);
        if (state === "done" || state === "failed") return this.outcome;
        try {
          state = await this[state]();
        } catch (e) {
          if (e instanceof Failure) state = this.fail(e.reason);
          else if (this.lost && ON_DROP[state]) state = ON_DROP[state];
          else state = this.fail(e && e.code ? e.message.replace(/^\d+ /, "") : "error");
        }
      }
    }

    async checking() {
      try {
        this.image = await this.release.image();
        await checkImage(this.m, this.image);
      } catch { throw new Failure("download"); }
      return "begin";
    }

    async begin() {
      const m = this.m;
      let r;
      try {
        r = await this.port.send(`OTA BEGIN ${m.board} ${m.version} ${m.flags} ${m.size} ${m.sha256} ${m.sig}`);
      } catch (e) {
        if (this.lost || !e.code) throw e;
        // A different, stale image holds the session. The button still guards the new one.
        if (/OTA_ACTIVE/.test(e.message) && !this.aborted) {
          this.aborted = true;
          await this.port.send("OTA ABORT");
          return "begin";
        }
        if (/NOT_NEWER/.test(e.message)) return this.finish("up_to_date", null);
        throw new Failure(e.message.replace(/^\d+ /, ""));
      }
      this.absorb(r.ok);
      return this.next() || "confirm";
    }

    // The state the dish's own state calls for, or null while it says confirm.
    next() {
      const s = this.dish.state;
      if (s === "receiving") return "sending";
      if (s === "ready") return "finishing";
      if (s === "idle") return this.fail(this.dish.last === "none" ? "lost_session" : this.dish.last);
      return null;
    }

    async confirm() {
      for (;;) {
        await this.status();
        const n = this.next();
        if (n) return n;
        await this.sleep(1000);
        if (this.lost) throw new Error("disconnected");
      }
    }

    async sending() {
      const size = this.m.size;
      let next = this.dish.at, heard = this.dish.at, moved = Date.now();
      this.rewind = null;
      for (;;) {
        if (this.lost) throw new Error("disconnected");
        if (this.dish.state !== "receiving") return this.next() || "confirm";
        if (this.rewind !== null) { next = this.rewind; this.rewind = null; }
        if (this.dish.at !== heard) {
          heard = this.dish.at;
          moved = Date.now();
          this.set("sending");
        }
        if (next < size && next - this.dish.at < WINDOW) {
          const n = Math.min(this.chunk, size - next);
          await this.port.writeData(packet(this.image, next, n));
          next += n;
          continue;
        }
        const quiet = Date.now() - moved;
        if (quiet < STALL_MS) { await this.sleep(STALL_MS - quiet); continue; }
        await this.status();
        next = this.dish.at;
        moved = Date.now();
      }
    }

    async finishing() {
      try {
        await this.port.send("OTA REBOOT");
      } catch (e) {
        if (this.lost || e.code !== 409) throw e;
        await this.status();
        return this.next() || "confirm";
      }
      return "rebooting";
    }

    async rebooting() {
      const until = Date.now() + REBOOT_WAIT_MS;
      while (!this.lost && Date.now() < until) await this.sleep(until - Date.now());
      if (this.lost) await this.reconnect();
      return "verifying";
    }

    async verifying() {
      const fw = kv((await this.port.send("HELLO")).ok).fw;
      if (fw === this.m.version) return this.finish("updated", fw);
      // Still waiting to restart (busy, or the drop was not the restart): ask again.
      await this.status();
      if (this.dish.state === "ready") return "finishing";
      return this.finish("rolled_back", fw);
    }

    async resuming() {
      await this.reconnect();
      return "begin";
    }
  }

  const api = { Updater, fetchRelease, isNewer, sha256, imageId };
  globalThis.BlorbOta = api;
  if (typeof module !== "undefined") module.exports = api;
})();
