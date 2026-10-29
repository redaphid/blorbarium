// Improv Wi-Fi over Web Bluetooth (https://www.improv-wifi.com/ble/): the
// page hands the dish a Wi-Fi network, which it remembers and uses to update
// itself when it is left alone at home (docs/wifi-ota.md "Provisioning"). The
// dish's side is lib/wifiota/include/wifiota/improv.h: it answers Wi-Fi
// settings only (no identify, no scan), and only after a hand on the dish says
// yes: it starts in "authorization required", asks on its screen ("Join
// Wi-Fi? Tip me on my side!") once the page opens the service, and says
// "authorized" when it is tipped on its side or its button pressed. No UI
// text here; the page owns that.
//
// From 0.7.35 the dish saves the network at once, says "provisioning", and
// drops Bluetooth (disconnects and stops advertising) for the whole join,
// DHCP and its check-in with the site (POST /api/badge/report), then brings
// Bluetooth back. So a join then ends in DROPPED, not a URL: the site says
// whether it checked in (GET /api/badge/online, waitOnline), and the dish's
// debug link says why not (WIFI, askDebug and wifiReason). An older dish
// answers "provisioned" with a URL before any disconnect, as before.
"use strict";
(function () {
  const SERVICE = "00467768-6228-2272-4663-277478268000";
  const CURRENT_STATE = "00467768-6228-2272-4663-277478268001";
  const ERROR_STATE = "00467768-6228-2272-4663-277478268002";
  const RPC_COMMAND = "00467768-6228-2272-4663-277478268003";
  const RPC_RESULT = "00467768-6228-2272-4663-277478268004";

  const STATE = { authorizationRequired: 1, authorized: 2, provisioning: 3, provisioned: 4 };
  const ERROR = { none: 0, invalidRpc: 1, unknownRpc: 2, unableToConnect: 3, notAuthorized: 4, unknown: 0xff };
  const COMMAND = { wifiSettings: 1, deviceInfo: 3 };
  const JOIN_MS = 30000;   // the dish tries the network for 12 s
  const ASK_MS = 32000;    // the dish asks for 30 s
  const WRITE_DROP_MS = 1500;   // a write cut off by the dish's drop waits this long for the drop
  const DROPPED = Object.freeze({ dropped: true });

  const checksum = (bytes) => bytes.reduce((sum, b) => (sum + b) & 0xff, 0);

  // Command, data length, data, checksum.
  function packet(command, data) {
    if (data.length > 255) throw new Error("TOO_LONG");
    const out = new Uint8Array(3 + data.length);
    out[0] = command;
    out[1] = data.length;
    out.set(data, 2);
    out[out.length - 1] = checksum(out.subarray(0, out.length - 1));
    return out;
  }

  // The Wi-Fi settings command: an SSID of 1..32 bytes, a password of 0..64 (empty: an open network).
  function wifiSettings(ssid, password) {
    const s = new TextEncoder().encode(ssid), p = new TextEncoder().encode(password);
    if (s.length < 1 || s.length > 32) throw Object.assign(new Error("SSID_LENGTH"), { field: "ssid" });
    if (p.length > 64) throw Object.assign(new Error("PASSWORD_LENGTH"), { field: "password" });
    return packet(COMMAND.wifiSettings, Uint8Array.of(s.length, ...s, p.length, ...p));
  }

  // An RPC result: the command and its strings, or null when it is malformed.
  function parseResult(bytes) {
    if (bytes.length < 3 || bytes[1] !== bytes.length - 3) return null;
    if (checksum(bytes.subarray(0, bytes.length - 1)) !== bytes[bytes.length - 1]) return null;
    const strings = [], decoder = new TextDecoder();
    for (let at = 2; at < bytes.length - 1;) {
      const n = bytes[at];
      if (at + 1 + n > bytes.length - 1) return null;
      strings.push(decoder.decode(bytes.subarray(at + 1, at + 1 + n)));
      at += 1 + n;
    }
    return { command: bytes[0], strings };
  }

  const bytesOf = (view) => new Uint8Array(view.buffer, view.byteOffset, view.byteLength);

  // One dish's Improv service. `retry` runs a GATT operation the way the page's
  // other characteristics do (gattRetry: Android Chrome runs one at a time).
  class Client {
    constructor(service, retry = (op) => op()) {
      this.service = service;
      this.retry = retry;
      this.chars = null;
      this.state = 0;
      this.error = 0;
      this.waiter = null;   // the one RPC in flight: { onState, onError, onResult }
      this.authWaiter = null;
    }

    // Subscribes once, on the first RPC: a dish that is never given a network costs the link nothing.
    async start() {
      if (this.chars) return;
      const get = (uuid) => this.retry(() => this.service.getCharacteristic(uuid));
      const chars = { state: await get(CURRENT_STATE), error: await get(ERROR_STATE), rpc: await get(RPC_COMMAND), result: await get(RPC_RESULT) };
      chars.state.addEventListener("characteristicvaluechanged", (e) => this.heard("state", bytesOf(e.target.value)));
      chars.error.addEventListener("characteristicvaluechanged", (e) => this.heard("error", bytesOf(e.target.value)));
      chars.result.addEventListener("characteristicvaluechanged", (e) => this.heard("result", bytesOf(e.target.value)));
      for (const c of [chars.state, chars.error, chars.result]) await this.retry(() => c.startNotifications());
      this.state = bytesOf(await this.retry(() => chars.state.readValue()))[0];
      this.chars = chars;
    }

    heard(what, bytes) {
      if (what === "state") this.state = bytes[0];
      if (what === "state") this.authWaiter?.(bytes[0]);
      if (what === "error") this.error = bytes[0];
      const w = this.waiter;
      if (!w) return;
      if (what === "state") w.onState?.(bytes[0]);
      else if (what === "error" && bytes[0] !== ERROR.none) w.onError(bytes[0]);
      else if (what === "result") {
        const r = parseResult(bytes);
        if (r) w.onResult?.(r);
      }
    }

    // Write one command and wait for `handlers` to settle it.
    async rpc(bytes, ms, handlers) {
      await this.start();
      if (this.waiter) throw new Error("BUSY");
      return new Promise((resolve, reject) => {
        let settled = false;
        const done = (fn) => (v) => { if (settled) return; settled = true; clearTimeout(timer); this.waiter = null; fn(v); };
        const ok = done(resolve);
        const no = done((code) => reject(Object.assign(new Error(code === "TIMEOUT" ? "TIMEOUT" : `IMPROV_ERROR_${code}`), { code })));
        const timer = setTimeout(() => no("TIMEOUT"), ms);
        const w = this.waiter = handlers(ok, no);
        this.retry(() => this.chars.rpc.writeValueWithResponse(bytes)).then(
          () => w.onWritten?.(),
          (e) => { if (w.onWriteError) w.onWriteError(e, done(reject)); else { clearTimeout(timer); this.waiter = null; reject(e); } });
      });
    }

    // Until a hand on the dish says yes: at once when it already has.
    // `onAsk` runs when the dish is asking, so the page can say what to do.
    // Rejects with code 4 (ERROR.notAuthorized) when nobody said yes in time.
    async authorized(onAsk, ms = ASK_MS) {
      await this.start();
      const yes = (s) => s === STATE.authorized || s === STATE.provisioned;
      if (yes(this.state)) return;
      onAsk?.();
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => { this.authWaiter = null; reject(Object.assign(new Error("NOT_AUTHORIZED"), { code: ERROR.notAuthorized })); }, ms);
        this.authWaiter = (s) => { if (yes(s)) { clearTimeout(timer); this.authWaiter = null; resolve(); } };
      });
    }

    // Join this network. Resolves with the URL the dish answers once joined
    // (before 0.7.35), or with DROPPED when the dish, having been sent the
    // network, drops Bluetooth to join it (0.7.35+; the page calls dropped()
    // on the disconnect). `onSent` runs once the network is on the dish: its
    // write is answered, or it says "provisioning". Rejects with code 3
    // (ERROR.unableToConnect) when it could not join, and code 4
    // (ERROR.notAuthorized) when nobody on the dish said yes.
    async join(ssid, password, { onAsk, onSent } = {}) {
      const bytes = wifiSettings(ssid, password);   // a bad name or password throws before the dish is asked
      await this.authorized(onAsk);
      let sent = false;
      const told = () => { if (!sent) { sent = true; onSent?.(); } };
      return this.rpc(bytes, JOIN_MS, (ok, no) => ({
        onError: no,
        onState: (s) => { if (s === STATE.provisioning) told(); },
        onResult: (r) => { if (r.command === COMMAND.wifiSettings) ok(r.strings[0] || ""); },
        onWritten: told,
        // The dish may drop the link before its write's answer comes back: the
        // network may well be on it. The disconnect, if it comes, says DROPPED.
        onWriteError: (e, fail) => setTimeout(() => fail(e), WRITE_DROP_MS),
        onDrop: () => ok(DROPPED),
      }));
    }

    // The link dropped. A join already sent ends in DROPPED: the dish is joining.
    dropped() {
      this.waiter?.onDrop?.();
    }
  }

  // ---- after the network is sent, 0.7.35+ ----

  // The site's clock less this phone's, from a response's Date header (whole
  // seconds, so a second early at worst): `since` is in the site's time.
  function clockOffset(dateHeader, localMs) {
    const t = Date.parse(dateHeader || "");
    return Number.isFinite(t) ? t - localMs : 0;
  }

  // Ask the site every `everyMs` until the badge has checked in since `since`
  // (its last_seen newer than `before`, what the site had for it already), or
  // `forMs` passed. `check(since)` resolves {joined, at}; a failed ask is
  // tried again. Resolves {joined, at, asked, failed}. `stop()` true ends it early.
  async function waitOnline({ check, since, before = null, everyMs = 3000, forMs = 120000,
    now = () => Date.now(), sleep = (ms) => new Promise((r) => setTimeout(r, ms)), stop = () => false }) {
    const until = now() + forMs;
    let asked = 0, failed = 0;
    for (;;) {
      if (stop()) return { joined: false, at: null, asked, failed, stopped: true };
      asked++;
      try {
        const r = await check(since);
        if (r && r.joined && (before == null || r.at > before)) return { joined: true, at: r.at, asked, failed };
      } catch (e) { failed++; }
      if (now() + everyMs > until) return { joined: false, at: null, asked, failed };
      await sleep(everyMs);
    }
  }

  // One request on the dish's debug link (docs/debug-ble.md, characteristic
  // 6E400005 in the UART service): "#<id> VERB" in; "#<id> + ..." lines, then
  // "#<id> OK ..." or "#<id> ERR ..." out, as notifications split anywhere.
  // Resolves {ok, text, lines}; rejects "TIMEOUT".
  const DEBUG_CHAR = "6e400005-b5a3-f393-e0a9-e50e24dcca9e";
  async function askDebug(char, cmd, { id = 0xf1, ms = 8000, retry = (op) => op() } = {}) {
    const tag = `#${id.toString(16)} `;
    let buf = "", finish = null;
    const lines = [];
    const heard = (e) => {
      buf += new TextDecoder().decode(bytesOf(e.target.value));
      let nl;
      while ((nl = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, nl).replace(/\r/g, "");
        buf = buf.slice(nl + 1);
        if (!line.startsWith(tag)) continue;
        const rest = line.slice(tag.length);
        if (rest === "+" || rest.startsWith("+ ")) lines.push(rest.slice(2));
        else if (/^(OK|ERR)\b/.test(rest)) finish?.({ ok: rest.startsWith("OK"), text: rest, lines });
      }
    };
    char.addEventListener("characteristicvaluechanged", heard);
    try {
      await retry(() => char.startNotifications());
      return await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("TIMEOUT")), ms);
        finish = (r) => { clearTimeout(timer); resolve(r); };
        const bytes = new TextEncoder().encode(`${tag}${cmd}\n`);
        retry(() => (char.writeValueWithResponse ? char.writeValueWithResponse(bytes) : char.writeValue(bytes)))
          .catch((e) => { clearTimeout(timer); reject(e); });
      });
    } finally {
      char.removeEventListener?.("characteristicvaluechanged", heard);
    }
  }

  // Why it did not check in, in the dish's own masked words: WIFI's
  // "[wifi] networks ..." line (it ends "last outcome: ...") and its
  // "network #1: ..." line. They never hold an SSID; anything the person typed
  // (`secrets`, 3 characters or more) is blanked anyway. [] when neither is there.
  function wifiReason(lines, secrets = []) {
    const bare = (l) => String(l).replace(/^\[wifi\] /, "");
    const pick = [lines.find((l) => /^networks /.test(bare(l))), lines.find((l) => /^network #1:/.test(bare(l)))];
    return pick.filter(Boolean).map((l) => {
      let t = bare(l);
      for (const s of secrets) if (s && s.length >= 3) t = t.split(s).join("\u2026");
      return t;
    });
  }

  const api = { Client, DROPPED, DEBUG_CHAR, clockOffset, waitOnline, askDebug, wifiReason, packet, wifiSettings, parseResult, checksum, SERVICE, CURRENT_STATE, ERROR_STATE, RPC_COMMAND, RPC_RESULT, STATE, ERROR, COMMAND };
  globalThis.BlorbImprov = api;
  if (typeof module !== "undefined") module.exports = api;
})();
