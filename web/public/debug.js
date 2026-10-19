// ?debug=true (or debug=1): the debug link's panel (docs/debug-ble.md). One
// control per command on the dish's debug characteristic, 6E400005. Without
// the parameter nothing here runs and nothing is drawn. The page hands over
// the NUS service once it is connected (attach), or the mock dish (attachMock).
"use strict";
(function () {
  const CHAR = "6e400005-b5a3-f393-e0a9-e50e24dcca9e";
  const value = new URLSearchParams(globalThis.location ? location.search : "").get("debug");
  const enabled = value === "true" || value === "1";

  // From the dish's own tables (species.def, drives.def, chemicals.def,
  // stimuli.def), so the pickers offer real names; any other word still goes as typed.
  const SPECIES = ["grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz", "hushwing", "tinker"];
  const CHEMS = ["hunger", "sleepiness", "boredom", "loneliness", "fear", "pain", "discomfort", "need_touch",
    "life", "injury", "energy", "food", "adrenaline", "toxin", "warmth", "melatonin", "vision", "daze", "pluck", "mettle", "dread"];
  const STIMULI = ["knock", "double_knock", "shake", "picked_up", "put_down", "dropped", "flipped", "righted", "button",
    "button_hold", "cradle", "lid_down", "lid_up", "touched", "button_double", "hang", "pellet_dropped", "fed", "fed_bad",
    "dusk", "dawn", "owner_arrived", "owner_left", "petted", "played", "spoken_to"];
  const THOUGHTS = ["peckish", "starving", "crunch", "fuzzy_bite", "green", "ow", "heard_you", "ground_lies", "hooded",
    "counted", "put_me_down", "warm_hands", "sky_down", "heavy_eyes", "dream_pellets", "p_snack", "p_feast", "p_feed_me"];

  // "#<id> VERB args" in, "#<id> + ..." then "#<id> OK|ERR ..." out, "! DEBUG ..." events.
  class Channel {
    constructor(write, onEvent) {
      this.write = write; this.onEvent = onEvent; this.nextId = 1; this.pending = new Map(); this.buf = "";
      this.chain = Promise.resolve();
    }
    feed(text) {
      this.buf += text;
      let nl;
      while ((nl = this.buf.indexOf("\n")) >= 0) {
        const line = this.buf.slice(0, nl).replace(/\r/g, "");
        this.buf = this.buf.slice(nl + 1);
        if (line) this.receive(line);
      }
    }
    receive(line) {
      if (line.startsWith("!")) return this.onEvent(line.slice(1).trim());
      const m = /^#([0-9a-fA-F]+) (\+|OK|ERR)(?: (.*))?$/.exec(line);
      if (!m) return;
      let id = parseInt(m[1], 16);
      if (id === 0 && m[2] === "ERR") id = this.pending.keys().next().value;
      const p = this.pending.get(id);
      if (!p) return;
      if (m[2] === "+") return void p.lines.push(m[3] || "");
      this.pending.delete(id);
      clearTimeout(p.timer);
      p.resolve({ ok: m[2] === "OK", text: `${m[2]} ${m[3] || ""}`.trim(), lines: p.lines });
    }
    // One at a time, as the protocol's page does. A long jump (a whole egg) takes seconds.
    send(cmd, timeoutMs = 60000) {
      const run = () => new Promise((resolve, reject) => {
        const id = this.nextId;
        this.nextId = this.nextId >= 0xffffff ? 1 : this.nextId + 1;
        const timer = setTimeout(() => { this.pending.delete(id); reject(new Error("TIMEOUT")); }, timeoutMs);
        this.pending.set(id, { lines: [], resolve, timer });
        this.write(`#${id.toString(16)} ${cmd}\n`).catch((e) => { this.pending.delete(id); clearTimeout(timer); reject(e); });
      });
      const result = this.chain.then(run, run);
      this.chain = result.catch(() => {});
      return result;
    }
    failAll(reason) {
      for (const p of this.pending.values()) { clearTimeout(p.timer); p.resolve({ ok: false, text: reason, lines: [] }); }
      this.pending.clear();
    }
  }

  let channel = null, panel = null, mock = null;

  function el(tag, props = {}, ...kids) {
    const e = Object.assign(document.createElement(tag), props);
    e.append(...kids);
    return e;
  }
  function options(list) { return list.map((v) => el("option", { value: v, textContent: v })); }
  function datalist(id, list) { return el("datalist", { id }, ...options(list)); }

  function log(text, tone = "") {
    const li = el("li", { textContent: text });
    if (tone) li.dataset.tone = tone;
    const ol = panel.querySelector("#dbgLog");
    ol.prepend(li);
    while (ol.children.length > 80) ol.lastChild.remove();
  }

  async function run(cmd) {
    if (!channel) return log(`${cmd}: not connected`, "err");
    log(`→ ${cmd}`);
    try {
      const r = await channel.send(cmd);
      for (const l of r.lines) log(`  ${l}`);
      log(`← ${r.text}`, r.ok ? "ok" : "err");
      if (/^ERR 423/.test(r.text)) say("Locked: press Unlock, then BOOT on the dish.");
      if (/^(UNLOCK|LOCK|SCALE|LOOK|WALL)/.test(cmd) || !r.ok) refresh();
      if (cmd === "STATUS" && r.ok) applyFight(r.text);
      return r;
    } catch (e) {
      log(`← ${e.message}`, "err");
    }
  }

  function say(text) { panel.querySelector("#dbgSay").textContent = text; }

  async function refresh() {
    if (!channel) return;
    const r = await channel.send("STATUS").catch(() => null);
    if (!r || !r.ok) return;
    panel.querySelector("#dbgStatus").textContent = r.text.replace(/^OK /, "");
    applyFight(r.text);
    const unlock = (/unlock=(\w+)/.exec(r.text) || [])[1];
    panel.dataset.unlock = unlock;
    if (unlock === "asking") say("Press BOOT on the dish now (after 1.5 s, within 30 s).");
    else if (unlock === "open") say("Unlocked for this connection.");
    if (mock) panel.querySelector("#dbgMockPress").hidden = unlock !== "asking";
  }

  // The fight line: fight mode's state from STATUS, and the last ask's gate,
  // result and the radio's answer from the dish's `! DEBUG fight|radio` events.
  const fight = { state: "", gate: "", result: "", ctx: "", radio: "" };
  function showFight() {
    const f = panel.querySelector("#dbgFight");
    if (!f) return;
    const parts = [`Fight: ${fight.state || "?"}`];
    if (fight.result) parts.push(`last ask ${fight.result}${fight.ctx ? ` (${fight.ctx})` : ""}`);
    if (fight.gate) parts.push(`gate ${fight.gate}`);
    if (fight.radio) parts.push(`radio ${fight.radio}`);
    f.textContent = parts.join(" · ");
  }

  // STATUS's fight= and last=; a last of "none" never hides an ask an event already told.
  function applyFight(status) {
    const state = (/fight=(\w+)/.exec(status) || [])[1];
    const last = (/last=(\S+)/.exec(status) || [])[1];
    if (last && last !== "none") fight.gate = last;
    if (state) fight.state = state, showFight();
  }

  function onEvent(text) {
    log(`! ${text}`);
    if (/^DEBUG unlock=/.test(text)) refresh();
    if (/^DEBUG power=/.test(text)) say(`Power: ${text.slice(6)}`);
    const ask = /^DEBUG fight double=\d(?: hang=\d)? ctx=(\S+) gate=(\S+) result=(\S+)/.exec(text);
    if (ask) [, fight.ctx, fight.gate, fight.result] = ask, showFight(), refresh();
    const radio = /^DEBUG radio open=(\d)(?: err=(\S+))?/.exec(text);
    if (radio) fight.radio = radio[1] === "1" ? "open" : `refused: ${radio[2] || "?"}`, showFight();
  }

  function row(label, ...controls) {
    return el("div", { className: "dbg-row" }, el("span", { className: "dbg-label", textContent: label }), ...controls);
  }
  function button(text, cmd, extra = {}) {
    const b = el("button", { type: "button", textContent: text, className: extra.className || "ghost" });
    b.dataset.cmd = typeof cmd === "string" ? cmd : "";
    b.addEventListener("click", () => {
      const c = typeof cmd === "function" ? cmd() : cmd;
      if (c && (!extra.confirm || confirm(extra.confirm))) run(c);
    });
    return b;
  }

  function build() {
    const stage = el("select", { id: "dbgStage" }, ...options(["baby", "child", "adult", "elder"]));
    const species = el("select", { id: "dbgSpecies" }, ...options(SPECIES));
    const look = el("select", { id: "dbgLook" }, ...options(SPECIES));
    const wipe = el("select", { id: "dbgWipe" }, el("option", { value: "", textContent: "the nest (3 eggs)" }), ...options(SPECIES));
    const chem = el("input", { id: "dbgChem", placeholder: "hunger", value: "hunger", size: 10 });
    chem.setAttribute("list", "dbgChems");
    const level = el("input", { id: "dbgLevel", type: "number", min: 0, max: 1000, value: 0, style: "width:6em" });
    const stim = el("input", { id: "dbgStim", value: "knock", size: 12 });
    stim.setAttribute("list", "dbgStims");
    const thought = el("input", { id: "dbgThought", value: "crunch", size: 12 });
    thought.setAttribute("list", "dbgThoughts");
    const minutes = el("input", { id: "dbgMinutes", type: "number", min: 1, max: 120, value: 30, style: "width:5em" });
    const wall = el("input", { id: "dbgWall", type: "datetime-local" });
    const idle = el("input", { id: "dbgIdle", type: "number", min: 1, max: 3600, value: 30, style: "width:5em", title: "seconds" });
    const aim = el("select", { id: "dbgAim" }, ...options(["0", "1", "2", "3"]));
    const net = el("input", { id: "dbgNet", placeholder: "rx 30 | blackout 5000", size: 18 });
    const unix = () => Math.floor((wall.value ? new Date(wall.value) : new Date()).getTime() / 1000);
    const tz = () => -new Date().getTimezoneOffset();
    const mockPress = el("button", { id: "dbgMockPress", type: "button", className: "ghost", hidden: true, textContent: "Press BOOT (pretend)" });
    mockPress.addEventListener("click", () => { if (mock) mock.debugPress(); setTimeout(refresh, 50); });
    const retry = el("button", { id: "dbgRetry", type: "button", hidden: true, textContent: "Retry debug link" });
    retry.addEventListener("click", () => attach(lastService));

    panel = el("section", { id: "debugPanel", className: "dbg" },
      el("h2", { textContent: "Debug" }),
      el("p", { className: "hint", id: "dbgSay", textContent: "Connect to the dish. Harmful commands need Unlock and a BOOT press." }),
      el("p", { className: "dbg-status", id: "dbgStatus", textContent: "not connected" }),
      el("p", { className: "dbg-status", id: "dbgFight", textContent: "Fight: ?" }),
      retry,
      datalist("dbgChems", CHEMS), datalist("dbgStims", STIMULI), datalist("dbgThoughts", THOUGHTS),
      row("Unlock", button("Unlock", "UNLOCK", { className: "" }), button("Lock", "LOCK"), button("Status", "STATUS"), mockPress),
      el("h3", { textContent: "No press needed" }),
      row("Think", thought, button("Think", () => `THINK ${thought.value.trim()}`), button("Fortune", "FORTUNE")),
      row("Fire", stim, button("Fire", () => `FIRE ${stim.value.trim()}`)),
      row("Look as", look, button("Look", () => `LOOK ${look.value}`), button("Own look", "LOOK off")),
      row("Wall time", wall, button("Set", () => `WALL ${unix()} ${tz()}`), button("Clear", "WALL clear")),
      row("Speed", button("1x", "SCALE 1"), button("10x", "SCALE 10"), button("60x", "SCALE 60")),
      row("Power", button("Low now", "POWER low"), button("Wake", "POWER wake"), button("Status", "POWER"),
        idle, button("Idle after", () => `POWER idle ${idle.value}`), button("10 min", "POWER idle default")),
      row("Fight", button("Fight mode?", "DUEL")),
      el("h3", { textContent: "Needs the unlock" }),
      row("Hatch", button("Hatch now", "HATCH")),
      row("Stage", stage, button("Grow", () => `STAGE ${stage.value}`)),
      row("Warp", minutes, button("Live minutes", () => `WARP ${minutes.value}`)),
      row("Chemical", chem, level, button("Set", () => `CHEM ${chem.value.trim()} ${level.value}`)),
      row("Die", button("Die, show eggs", "DIE eggs", { confirm: "He dies now. Continue?" }),
        button("Die, at the remains", "DIE remains", { confirm: "He dies now. Continue?" })),
      row("Species", species, button("Change", () => `SPECIES ${species.value}`, { confirm: "Change his species for good?" })),
      row("Nap", button("Nap now", "NAP")),
      row("Button", button("Press", "PRESS"), button("Hold 1.5 s", "PRESS 1500"), button("Double", "DOUBLE")),
      row("No button", button("Knock knock", "KNOCK 2"), button("Knock", "KNOCK 1"), button("Side hold", "SIDE 2500"),
        button("Side hold 4 s", "SIDE 4000")),
      row("Gesture", button("Hang", "HANG 2500"), button("Wobbly hang", "HANG 3000 1"), button("Cradle", "CRADLE"),
        aim, button("Lean", () => `LEAN ${aim.value}`)),
      row("Radio", net, button("Faults", () => `NET ${net.value.trim()}`.trim()), button("Clear", "NET clear")),
      row("Reboot", button("Reboot", "REBOOT", { confirm: "Restart the badge (it saves first)?" }),
        button("Reboot quiet", "REBOOT quiet", { confirm: "Restart with the radio dark until NET blackout 0?" })),
      row("Wipe", wipe, button("Wipe the pet", () => `WIPE ${wipe.value}`.trim(),
        { className: "danger", confirm: "Erase this pet and its whole lineage, and restart as a fresh board?" })),
      el("ol", { id: "dbgLog", className: "dbg-log" }));
    panel.append(el("style", { textContent: `
.dbg { background: var(--surface); border: 1px dashed var(--warn); border-radius: 14px; padding: 12px 16px; min-width: 0; }
.dbg h3 { margin: 14px 0 4px; font-size: 0.95rem; color: var(--muted); }
.dbg-row { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; margin: 6px 0; }
.dbg-label { min-width: 6.5em; color: var(--muted); font-size: 0.9rem; }
.dbg-row input, .dbg-row select { font: inherit; max-width: 100%; min-width: 0; }
.dbg-status { font: 0.75rem/1.4 var(--font-mono); overflow-wrap: anywhere; color: var(--teal); }
.dbg-log { list-style: none; padding: 0; margin: 10px 0 0; font: 0.72rem/1.45 var(--font-mono); max-height: 40vh; overflow: auto; }
.dbg-log li { white-space: pre-wrap; overflow-wrap: anywhere; }
.dbg-log li[data-tone="err"] { color: var(--warn); }
.dbg-log li[data-tone="ok"] { color: var(--teal); }
.dbg .danger { color: var(--warn); border-color: var(--warn); }` }));
    const footer = document.getElementById("footer");
    if (footer) footer.before(panel);
    else document.body.append(panel);
  }

  // early: ask STATUS now (the mock); a real link asks once its notifications are on.
  function connected(write, early = true) {
    if (channel) channel.failAll("disconnected");
    channel = new Channel(write, onEvent);
    say("Connected. Harmful commands need Unlock and a BOOT press.");
    if (early) refresh();
    return channel;
  }

  // Android Chrome runs one Bluetooth operation at a time and does not queue
  // across characteristics, so a debug write or subscribe that meets the
  // page's own traffic fails ("GATT operation already in progress" / "failed
  // for unknown reason"). Each debug GATT call is retried with backoff; only a
  // missing characteristic (NotFoundError) or a dropped link gives up at once.
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  async function retryGatt(service, op, tries = 10) {
    for (let i = 1; ; i++) {
      try {
        return await op();
      } catch (e) {
        const gone = service.device && service.device.gatt && !service.device.gatt.connected;
        if ((e && e.name === "NotFoundError") || gone || i >= tries) throw e;
        await sleep(120 * i);
      }
    }
  }

  let lastService = null;
  // The NUS service of a freshly opened connection. A firmware without the
  // debug link has no such characteristic: the panel says so and stays idle.
  async function attach(service) {
    if (!enabled || !service) return;
    lastService = service;
    showRetry(false);
    say("Opening the debug link…");
    await sleep(800);   // the page's first lines (TIME, HELLO, OTA STATUS) go first
    if (service !== lastService) return;
    try {
      const c = await retryGatt(service, () => service.getCharacteristic(CHAR));
      const ch = connected((text) => {
        const bytes = new TextEncoder().encode(text);
        return retryGatt(service, () => (c.writeValueWithResponse ? c.writeValueWithResponse(bytes) : c.writeValue(bytes)));
      }, false);
      c.addEventListener("characteristicvaluechanged", (e) => ch === channel && ch.feed(new TextDecoder().decode(e.target.value)));
      await retryGatt(service, () => c.startNotifications());
      service.device.addEventListener("gattserverdisconnected", () => {
        if (ch !== channel) return;
        channel.failAll("disconnected");
        channel = null;
        say("Disconnected: the unlock and any speed-up ended with the connection.");
      }, { once: true });
      refresh();
      run("FREEHATCH on");   // ?debug: eggs hatch without a phone's TIME
    } catch (e) {
      channel = null;
      showRetry(true);
      if (e && e.name === "NotFoundError") {
        say(`This dish shows no debug link (${e.message}). Firmware 0.7.0 and newer has one: update it first. ` +
          "If it already runs 0.7.0, the phone may hold an old copy of its Bluetooth layout: in Android Settings, " +
          "Apps, show system apps, Bluetooth, Storage, clear the cache; then connect again.");
      } else {
        say(`The debug link did not open (${e && e.name}: ${e && e.message}). Tap Retry debug link.`);
      }
    }
  }
  function showRetry(on) {
    const b = panel && panel.querySelector("#dbgRetry");
    if (b) b.hidden = !on;
  }

  // ?mock=1: the mock dish answers debug lines (ota-mock.js MockDish.debug).
  function attachMock(dish) {
    if (!enabled || !dish || !dish.debug) return;
    mock = dish;
    const ch = connected(async (text) => {
      const lines = dish.debug(text.trim());
      setTimeout(() => { for (const l of lines) ch.feed(l + "\n"); }, 20);
    });
    dish.debugEmit = (text) => ch.feed(`! ${text}\n`);
    run("FREEHATCH on");
  }

  if (enabled && globalThis.document) {
    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", build);
    else build();
  }
  globalThis.BlorbDebug = { enabled, attach, attachMock, send: (cmd) => run(cmd) };
})();
