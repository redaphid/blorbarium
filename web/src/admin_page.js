// The owner's dashboard (#41), served by the Worker at /admin. It holds no
// data itself: every view reads /api/admin/* with the admin token the owner
// types in (kept for this tab in sessionStorage). Deployed, put Cloudflare
// Access in front of /admin* and /api/admin* as well.
export const ADMIN_HTML = String.raw`<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blorbarium Admin</title>
<meta name="robots" content="noindex">
<style>
/* Layout: a top bar of views; each view is one column of panels, tables scroll inside themselves. */
:root { --bg: #f1f3ee; --panel: #fff; --fg: #1b2420; --muted: #5f6b64; --rule: #d7ddd5; --accent: #0d7a6c; --warn: #a15c00; --bad: #b42318; --good: #19734a; --screen: #0f1a17; --glow: #6ee7d3;
  --body: system-ui, -apple-system, "Segoe UI", sans-serif; --mono: ui-monospace, "SF Mono", Menlo, monospace; }
@media (prefers-color-scheme: dark) { :root { --bg: #101613; --panel: #18201c; --fg: #e3ebe6; --muted: #9aa8a0; --rule: #2b3631; --accent: #4fd1bf; --warn: #f0a640; --bad: #f97066; --good: #4ade80; --screen: #050a08; --glow: #7ef2de; color-scheme: dark; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--fg); font: 14px/1.45 var(--body); }
header { display: flex; flex-wrap: wrap; gap: 6px 18px; align-items: center; padding: 10px 16px; border-bottom: 1px solid var(--rule); background: var(--panel); position: sticky; top: 0; z-index: 2; }
header b { font-size: 1.05rem; } header nav { display: flex; gap: 4px; flex-wrap: wrap; }
header nav a, button, .btn { font: inherit; border: 1px solid var(--rule); background: var(--panel); color: var(--fg); padding: 4px 10px; border-radius: 6px; cursor: pointer; text-decoration: none; }
header nav a.on { background: var(--accent); color: var(--bg); border-color: var(--accent); }
button.primary { background: var(--accent); color: var(--bg); border-color: var(--accent); }
button.danger { color: var(--bad); border-color: var(--bad); }
button:focus-visible, a:focus-visible, input:focus-visible, textarea:focus-visible, select:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }
main { max-width: 1200px; margin: 0 auto; padding: 16px; display: grid; gap: 16px; }
.panel { background: var(--panel); border: 1px solid var(--rule); border-radius: 8px; padding: 14px; display: grid; gap: 10px; min-width: 0; }
h2 { margin: 0; font-size: 1.1rem; } h3 { margin: 0; font-size: .95rem; }
.muted { color: var(--muted); } .mono { font-family: var(--mono); font-size: .85em; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 10px; }
.tile { border: 1px solid var(--rule); border-radius: 8px; padding: 10px; } .tile b { display: block; font-size: 1.5rem; font-variant-numeric: tabular-nums; }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; }
th, td { text-align: left; padding: 6px 8px; border-bottom: 1px solid var(--rule); vertical-align: top; }
th { color: var(--muted); font-size: .78rem; font-weight: 600; }
tr.link { cursor: pointer; } tr.link:hover td { background: color-mix(in srgb, var(--accent) 8%, transparent); }
.chip { display: inline-block; padding: 0 8px; border-radius: 999px; font-size: .78rem; border: 1px solid currentColor; }
.s-pending, .s-queued, .s-paused { color: var(--warn); } .s-delivered, .s-running, .s-active, .s-ready { color: var(--accent); }
.s-done, .s-alive { color: var(--good); } .s-failed, .s-killed, .s-cancelled, .s-expired, .s-dead, .s-superseded { color: var(--muted); }
.cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 340px), 1fr)); gap: 16px; }
.say { display: inline-block; font-family: var(--mono); font-size: .78rem; background: var(--screen); color: var(--glow); padding: 2px 8px; border-radius: 999px; margin: 0 4px 4px 0; }
dl.kv { display: grid; grid-template-columns: max-content 1fr; gap: 2px 12px; margin: 0; } dl.kv dt { color: var(--muted); } dl.kv dd { margin: 0; }
textarea, input, select { font: inherit; color: var(--fg); background: var(--bg); border: 1px solid var(--rule); border-radius: 6px; padding: 6px 8px; width: 100%; }
textarea { min-height: 7rem; } .row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; } .row > input, .row > select { width: auto; flex: 1 1 160px; }
#toast { position: fixed; right: 16px; bottom: 16px; background: var(--fg); color: var(--bg); padding: 8px 14px; border-radius: 8px; }
svg text { fill: var(--muted); font-size: 11px; } svg .line { fill: none; stroke: var(--accent); stroke-width: 2; } svg .grid { stroke: var(--rule); }
details summary { cursor: pointer; color: var(--accent); }
.chip.ok { color: var(--good); } .chip.warn { color: var(--warn); } .chip.bad, .bad { color: var(--bad); }
</style></head>
<body>
<header><b>Blorbarium admin</b><nav id="nav"></nav><span class="muted" id="who"></span></header>
<main id="main"></main>
<div id="toast" hidden></div>
<script>
const $ = (s, el = document) => el.querySelector(s);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const when = (t) => (t ? new Date(t).toLocaleString() : "–");
const chip = (s) => '<span class="chip s-' + esc(s) + '">' + esc(s) + "</span>";
let token = ""; try { token = sessionStorage.getItem("blorbAdmin") || ""; } catch {}
function toast(msg) { const t = $("#toast"); t.textContent = msg; t.hidden = false; clearTimeout(toast.h); toast.h = setTimeout(() => (t.hidden = true), 3500); }
async function api(path, body) {
  const r = await fetch("/api/admin" + path, { method: body === undefined ? "GET" : "POST", headers: { authorization: "Bearer " + token, "content-type": "application/json" }, body: body === undefined ? undefined : JSON.stringify(body) });
  if (r.status === 401) { token = ""; try { sessionStorage.removeItem("blorbAdmin"); } catch {} route(); throw new Error("The admin token was refused."); }
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || ("HTTP " + r.status));
  return j;
}
const VIEWS = [["overview", "Badges"], ["health", "Health"], ["ota", "OTA"], ["analytics", "Analytics"]];
function nav(on) { $("#nav").innerHTML = VIEWS.map(([k, n]) => '<a href="#' + k + '" class="' + (on === k ? "on" : "") + '">' + n + "</a>").join(""); }

function login() {
  nav("");
  $("#main").innerHTML = '<form class="panel" id="login"><h2>Sign in</h2><p class="muted">The admin token (the Worker secret ADMIN_TOKEN). It stays in this tab only.</p><input id="tok" type="password" autocomplete="current-password" placeholder="Admin token"><div><button class="primary">Sign in</button></div></form>';
  $("#login").onsubmit = (e) => { e.preventDefault(); token = $("#tok").value.trim(); try { sessionStorage.setItem("blorbAdmin", token); } catch {} route(); };
}

async function overview() {
  const o = await api("/overview");
  const c = o.counts;
  $("#main").innerHTML = '<section class="tiles">' + [["badges", o.badges.length], ["creatures", c.creatures], ["line packs", c.packs], ["diary entries", c.diary], ["open actions", c.open_actions]].map(([k, v]) => '<div class="tile"><span class="muted">' + k + "</span><b>" + v + "</b></div>").join("") + "</section>" +
    '<section class="panel"><div class="row"><h2 style="flex:1">Badges</h2><button id="all-settings">Settings for all…</button></div><div class="scroll"><table><thead><tr><th>badge</th><th>firmware</th><th>last seen</th><th>pet now</th><th>pack</th><th>open actions</th></tr></thead><tbody>' +
    o.badges.map((b) => { const p = b.current; return '<tr class="link" data-mac="' + esc(b.mac) + '"><td><b>' + esc(b.label || "") + '</b> <span class="mono">' + esc(b.mac) + "</span></td><td>" + esc(b.fw || "–") + "</td><td>" + when(b.last_seen) + "</td><td>" + (p ? esc(p.name || "") + " " + esc(p.species) + " · gen " + esc(p.generation ?? "?") + " · " + esc(p.stage || "") + " " + chip(p.status) : "–") + "</td><td>" + (p && p.pack_version ? "v" + p.pack_version + (p.pack_pending ? " " + chip("pending") : "") : "–") + "</td><td>" + b.open_actions + "</td></tr>"; }).join("") +
    "</tbody></table></div></section>" + pushPanel("all");
  for (const tr of document.querySelectorAll("tr[data-mac]")) tr.onclick = () => (location.hash = "badge/" + tr.dataset.mac);
  $("#all-settings").onclick = () => $("#push-all").scrollIntoView();
  wirePush("all");
}

// Settings and commands, for one badge or all of them.
function pushPanel(target) {
  const id = target === "all" ? "all" : "one";
  return '<section class="panel" id="push-' + id + '"><h2>' + (target === "all" ? "Push to every badge" : "Push to this badge") + '</h2>' +
    '<div class="row"><select id="kind-' + id + '"><option value="settings">Settings</option><option value="command">Command</option></select>' +
    '<select id="preset-' + id + '"><option value="">Preset…</option><option value=\'{"idle_timeout_s":120}\'>Idle timeout</option><option value=\'{"home_orientation":"usb_down"}\'>Home orientation</option><option value=\'{"wifi":[{"ssid":"","psk":""}]}\'>Wi-Fi networks</option><option value=\'{"debug_verbs":["fps","latency"]}\'>Allowed debug verbs</option><option value=\'{"verb":"REBOOT","args":[]}\'>Command: reboot</option></select></div>' +
    '<textarea id="payload-' + id + '" class="mono" spellcheck="false">{"idle_timeout_s": 120}</textarea>' +
    '<div class="row"><input id="expires-' + id + '" type="number" min="0" placeholder="Expires after (hours, optional)"><button class="primary" id="send-' + id + '">Queue it</button></div></section>';
}
function wirePush(target, mac) {
  const id = target === "all" ? "all" : "one";
  $("#preset-" + id).onchange = (e) => { if (e.target.value) { $("#payload-" + id).value = JSON.stringify(JSON.parse(e.target.value), null, 1); if (/verb/.test(e.target.value)) $("#kind-" + id).value = "command"; } };
  $("#send-" + id).onclick = async () => {
    let payload; try { payload = JSON.parse($("#payload-" + id).value); } catch { return toast("The payload is not valid JSON."); }
    const h = Number($("#expires-" + id).value) || 0;
    try { const r = await api("/actions", { macs: target === "all" ? "all" : [mac], kind: $("#kind-" + id).value, payload, expires_in_s: h * 3600 || undefined }); toast("Queued for " + r.ids.length + " badge" + (r.ids.length === 1 ? "" : "s") + "."); route(); } catch (e) { toast(e.message); }
  };
}

function actionsTable(actions) {
  return '<div class="scroll"><table><thead><tr><th>#</th><th>kind</th><th>payload</th><th>status</th><th>created</th><th>delivered</th><th>result</th><th></th></tr></thead><tbody>' +
    actions.map((a) => "<tr><td>" + a.id + "</td><td>" + esc(a.kind) + '</td><td class="mono">' + esc(JSON.stringify(a.payload)).slice(0, 120) + "</td><td>" + chip(a.status) + "</td><td>" + when(a.created_at) + "</td><td>" + (a.delivered_at ? when(a.delivered_at) + " · " + esc(a.delivered_via) : "–") + '</td><td class="mono">' + esc(a.result ? JSON.stringify(a.result) : "") + "</td><td>" + (["pending", "delivered"].includes(a.status) ? '<button class="danger" data-cancel="' + a.id + '">Cancel</button>' : "") + "</td></tr>").join("") +
    "</tbody></table></div>";
}
function wireCancel() { for (const b of document.querySelectorAll("[data-cancel]")) b.onclick = async () => { await api("/actions/" + b.dataset.cancel + "/cancel", {}); toast("Cancelled."); route(); }; }

async function badge(mac) {
  const d = await api("/badges/" + mac);
  const b = d.badge;
  const byLineage = {};
  for (const c of d.creatures) (byLineage[c.lineage_id || "(no lineage id)"] ||= []).push(c);
  $("#main").innerHTML = '<section class="panel"><div class="row"><h2 style="flex:1">' + esc(b.label || "Badge") + ' <span class="mono muted">' + esc(b.mac) + '</span></h2><input id="label" value="' + esc(b.label || "") + '" placeholder="Label"><button id="save-label">Save label</button></div>' +
    '<dl class="kv"><dt>firmware</dt><dd>' + esc(b.fw || "–") + "</dd><dt>first seen</dt><dd>" + when(b.first_seen) + "</dd><dt>last seen</dt><dd>" + when(b.last_seen) + "</dd><dt>settings</dt><dd class=\"mono\">" + esc(b.settings ? JSON.stringify(b.settings) : "–") + "</dd></dl></section>" +
    '<section class="panel" id="remote"><h2>Remote debug</h2><p class="muted">Loading…</p></section>' +
    '<section class="panel"><h2>Creatures, by lineage</h2>' + Object.entries(byLineage).map(([l, cs]) => "<h3>" + esc(l) + '</h3><div class="scroll"><table><thead><tr><th>pet</th><th>gen</th><th>stage</th><th>status</th><th>pack</th><th>first seen</th></tr></thead><tbody>' +
      cs.map((c) => '<tr class="link" data-c="' + c.id + '"><td>' + esc(c.name || "") + " " + esc(c.species) + "</td><td>" + esc(c.generation ?? "?") + "</td><td>" + esc(c.stage || "") + "</td><td>" + chip(c.status) + "</td><td>" + (c.pack_version ? "v" + c.pack_version : "–") + "</td><td>" + when(c.first_seen) + "</td></tr>").join("") + "</tbody></table></div>").join("") + "</section>" +
    '<section class="panel"><h2>Actions</h2>' + actionsTable(d.actions) + "</section>" + pushPanel("one", mac) +
    '<section class="panel"><h2>Installed packs</h2>' + (d.installs.length ? '<div class="scroll"><table><thead><tr><th>when</th><th>pet</th><th>version</th><th>how</th></tr></thead><tbody>' + d.installs.map((i) => "<tr><td>" + when(i.installed_at) + '</td><td><a href="#creature/' + i.creature_id + '">' + esc(i.name || "") + " " + esc(i.species) + "</a></td><td>v" + i.version + "</td><td>" + esc(i.via || "") + "</td></tr>").join("") + "</tbody></table></div>" : '<p class="muted">The badge has not confirmed a pack yet.</p>') + "</section>" +
    '<section class="panel"><h2>Device debug</h2><div class="row"><select id="dbgKind"><option value="">Every kind</option>' + d.debugKinds.map((k) => '<option value="' + esc(k.kind) + '">' + esc(k.kind) + " (" + k.n + ")</option>").join("") + '</select><label class="muted">since <input id="dbgSince" type="date" style="width:auto"></label><button id="dbgGo">Show</button></div><div id="dbgList"></div><pre class="mono" id="rawBody" style="white-space:pre-wrap;max-height:28rem;overflow:auto;margin:0" hidden></pre></section>' +
    '<section class="panel"><h2>Analytics uploads</h2>' + (d.uploads.length ? '<table><tbody>' + d.uploads.map((u) => "<tr><td>#" + u.id + "</td><td>" + esc(u.fw || "") + "</td><td>" + when(u.uploaded_at) + "</td></tr>").join("") + "</tbody></table>" : '<p class="muted">None yet.</p>') + "</section>";
  for (const tr of document.querySelectorAll("tr[data-c]")) tr.onclick = () => (location.hash = "creature/" + tr.dataset.c);
  const loadDebug = async () => {
    const since = $("#dbgSince").value ? Date.parse($("#dbgSince").value) : 0;
    const r = await api("/debug?mac=" + mac + "&kind=" + encodeURIComponent($("#dbgKind").value) + "&since=" + since);
    $("#dbgList").innerHTML = r.items.length ? '<div class="scroll"><table><thead><tr><th>when</th><th>kind</th><th>summary</th><th>firmware</th><th>via</th><th>size</th><th></th></tr></thead><tbody>' + r.items.map((i) => "<tr><td>" + when(i.received_at) + "</td><td>" + esc(i.kind) + '</td><td class="mono">' + esc(i.summary || "") + "</td><td>" + esc(i.fw || "") + "</td><td>" + esc(i.via || "") + "</td><td>" + Math.round(i.size / 1024 * 10) / 10 + " KB" + (i.in_r2 ? " (R2)" : "") + '</td><td><button data-raw="device/' + i.id + '">Show</button></td></tr>').join("") + "</tbody></table></div>" : '<p class="muted">Nothing uploaded matches.</p>';
    wireRaw();
  };
  $("#dbgGo").onclick = () => loadDebug().catch((e) => toast(e.message));
  loadDebug().catch(() => {});
  $("#save-label").onclick = async () => { await api("/badges/" + mac, { label: $("#label").value }); toast("Saved."); };
  wireCancel(); wirePush("one", mac);
  remotePanel(mac).catch((e) => ($("#remote").innerHTML = "<h2>Remote debug</h2><p>" + esc(e.message) + "</p>"));
}

// ---- remote debug (reports.js) ----
const ago = (t, now) => { if (!t) return "never"; const s = Math.round(((now || Date.now()) - t) / 1000); return s < 90 ? s + " s ago" : s < 5400 ? Math.round(s / 60) + " min ago" : s < 129600 ? Math.round(s / 3600) + " h ago" : Math.round(s / 86400) + " d ago"; };
const dur = (s) => (s == null ? "–" : s < 5400 ? Math.round(s / 60) + " min" : s < 172800 ? Math.round(s / 3600) + " h" : Math.round(s / 86400) + " d");
const tag = (text, cls) => '<span class="chip ' + cls + '">' + esc(text) + "</span>";
// A badge at a glance: red when updates are off or it crashed, amber when silent or short of RAM.
function verdict(b, now) {
  if (!b.received_at) return tag("no report yet", "warn");
  if (b.auto_on === 0) return tag("no Wi-Fi network", "bad");
  if (b.crashed) return tag("crashed (" + b.reset + ")", "bad");
  if (now - b.received_at > 36 * 3600e3) return tag("silent " + ago(b.received_at, now), "warn");
  if (b.minheap != null && b.minheap < 4096) return tag("minheap " + b.minheap, "warn");
  return tag("healthy", "ok");
}
function otaCell(o, b) {
  if (!o) return "–";
  return (o.auto ? "" : '<b class="bad">no network</b> · ') + "next " + dur(b.next_check_s) + " · every " + dur((o.every_min || 0) * 60) +
    (o.absent_streak || o.server_streak ? " · streaks " + o.absent_streak + "/" + o.server_streak : "") + (o.on_trial ? " · on trial" : "") + (o.rolled_back ? ' · <b class="bad">rolled back ' + esc(o.rolled_back) + "</b>" : "") +
    '<br><span class="muted">' + esc(b.ota_last || o.last || "") + "</span>";
}
function petCell(p) { return p ? esc(p.kind) + (p.stage ? " · " + esc(p.stage) : "") + (p.age_min != null ? " · " + dur(p.age_min * 60) : "") + (p.hunger_pct != null ? " · hunger " + p.hunger_pct + "%" : "") : "–"; }

async function health() {
  const h = await api("/health");
  const now = h.now;
  $("#main").innerHTML = '<section class="panel"><h2>Badge health</h2><p class="muted">Each badge reports at the end of every Wi-Fi window that reaches the site (a few times a day, when it is dark and nobody is using it). Click a badge for its log, crashes and remote commands.</p><div class="scroll"><table><thead><tr><th>badge</th><th>verdict</th><th>last report</th><th>firmware</th><th>up</th><th>last reset</th><th>updates</th><th>heap / min</th><th>battery</th><th>signal</th><th>pet</th><th>cmds</th></tr></thead><tbody>' +
    h.badges.map((b) => '<tr class="link" data-mac="' + esc(b.mac) + '"><td><b>' + esc(b.label || "") + '</b> <span class="mono">' + esc(b.mac) + "</span></td><td>" + verdict(b, now) + "</td><td>" + ago(b.received_at, now) + "</td><td>" + esc(b.fw || b.badge_fw || "–") + "</td><td>" + dur(b.uptime_s) + "</td><td>" + esc(b.reset || "–") + (b.crashes ? ' <span class="bad">(' + b.crashes + " crash" + (b.crashes === 1 ? "" : "es") + " kept)</span>" : "") + "</td><td>" + otaCell(b.ota, b) + "</td><td>" + (b.heap != null ? Math.round(b.heap / 1024) + " KB / " + b.minheap + " B" : "–") + "</td><td>" + (b.vbat_mv ? (b.vbat_mv / 1000).toFixed(2) + " V" : "–") + "</td><td>" + (b.rssi != null ? b.rssi + " dBm" : "–") + "</td><td>" + petCell(b.pet) + "</td><td>" + (b.open_commands || "") + "</td></tr>").join("") +
    "</tbody></table></div></section>";
  for (const tr of document.querySelectorAll("tr[data-mac]")) tr.onclick = () => (location.hash = "badge/" + tr.dataset.mac);
}

// The cable's shot lines (paint/shot.h: PackBits over little-endian RGB565, base64 a line) onto a canvas.
function drawShot(canvas, text) {
  const lines = text.split("\n").filter((l) => l.startsWith("[hw] shot "));
  const begin = /w=(\d+) h=(\d+)/.exec(lines[0] || "");
  if (!begin) throw new Error("not a shot");
  const W = Number(begin[1]), H = Number(begin[2]);
  const bytes = [];
  for (const l of lines.slice(1)) { const c = l.slice(10); if (c.startsWith("end")) break; const b = atob(c); for (let i = 0; i < b.length; i++) bytes.push(b.charCodeAt(i)); }
  const px = [];
  for (let i = 0; i < bytes.length;) {
    const h = bytes[i++];
    if (h < 128) { const p = bytes[i] | (bytes[i + 1] << 8); i += 2; for (let k = 0; k <= h; k++) px.push(p); }
    else for (let k = 0; k < h - 127; k++, i += 2) px.push(bytes[i] | (bytes[i + 1] << 8));
  }
  canvas.width = W; canvas.height = H;
  const ctx = canvas.getContext("2d"), img = ctx.createImageData(W, H);
  for (let i = 0; i < W * H && i < px.length; i++) {
    const p = px[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    img.data.set([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255], i * 4);
  }
  ctx.putImageData(img, 0, 0);
}

async function remotePanel(mac) {
  const d = await api("/badges/" + mac + "/reports");
  const el = $("#remote");
  const cmd = (c) => esc(c.payload?.verb || "") + (c.payload?.arg != null ? " " + esc(c.payload.arg) : "");
  el.innerHTML = '<h2>Remote debug</h2><p class="muted">Commands go out in the reply to the badge\'s next report and run once its Wi-Fi window closes; their results come back in the report after. Updates can be checked sooner or less often, never switched off.</p>' +
    '<div class="row"><button data-cmd="check">Check for updates</button><input id="every" type="number" min="30" max="720" step="30" placeholder="Check every … min (30–720)" style="flex:0 1 220px"><button data-cmd="interval">Set interval</button><button data-cmd="state">Capture state</button><button data-cmd="shot">Screenshot</button><button class="danger" data-cmd="reboot">Reboot</button></div>' +
    '<div class="row"><input id="marquee" maxlength="120" placeholder="A message for the kids: it scrolls across the screen (120 characters, plain letters)"><button class="primary" data-cmd="marquee">Send message</button></div>' +
    '<div class="row"><input id="debugLine" maxlength="120" class="mono" placeholder="Any cable DEBUG line: wifi, wifi list, power stats, ota check… (its output comes back in the next report\'s state)"><button data-cmd="debug">Run</button></div>' +
    (d.commands.length ? '<div class="scroll"><table><thead><tr><th>#</th><th>command</th><th>status</th><th>queued</th><th>sent</th><th>result</th></tr></thead><tbody>' + d.commands.map((c) => "<tr><td>" + c.id + '</td><td class="mono">' + cmd(c) + "</td><td>" + chip(c.status) + "</td><td>" + when(c.created_at) + "</td><td>" + (c.delivered_at ? when(c.delivered_at) : "–") + '</td><td class="mono">' + esc(c.result?.text || "") + (c.result?.shot_id ? ' <button data-shot="' + c.result.shot_id + '">View</button>' : "") + "</td></tr>").join("") + "</tbody></table></div>" : "") +
    "<h3>Reports</h3>" + (d.reports.length ? '<div class="scroll"><table><thead><tr><th>when</th><th>firmware</th><th>up</th><th>reset</th><th>heap / min / big</th><th>battery</th><th>signal</th><th>window ended</th><th></th></tr></thead><tbody>' +
      d.reports.map((r, i) => "<tr><td>" + when(r.received_at) + "</td><td>" + esc(r.fw || "") + "</td><td>" + dur(r.uptime_s) + "</td><td>" + (r.crashed ? '<b class="bad">' + esc(r.reset) + "</b>" : esc(r.reset || "")) + "</td><td>" + (r.heap ?? "–") + " / " + (r.minheap ?? "–") + " / " + (r.big ?? "–") + "</td><td>" + (r.vbat_mv ? (r.vbat_mv / 1000).toFixed(2) + " V" : "–") + "</td><td>" + (r.rssi ?? "–") + '</td><td class="mono">' + esc(r.ota_last || "") + '</td><td><button data-rep="' + i + '">Show</button></td></tr>').join("") + "</tbody></table></div>" : '<p class="muted">No report yet: one comes at the end of the badge\'s next Wi-Fi window.</p>') +
    '<div id="repOne"></div>';
  for (const b of el.querySelectorAll("[data-cmd]")) b.onclick = async () => {
    const verb = b.dataset.cmd, body = { verb };
    if (verb === "interval") body.arg = Number($("#every").value);
    if (verb === "marquee") body.arg = $("#marquee").value.slice(0, 120);
    if (verb === "debug") body.arg = $("#debugLine").value.trim();
    if (verb === "reboot" && !confirm("Reboot this badge at its next quiet moment?")) return;
    try { await api("/badges/" + mac + "/debug", body); toast("Queued: " + verb + ". It goes out at the badge's next Wi-Fi window."); remotePanel(mac); } catch (e) { toast(e.message); }
  };
  const showShot = async (id, into) => { into.innerHTML = '<canvas style="width:240px;height:240px;border-radius:50%;background:#000;image-rendering:pixelated"></canvas>'; try { drawShot($("canvas", into), (await api("/blob/device/" + id)).lines); } catch (e) { into.textContent = e.message; } };
  for (const b of el.querySelectorAll("[data-shot]")) b.onclick = () => showShot(b.dataset.shot, $("#repOne"));
  for (const b of el.querySelectorAll("[data-rep]")) b.onclick = () => {
    const r = d.reports[Number(b.dataset.rep)];
    const pre = (title, text) => (text ? "<h3>" + title + '</h3><pre class="mono" style="white-space:pre-wrap;max-height:28rem;overflow:auto;margin:0">' + esc(text) + "</pre>" : "");
    $("#repOne").innerHTML = "<h3>Report of " + when(r.received_at) + "</h3>" + (r.shot_id ? '<div id="repShot"></div>' : "") +
      pre("Crash (core dump of the boot before)", r.crash ? JSON.stringify(r.crash, null, 1) : "") + pre("Command results", r.results && r.results.length ? r.results.map((x) => "#" + x.id + " " + (x.ok ? "ok " : "REFUSED ") + x.text).join("\n") : "") +
      pre("Updates", JSON.stringify(r.ota, null, 1)) + pre("Pet", JSON.stringify(r.pet)) + pre("State capture", r.state_text) + pre("Serial log, last 8 KB", r.log_text);
    if (r.shot_id) showShot(r.shot_id, $("#repShot"));
    $("#repOne").scrollIntoView({ block: "nearest" });
  };
}

function wireRaw() {
  for (const b of document.querySelectorAll("[data-raw]")) b.onclick = async () => {
    const pre = $("#rawBody");
    pre.hidden = false; pre.textContent = "Loading…";
    try { pre.textContent = JSON.stringify(await api("/blob/" + b.dataset.raw), null, 1); } catch (e) { pre.textContent = e.message; }
    pre.scrollIntoView({ block: "nearest" });
  };
}
function linesList(lines) { return '<dl class="kv">' + Object.entries(lines).map(([k, ls]) => "<dt>" + esc(k) + "</dt><dd>" + ls.map((l) => '<span class="say">' + esc(l) + "</span>").join("") + "</dd>").join("") + "</dl>"; }

async function creature(id) {
  const d = await api("/creatures/" + id);
  const c = d.creature, latest = d.personalities[0];
  const raw = c.raw || {};
  $("#main").innerHTML = '<section class="panel"><h2>' + esc(c.name || "") + " " + esc(c.species) + ' <span class="muted">gen ' + esc(c.generation ?? "?") + " · " + esc(c.stage || "") + "</span> " + chip(c.status) + '</h2><p><a href="#badge/' + esc(c.mac) + '">Badge ' + esc(c.mac) + "</a>" + (c.parent_id ? ' · <a href="#creature/' + c.parent_id + '">parent</a>' : "") + d.children.map((k) => ' · <a href="#creature/' + k.id + '">child (gen ' + esc(k.generation) + ")</a>").join("") + "</p>" +
    '<div class="cols"><div><h3>Genes</h3><dl class="kv"><dt>power</dt><dd>' + esc(raw.power) + "</dd><dt>loves</dt><dd>" + esc(raw.lovedStance) + "</dd><dt>startle gain</dt><dd>" + esc(raw.startleGain) + "</dd><dt>hunger fill</dt><dd>" + esc((raw.hungerFill || []).join(", ") || "none") + "</dd><dt>melatonin fill</dt><dd>" + esc((raw.melatoninFill || []).join(", ") || "none") + "</dd><dt>heirlooms</dt><dd>" + esc((raw.heirlooms || []).length) + "</dd><dt>genes</dt><dd>" + esc(raw.genes) + '</dd><dt>genome</dt><dd class="mono">' + esc(c.genome_hash.slice(0, 16)) + "…</dd></dl></div>" +
    '<div><h3>Traits</h3><pre class="mono" style="white-space:pre-wrap;margin:0">' + esc(c.traits) + "</pre></div></div></section>" +
    '<section class="panel"><h2>Personality</h2><p class="muted">Version ' + (latest ? latest.version + " · " + esc(latest.source) + " · " + esc(latest.model || "") + " " + esc(latest.prompt_version || "") + " · " + when(latest.created_at) : "none yet") + '</p><textarea id="bible">' + esc(latest ? latest.bible : "") + '</textarea><div class="row"><label><input type="checkbox" id="push" checked style="width:auto"> push the new pack to the badge</label><button class="primary" id="regen-edit">Write lines from this text</button><button id="regen-fresh">Write a fresh personality from the genes</button></div>' +
    (d.personalities.length > 1 ? "<details><summary>Earlier versions</summary>" + d.personalities.slice(1).map((p) => "<p><b>v" + p.version + "</b> " + esc(p.source) + " · " + when(p.created_at) + "<br>" + esc(p.bible) + "</p>").join("") + "</details>" : "") + "</section>" +
    '<section class="panel"><h2>Line packs</h2><div class="scroll"><table><thead><tr><th>version</th><th>status</th><th>installed</th><th>model, prompt, sampling</th><th>retries</th><th>rejected</th><th>fell back</th><th>tokens in / out</th><th>cost</th><th>time</th><th>created</th><th></th></tr></thead><tbody>' +
    d.packs.map((p) => { const inst = d.installs.filter((i) => i.pack_id === p.id); return "<tr><td>v" + p.version + (p.provenance.nightly ? ' <span class="muted">nightly</span>' : "") + "</td><td>" + chip(p.status) + "</td><td>" + (inst.length ? inst.map((i) => when(i.installed_at) + " · " + esc(i.via)).join("<br>") : (p.pending_device ? chip("pending") : when(p.delivered_at))) + "</td><td>" + esc(p.provenance.model) + " " + esc(p.provenance.prompt) + '<br><span class="muted mono">' + esc(p.provenance.sampling ? JSON.stringify(p.provenance.sampling) : "") + "</span></td><td>" + esc(p.provenance.retries) + "</td><td>" + esc((p.provenance.rejected || []).length) + "</td><td>" + esc(p.fallback.length) + "</td><td>" + esc((p.provenance.meter?.inTok ?? 0) + " / " + (p.provenance.meter?.outTok ?? 0)) + "</td><td>$" + Number(p.provenance.meter?.usd || 0).toFixed(3) + "</td><td>" + Math.round((p.provenance.ms || 0) / 1000) + " s</td><td>" + when(p.created_at) + '</td><td><button data-pushpack="' + p.id + '" data-v="' + p.version + '">Push</button> <button data-lines="' + p.version + '">Lines</button></td></tr>'; }).join("") + "</tbody></table></div><div id=\"packLines\"></div>" +
    (d.packs[0] ? "<details><summary>Lines in v" + d.packs[0].version + " (" + Object.keys(d.packs[0].lines).length + " situations)</summary>" + linesList(d.packs[0].lines) + "</details>" : "") + "</section>" +
    '<section class="panel"><h2>Generation history</h2><p class="muted">Every generation, kept: the raw model replies before any check, what the checks made of them, and errors.</p><div class="scroll"><table><thead><tr><th>#</th><th>when</th><th>source</th><th>result</th><th>pack</th><th>model</th><th>calls</th><th>time</th><th>cost</th><th>size</th><th></th></tr></thead><tbody>' +
    d.genDebug.map((g) => "<tr><td>" + g.id + "</td><td>" + when(g.created_at) + "</td><td>" + esc(g.source || "") + "</td><td>" + (g.ok ? chip("done") : chip("failed") + ' <span class="mono">' + esc(String(g.error || "").slice(0, 80)) + "</span>") + "</td><td>" + esc(g.pack_id ?? "–") + "</td><td>" + esc(g.model || "") + " " + esc(g.prompt || "") + "</td><td>" + esc(g.calls ?? "") + "</td><td>" + Math.round((g.ms || 0) / 1000) + " s</td><td>$" + Number(g.usd || 0).toFixed(3) + "</td><td>" + Math.round(g.size / 1024) + " KB" + (g.in_r2 ? " (R2)" : "") + '</td><td><button data-raw="gen/' + g.id + '">Raw</button></td></tr>').join("") +
    '</tbody></table></div><pre class="mono" id="rawBody" style="white-space:pre-wrap;max-height:28rem;overflow:auto;margin:0" hidden></pre></section>' +
    '<div class="cols"><section class="panel"><h2>Diary</h2>' + (d.diary.length ? d.diary.map((e) => "<p><b>" + esc(e.day) + "</b> " + esc(e.entry) + "</p>").join("") : '<p class="muted">No entries yet: the diary is written each night from new stats.</p>') + "</section>" +
    '<section class="panel"><h2>Life stats</h2>' + (d.stats.length ? '<p class="muted">Latest of ' + d.stats.length + ", " + when(d.stats[0].uploaded_at) + '</p><pre class="mono" style="white-space:pre-wrap;margin:0">' + esc(JSON.stringify(d.stats[0].stats, null, 1)) + "</pre>" : '<p class="muted">None uploaded yet.</p>') + "</section></div>";
  const regen = async (body, btn) => { btn.disabled = true; btn.textContent = "Writing… (a minute or two)"; try { const r = await api("/creatures/" + id + "/regenerate", body); toast("Pack v" + r.version + " written" + (r.action ? " and queued for the badge." : ".")); route(); } catch (e) { toast(e.message); btn.disabled = false; } };
  $("#regen-edit").onclick = (e) => regen({ bible: $("#bible").value, push: $("#push").checked }, e.target);
  $("#regen-fresh").onclick = (e) => regen({ push: $("#push").checked }, e.target);
  wireRaw();
  for (const b of document.querySelectorAll("[data-lines]")) b.onclick = () => { const p = d.packs.find((x) => x.version === Number(b.dataset.lines)); $("#packLines").innerHTML = "<h3>Lines in v" + p.version + "</h3>" + linesList(p.lines); };
  for (const b of document.querySelectorAll("[data-pushpack]")) b.onclick = async () => { await api("/actions", { macs: [c.mac], kind: "line_pack", payload: { pack_id: Number(b.dataset.pushpack), version: Number(b.dataset.v), genome_hash: c.genome_hash } }); toast("Pack v" + b.dataset.v + " queued for the badge."); };
}

async function ota() {
  const d = await api("/ota");
  $("#main").innerHTML = '<section class="panel"><h2>New rollout</h2><p class="muted">The badge fetches the release through the existing signed manifest flow (web/public/fw/). Target every badge or a list of MACs.</p><div class="row"><input id="release" placeholder="Release, e.g. lcd128/1.5.0"><input id="target" placeholder="all, or MACs separated by commas" value="all"><button class="primary" id="go">Start rollout</button></div></section>' +
    '<section class="panel"><h2>Rollouts</h2><div class="scroll"><table><thead><tr><th>#</th><th>release</th><th>target</th><th>state</th><th>created</th><th></th></tr></thead><tbody>' +
    d.rollouts.map((r) => "<tr><td>" + r.id + "</td><td>" + esc(r.release) + '</td><td class="mono">' + esc(Array.isArray(r.target) ? r.target.join(", ") : r.target) + "</td><td>" + chip(r.state) + "</td><td>" + when(r.created_at) + "</td><td>" + (r.state === "killed" ? "" : (r.state === "active" ? '<button data-st="paused" data-id="' + r.id + '">Pause</button>' : '<button data-st="active" data-id="' + r.id + '">Resume</button>') + ' <button class="danger" data-st="killed" data-id="' + r.id + '">Kill</button>') + "</td></tr>").join("") + "</tbody></table></div></section>";
  $("#go").onclick = async () => { const t = $("#target").value.trim(); try { const r = await api("/ota", { release: $("#release").value, target: t === "all" ? "all" : t.split(/[\s,]+/).filter(Boolean) }); toast("Rollout #" + r.rollout_id + " queued for " + r.ids.length + " badge(s)."); route(); } catch (e) { toast(e.message); } };
  for (const b of document.querySelectorAll("[data-st]")) b.onclick = async () => { await api("/ota/" + b.dataset.id, { state: b.dataset.st }); toast("Rollout " + b.dataset.st + "."); route(); };
}

function chart(rows) {
  if (!rows.length) return '<p class="muted">No data in this window.</p>';
  const W = 640, H = 180, L = 44, B = 24, vals = rows.map((r) => r.sum), max = Math.max(...vals, 1);
  const x = (i) => L + (rows.length === 1 ? (W - L) / 2 : (i * (W - L - 10)) / (rows.length - 1)), y = (v) => H - B - (v / max) * (H - B - 10);
  const ticks = [0, max / 2, max];
  return '<div class="scroll"><svg viewBox="0 0 ' + W + " " + H + '" width="100%" style="max-width:' + W + 'px">' + ticks.map((t) => '<line class="grid" x1="' + L + '" x2="' + W + '" y1="' + y(t) + '" y2="' + y(t) + '"/><text x="' + (L - 6) + '" y="' + (y(t) + 4) + '" text-anchor="end">' + +t.toFixed(1) + "</text>").join("") +
    '<polyline class="line" points="' + rows.map((r, i) => x(i) + "," + y(r.sum)).join(" ") + '"/>' + rows.map((r, i) => '<circle cx="' + x(i) + '" cy="' + y(r.sum) + '" r="3" fill="var(--accent)"><title>' + esc(r.day) + ": " + r.sum + "</title></circle>").join("") +
    [0, rows.length - 1].filter((v, i, a) => a.indexOf(v) === i).map((i) => '<text x="' + x(i) + '" y="' + (H - 6) + '" text-anchor="middle">' + esc(rows[i].day) + "</text>").join("") + "</svg></div>";
}

async function analytics(metric) {
  const list = await api("/analytics");
  let body = "";
  if (metric) {
    const d = await api("/analytics?metric=" + encodeURIComponent(metric) + "&days=60");
    const byDay = {};
    for (const r of d.perDay) { const k = r.day; (byDay[k] ||= { day: k, sum: 0 }).sum += r.sum; }
    body = '<section class="panel"><h2>' + esc(metric) + ' <span class="muted">· last 60 days, summed over badges</span></h2>' + chart(Object.values(byDay)) +
      '<div class="scroll"><table><thead><tr><th>badge</th><th>bucket</th><th>sum</th><th>mean</th><th>uploads</th><th>last</th></tr></thead><tbody>' + d.perBadge.map((r) => '<tr><td class="mono">' + esc(r.mac) + "</td><td>" + esc(r.dims_json ? JSON.parse(r.dims_json).bucket ?? r.dims_json : "") + "</td><td>" + r.sum + "</td><td>" + Number(r.avg).toFixed(2) + "</td><td>" + r.n + "</td><td>" + when(r.last) + "</td></tr>").join("") + "</tbody></table></div></section>";
  }
  $("#main").innerHTML = '<section class="panel"><h2>Metrics</h2>' + (list.metrics.length ? '<div class="row">' + list.metrics.map((m) => '<a class="btn" href="#analytics/' + encodeURIComponent(m.metric) + '">' + esc(m.metric) + ' <span class="muted">' + m.badges + "</span></a>").join("") + "</div>" : '<p class="muted">No analytics uploaded yet. Badges send them with their life stats.</p>') + "</section>" + body;
}

async function route() {
  if (!token) return login();
  const [view, arg] = decodeURIComponent(location.hash.slice(1)).split(/\/(.*)/s);
  nav(["badge", "creature"].includes(view) ? "overview" : view || "overview");
  try {
    if (view === "badge") await badge(arg);
    else if (view === "creature") await creature(arg);
    else if (view === "health") await health();
    else if (view === "ota") await ota();
    else if (view === "analytics") await analytics(arg);
    else await overview();
  } catch (e) { if (token) $("#main").innerHTML = '<section class="panel"><h2>That did not load</h2><p>' + esc(e.message) + "</p></section>"; }
}
addEventListener("hashchange", route);
route();
</script></body></html>`;
