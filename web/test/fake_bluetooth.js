// Web Bluetooth as Chrome gives it to the page, with mock dishes behind it, so
// web/test/ota_page.py can drive the page's real BleTransport: the chooser's
// filters, gatt.connect after a drop or a restart, and a dish that renames itself.
// window.__fakeBt.nearby: [[name, renamed], ...]; .choose: the name to pick
// (the first match if unset, none for a cancel). opens and connects are counted.
"use strict";
(function () {
  // busy: the next N debug-characteristic operations fail as Android Chrome's
  // do when they meet another operation in flight; noDebug: a firmware without it.
  // improv: {drops, awayMs} gives the dish an Improv service; one that drops
  // (0.7.35+) leaves Bluetooth for awayMs once sent a network, else it answers
  // "provisioned" with a URL. mac: STATE's mac=. wifiLines: the debug link's WIFI reply.
  const bt = (window.__fakeBt = { nearby: [], choose: undefined, opens: 0, connects: 0, offered: [], filters: [], radio: null,
    busy: 0, noDebug: false, debugHeard: [], improv: null, mac: null, wifiLines: null, awayUntil: 0, improvWrites: 0 });
  const IMPROV = "00467768-6228-2272-4663-277478268000";
  const DBG = "6e400005-b5a3-f393-e0a9-e50e24dcca9e";
  const busyCheck = () => {
    if (bt.busy > 0) { bt.busy--; throw new DOMException("GATT operation already in progress.", "NetworkError"); }
  };
  const encode = (text) => new TextEncoder().encode(text);

  class FakeChar extends EventTarget {
    constructor(write) { super(); this.write = write; this.value = null; }
    async writeValueWithResponse(bytes) { await this.write(bytes); }
    async writeValueWithoutResponse(bytes) { await this.write(bytes); }
    async startNotifications() {}
    notify(text) { this.value = new DataView(encode(text).buffer); this.dispatchEvent(new Event("characteristicvaluechanged")); }
  }

  // Improv as lib/wifiota/src/improv.cpp answers it, already authorized.
  function fakeImprov(gatt) {
    const char = (bytes) => Object.assign(new EventTarget(), {
      value: new DataView(Uint8Array.from(bytes).buffer),
      async startNotifications() {}, async readValue() { return this.value; },
      set(b) { this.value = new DataView(Uint8Array.from(b).buffer); this.dispatchEvent(new Event("characteristicvaluechanged")); },
    });
    const state = char([2]), error = char([0]), result = char([]), rpc = char([]);
    rpc.writeValueWithResponse = async () => {
      bt.improvWrites++;
      state.set([3]);
      setTimeout(() => {
        if (bt.improv.drops) { bt.awayUntil = Date.now() + (bt.improv.awayMs || 1500); gatt.disconnect(); return; }
        const url = new TextEncoder().encode("https://blorb.hypnodroid.com/?d=mossby");
        const body = [1, url.length + 1, url.length, ...url];
        state.set([4]);
        result.set([...body, body.reduce((a, b) => (a + b) & 255, 0)]);
      }, 30);
    };
    const chars = { "00467768-6228-2272-4663-277478268001": state, "00467768-6228-2272-4663-277478268002": error,
      "00467768-6228-2272-4663-277478268003": rpc, "00467768-6228-2272-4663-277478268004": result };
    return { getCharacteristic: async (u) => chars[u] };
  }

  function fakeDevice(ad) {
    const device = new EventTarget();
    // Chrome keeps the name the device had in the chooser, and an id that never changes.
    Object.assign(device, { name: ad.name, id: `fake-${ad.dish.shipped.name}` });
    let link = null;
    const gatt = (device.gatt = {
      connected: false,
      async connect() {
        bt.connects++;
        if (Date.now() < bt.awayUntil) throw new DOMException("Connection attempt failed.", "NetworkError");
        if (!ad.dish.connected) await ad.dish.reconnect();
        // One MockTransport per connection: the dish's line protocol, as notifications.
        link = new MockTransport(() => { if (gatt.connected) { gatt.connected = false; device.dispatchEvent(new Event("gattserverdisconnected")); } }, ad.dish);
        const tx = new FakeChar(async () => {});
        link.onLine = (line) => tx.notify((bt.mac && / \+ phase=/.test(line) ? `${line} species=mossby mac=${bt.mac}` : line) + "\n");
        const rx = new FakeChar((bytes) => link.write(new TextDecoder().decode(bytes)));
        const data = new FakeChar((bytes) => link.writeData(bytes));
        const chars = { [RX]: rx, [TX]: tx, [OTA_DATA]: data };
        if (!bt.noDebug) {
          const dbg = new FakeChar(async (bytes) => {
            busyCheck();
            for (const line of new TextDecoder().decode(bytes).split(String.fromCharCode(10)).filter(Boolean)) {
              bt.debugHeard.push(line);
              const wifi = /^#([0-9a-f]+) WIFI$/.exec(line);
              const reply = wifi && bt.wifiLines ? [...bt.wifiLines.map((l) => `#${wifi[1]} + ${l}`), `#${wifi[1]} OK lines=${bt.wifiLines.length}`] : ad.dish.debug(line);
              setTimeout(() => dbg.notify(reply.join(String.fromCharCode(10)) + String.fromCharCode(10)), 10);
            }
          });
          dbg.startNotifications = async () => busyCheck();
          chars[DBG] = dbg;
        }
        gatt.connected = true;
        const improv = bt.improv && fakeImprov(gatt);
        return { getPrimaryService: async (uuid) => {
          if (uuid === IMPROV && improv) return improv;
          if (uuid !== NUS) throw new DOMException("no service", "NotFoundError");
          return { device, getCharacteristic: async (c) => {
            if (!chars[c]) throw new DOMException(`No Characteristics matching UUID ${c} found in Service.`, "NotFoundError");
            return chars[c];
          } };
        } };
      },
      disconnect() { if (gatt.connected) ad.dish.drop(); },
    });
    return device;
  }

  const fake = {
    async requestDevice({ filters }) {
      bt.opens++;
      bt.filters.push(filters);
      if (!globalThis.BlorbOtaMock) await loadScript("ota-mock.js");
      bt.radio ||= bt.nearby.map(([name, renamed]) => {
        const dish = mockDish(name, renamed || null);
        return { dish, services: [NUS], get name() { return dish.name; } };
      });
      const listed = bt.radio.filter((ad) => BlorbOtaMock.chooserMatches(filters, ad));
      bt.offered.push(listed.map((ad) => ad.name));
      const ad = bt.choose === undefined ? listed[0] : listed.find((a) => a.name === bt.choose);
      if (!ad) throw new DOMException("User cancelled the requestDevice() chooser.", "NotFoundError");
      return fakeDevice(ad);
    },
  };
  Object.defineProperty(Navigator.prototype, "bluetooth", { get: () => fake, configurable: true });
})();
