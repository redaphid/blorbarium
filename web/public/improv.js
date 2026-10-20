// Improv Wi-Fi over Web Bluetooth (https://www.improv-wifi.com/ble/): the
// page hands the dish a Wi-Fi network, which it remembers and uses to update
// itself when plugged in (docs/wifi-ota.md "Provisioning"). The dish's side is
// lib/wifiota/include/wifiota/improv.h. No UI text here; the page owns that.
"use strict";
(function () {
  const SERVICE = "00467768-6228-2272-4663-277478268000";
  const CURRENT_STATE = "00467768-6228-2272-4663-277478268001";
  const ERROR_STATE = "00467768-6228-2272-4663-277478268002";
  const RPC_COMMAND = "00467768-6228-2272-4663-277478268003";
  const RPC_RESULT = "00467768-6228-2272-4663-277478268004";

  const STATE = { authorizationRequired: 1, authorized: 2, provisioning: 3, provisioned: 4 };
  const ERROR = { none: 0, invalidRpc: 1, unknownRpc: 2, unableToConnect: 3, notAuthorized: 4, unknown: 0xff };
  const COMMAND = { wifiSettings: 1, identify: 2, deviceInfo: 3, scanNetworks: 4 };
  const JOIN_MS = 60000;   // the dish tries one network for 15 s, after a duel's radio lets go
  const SCAN_MS = 20000;

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
        const done = (fn) => (v) => { clearTimeout(timer); this.waiter = null; fn(v); };
        const ok = done(resolve);
        const no = done((code) => reject(Object.assign(new Error(code === "TIMEOUT" ? "TIMEOUT" : `IMPROV_ERROR_${code}`), { code })));
        const timer = setTimeout(() => no("TIMEOUT"), ms);
        this.waiter = handlers(ok, no);
        this.retry(() => this.chars.rpc.writeValueWithResponse(bytes)).catch((e) => { clearTimeout(timer); this.waiter = null; reject(e); });
      });
    }

    // Join this network. Resolves with the URL the dish answers once joined;
    // rejects with code 3 (ERROR.unableToConnect) when it could not join.
    join(ssid, password) {
      return this.rpc(wifiSettings(ssid, password), JOIN_MS, (ok, no) => ({
        onError: no,
        onResult: (r) => { if (r.command === COMMAND.wifiSettings) ok(r.strings[0] || ""); },
      }));
    }

    // The networks the dish hears: { ssid, rssi, secured }, strongest first.
    scan() {
      const found = [];
      return this.rpc(packet(COMMAND.scanNetworks, new Uint8Array(0)), SCAN_MS, (ok, no) => ({
        onError: no,
        onResult: (r) => {
          if (r.command !== COMMAND.scanNetworks) return;
          if (!r.strings.length) return ok(found);   // the end of the list
          found.push({ ssid: r.strings[0], rssi: Number(r.strings[1]), secured: r.strings[2] === "YES" });
        },
      }));
    }
  }

  const api = { Client, packet, wifiSettings, parseResult, checksum, SERVICE, CURRENT_STATE, ERROR_STATE, RPC_COMMAND, RPC_RESULT, STATE, ERROR, COMMAND };
  globalThis.BlorbImprov = api;
  if (typeof module !== "undefined") module.exports = api;
})();
