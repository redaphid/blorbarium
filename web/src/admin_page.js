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
const VIEWS = [["overview", "Badges"], ["ota", "OTA"], ["analytics", "Analytics"]];
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
    '<section class="panel"><h2>Creatures, by lineage</h2>' + Object.entries(byLineage).map(([l, cs]) => "<h3>" + esc(l) + '</h3><div class="scroll"><table><thead><tr><th>pet</th><th>gen</th><th>stage</th><th>status</th><th>pack</th><th>first seen</th></tr></thead><tbody>' +
      cs.map((c) => '<tr class="link" data-c="' + c.id + '"><td>' + esc(c.name || "") + " " + esc(c.species) + "</td><td>" + esc(c.generation ?? "?") + "</td><td>" + esc(c.stage || "") + "</td><td>" + chip(c.status) + "</td><td>" + (c.pack_version ? "v" + c.pack_version : "–") + "</td><td>" + when(c.first_seen) + "</td></tr>").join("") + "</tbody></table></div>").join("") + "</section>" +
    '<section class="panel"><h2>Actions</h2>' + actionsTable(d.actions) + "</section>" + pushPanel("one", mac) +
    '<section class="panel"><h2>Analytics uploads</h2>' + (d.uploads.length ? '<table><tbody>' + d.uploads.map((u) => "<tr><td>#" + u.id + "</td><td>" + esc(u.fw || "") + "</td><td>" + when(u.uploaded_at) + "</td></tr>").join("") + "</tbody></table>" : '<p class="muted">None yet.</p>') + "</section>";
  for (const tr of document.querySelectorAll("tr[data-c]")) tr.onclick = () => (location.hash = "creature/" + tr.dataset.c);
  $("#save-label").onclick = async () => { await api("/badges/" + mac, { label: $("#label").value }); toast("Saved."); };
  wireCancel(); wirePush("one", mac);
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
    '<section class="panel"><h2>Line packs</h2><div class="scroll"><table><thead><tr><th>version</th><th>status</th><th>badge has it</th><th>model</th><th>retries</th><th>fell back</th><th>cost</th><th>created</th><th></th></tr></thead><tbody>' +
    d.packs.map((p) => "<tr><td>v" + p.version + "</td><td>" + chip(p.status) + "</td><td>" + (p.pending_device ? chip("pending") : when(p.delivered_at)) + "</td><td>" + esc(p.provenance.model) + " " + esc(p.provenance.prompt) + "</td><td>" + esc(p.provenance.retries) + "</td><td>" + esc(p.fallback.length) + "</td><td>$" + Number(p.provenance.meter?.usd || 0).toFixed(3) + "</td><td>" + when(p.created_at) + '</td><td><button data-pushpack="' + p.id + '" data-v="' + p.version + '">Push</button></td></tr>').join("") + "</tbody></table></div>" +
    (d.packs[0] ? "<details><summary>Lines in v" + d.packs[0].version + " (" + Object.keys(d.packs[0].lines).length + " situations)</summary>" + linesList(d.packs[0].lines) + "</details>" : "") + "</section>" +
    '<div class="cols"><section class="panel"><h2>Diary</h2>' + (d.diary.length ? d.diary.map((e) => "<p><b>" + esc(e.day) + "</b> " + esc(e.entry) + "</p>").join("") : '<p class="muted">No entries yet: the diary is written each night from new stats.</p>') + "</section>" +
    '<section class="panel"><h2>Life stats</h2>' + (d.stats.length ? '<p class="muted">Latest of ' + d.stats.length + ", " + when(d.stats[0].uploaded_at) + '</p><pre class="mono" style="white-space:pre-wrap;margin:0">' + esc(JSON.stringify(d.stats[0].stats, null, 1)) + "</pre>" : '<p class="muted">None uploaded yet.</p>') + "</section></div>";
  const regen = async (body, btn) => { btn.disabled = true; btn.textContent = "Writing… (a minute or two)"; try { const r = await api("/creatures/" + id + "/regenerate", body); toast("Pack v" + r.version + " written" + (r.action ? " and queued for the badge." : ".")); route(); } catch (e) { toast(e.message); btn.disabled = false; } };
  $("#regen-edit").onclick = (e) => regen({ bible: $("#bible").value, push: $("#push").checked }, e.target);
  $("#regen-fresh").onclick = (e) => regen({ push: $("#push").checked }, e.target);
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
    else if (view === "ota") await ota();
    else if (view === "analytics") await analytics(arg);
    else await overview();
  } catch (e) { if (token) $("#main").innerHTML = '<section class="panel"><h2>That did not load</h2><p>' + esc(e.message) + "</p></section>"; }
}
addEventListener("hashchange", route);
route();
</script></body></html>`;
