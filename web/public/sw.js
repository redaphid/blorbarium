// Offline after one visit (docs/ota.md "Offline"): the page and its files, and
// the latest release, so a dish can be updated with no network at all.
"use strict";
importScripts("ota.js");

// Every file under web/public the page can ask for, except this one and fw/
// (a release is kept by what its manifest names). web/test/offline.test.mjs
// fails when web/public gains a file that is not listed here.
const SHELL = ["./", "index.html", "ota.js", "ota-mock.js", "grungo.png", "manifest.webmanifest"];
const BOARDS = ["lcd128"];
const SHELL_CACHE = "blorb-shell";
const FONT_CACHE = "blorb-fonts";
const FW_PREFIX = "blorb-fw-";
const PAGE_WAIT_MS = 4000;      // a network slower than this is a plane's: use what is kept
const MANIFEST_WAIT_MS = 5000;
const IMAGE_WAIT_MS = 120000;

const scope = () => self.registration.scope;
const manifestUrl = (board) => new URL(`fw/${board}/manifest.json`, scope()).href;

function timed(promise, ms) {
  return Promise.race([promise, new Promise((_, reject) => setTimeout(() => reject(new Error("timeout")), ms))]);
}

// A release lives in one cache, fw-<board>-<when kept>, as a pair: the image,
// then its manifest. The manifest goes in last, so a cache without one is a
// download that never finished and is never served. The newest whole pair wins.
async function keptRelease(board) {
  const stamp = (name) => Number(name.slice(name.lastIndexOf("-") + 1));
  const names = (await caches.keys()).filter((n) => n.startsWith(`${FW_PREFIX}${board}-`)).sort((a, b) => stamp(b) - stamp(a));
  for (const name of names) {
    const cache = await caches.open(name);
    const res = await cache.match(manifestUrl(board));
    if (res) return { name, cache, res, manifest: await res.clone().json() };
  }
  return null;
}

// Keep the site's release if it is not kept already: the image is checked
// against its manifest's size and SHA-256 before the pair is written, and the
// pair it replaces goes only once the new one is whole.
async function keepRelease(board) {
  const url = manifestUrl(board);
  const res = await timed(fetch(url, { cache: "no-cache" }), MANIFEST_WAIT_MS);
  if (!res.ok) throw new Error(`manifest ${res.status}`);
  const text = await res.text();
  const m = JSON.parse(text);
  const kept = await keptRelease(board);
  if (kept && JSON.stringify(kept.manifest) === JSON.stringify(m)) return;
  const imageUrl = new URL(m.image, url).href;
  const img = await timed(fetch(imageUrl, { cache: "no-cache" }), IMAGE_WAIT_MS);
  if (!img.ok) throw new Error(`image ${img.status}`);
  const bytes = new Uint8Array(await img.arrayBuffer());
  if (bytes.length !== m.size || await BlorbOta.sha256(bytes) !== m.sha256) throw new Error("image does not match its manifest");
  const name = `${FW_PREFIX}${board}-${Date.now()}`;
  const cache = await caches.open(name);
  await cache.put(imageUrl, new Response(bytes, { headers: { "content-type": "application/octet-stream" } }));
  await cache.put(url, new Response(text, { headers: { "content-type": "application/json" } }));
  for (const old of await caches.keys()) {
    if (old.startsWith(`${FW_PREFIX}${board}-`) && old !== name) await caches.delete(old);
  }
}

// One download per board at a time: two at once would each delete the other's pair.
const keeping = {};
function keep(board) {
  keeping[board] ||= keepRelease(board).catch((e) => console.warn(`release ${board}: ${e.message}`))
    .finally(() => { delete keeping[board]; });
  return keeping[board];
}
const keepAll = () => Promise.all(BOARDS.map(keep));

// The manifest the page gets is always the kept pair's, so the image it then
// asks for is the one beside it. Online, the site's release is kept first.
async function releaseManifest(request, board) {
  await keep(board);
  const kept = await keptRelease(board);
  return kept ? kept.res : fetch(request);
}

async function releaseFile(request, board) {
  const kept = await keptRelease(board);
  return (kept && await kept.cache.match(request, { ignoreSearch: true })) || fetch(request);
}

// The page's own files: the network's when it answers in time, else the kept copy.
// Every navigation is the one page, kept as the site's root: Cloudflare answers
// /index.html with a redirect to /, and a redirect cannot answer a navigation.
async function shellFile(event) {
  const url = new URL(event.request.url);
  const key = event.request.mode === "navigate" ? scope() : url.origin + url.pathname;
  const cache = await caches.open(SHELL_CACHE);
  try {
    const res = await timed(fetch(event.request), PAGE_WAIT_MS);
    if (res.ok && !res.redirected) event.waitUntil(cache.put(key, res.clone()));
    return res;
  } catch (e) {
    const kept = await cache.match(key);
    if (kept) return kept;
    throw e;
  }
}

async function font(request) {
  const cache = await caches.open(FONT_CACHE);
  const kept = await cache.match(request);
  if (kept) return kept;
  const res = await fetch(request);
  if (res.ok || res.type === "opaque") await cache.put(request, res.clone());
  return res;
}

self.addEventListener("install", (event) => {
  event.waitUntil((async () => {
    const cache = await caches.open(SHELL_CACHE);
    await cache.addAll(SHELL.map((path) => new Request(path, { cache: "reload" })));
    await keepAll();
    await self.skipWaiting();
  })());
});

self.addEventListener("activate", (event) => event.waitUntil(self.clients.claim()));

self.addEventListener("fetch", (event) => {
  const request = event.request;
  if (request.method !== "GET") return;
  const url = new URL(request.url);
  if (/^fonts\.(googleapis|gstatic)\.com$/.test(url.hostname)) return event.respondWith(font(request));
  if (!request.url.startsWith(scope())) return;
  const path = url.href.slice(scope().length).split(/[?#]/)[0];
  const fw = /^fw\/([^/]+)\/(.+)$/.exec(path);
  if (fw && fw[2] === "manifest.json") return event.respondWith(releaseManifest(request, fw[1]));
  if (fw) return event.respondWith(releaseFile(request, fw[1]));
  if (request.mode === "navigate") event.waitUntil(keepAll());
  event.respondWith(shellFile(event));
});
