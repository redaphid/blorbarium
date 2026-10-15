// Web Bluetooth as Chrome gives it to the page, with mock dishes behind it, so
// web/test/ota_page.py can drive the page's real BleTransport: the chooser's
// filters, gatt.connect after a drop or a restart, and a dish that renames itself.
// window.__fakeBt.nearby: [[name, renamed], ...]; .choose: the name to pick
// (the first match if unset, none for a cancel). opens and connects are counted.
"use strict";
(function () {
  const bt = (window.__fakeBt = { nearby: [], choose: undefined, opens: 0, connects: 0, offered: [], filters: [], radio: null });
  const encode = (text) => new TextEncoder().encode(text);

  class FakeChar extends EventTarget {
    constructor(write) { super(); this.write = write; this.value = null; }
    async writeValueWithResponse(bytes) { await this.write(bytes); }
    async writeValueWithoutResponse(bytes) { await this.write(bytes); }
    async startNotifications() {}
    notify(text) { this.value = new DataView(encode(text).buffer); this.dispatchEvent(new Event("characteristicvaluechanged")); }
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
        if (!ad.dish.connected) await ad.dish.reconnect();
        // One MockTransport per connection: the dish's line protocol, as notifications.
        link = new MockTransport(() => { if (gatt.connected) { gatt.connected = false; device.dispatchEvent(new Event("gattserverdisconnected")); } }, ad.dish);
        const tx = new FakeChar(async () => {});
        link.onLine = (line) => tx.notify(line + "\n");
        const rx = new FakeChar((bytes) => link.write(new TextDecoder().decode(bytes)));
        const data = new FakeChar((bytes) => link.writeData(bytes));
        const chars = { [RX]: rx, [TX]: tx, [OTA_DATA]: data };
        gatt.connected = true;
        return { getPrimaryService: async (uuid) => {
          if (uuid !== NUS) throw new DOMException("no service", "NotFoundError");
          return { getCharacteristic: async (c) => chars[c] };
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
