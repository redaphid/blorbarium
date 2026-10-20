// Wi-Fi: Improv provisioning, the windows when the station is up, and the
// unattended update over HTTPS (docs/wifi-ota.md). The decisions are pure and
// tested natively (lib/wifiota, test/test_wifi_ota); the update itself is
// the same ota::Session, signature check, trial and rollback as Bluetooth's
// (lib/ota). main.cpp includes this once, after the loop's statics it uses
// (dish, ble, updater, trial, trialPort, powerPolicy, espNow, shot,
// powerHeld), so it is part of that one translation unit. Every line it logs
// starts "[wifi] ".
#pragma once

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/platform.h>

#include <cstdarg>
#include <cstring>
#include <optional>
#include <tuple>

#include "hw/battery.h"
#include "hw/improv_ble.h"
#include "hw/wifi_station.h"
#include "hw/wifi_store.h"
#include "paint/text.h"
#include "wifiota/improv.h"
#include "wifiota/manifest.h"
#include "wifiota/networks.h"
#include "wifiota/power_sense.h"
#include "wifiota/schedule.h"

namespace wififw {

namespace improv = wifiota::improv;
using wifiota::Window;

constexpr uint32_t kScanMs = 10000;       // a scan that has not finished by then has failed
constexpr uint32_t kJoinMs = 15000;       // one network's join
constexpr uint32_t kConfirmRetryMs = 20000;
constexpr uint32_t kIdentifyMs = 8000;
constexpr const char* kSiteUrl = "https://blorb.hypnodroid.com/?d=";

enum class Phase : uint8_t { Off, Scanning, Joining, Manifest, Image, Closing };
const char* phaseName(Phase p) {
  constexpr const char* kNames[] = {"off", "scanning", "joining", "manifest", "image", "closing"};
  return kNames[uint8_t(p)];
}

static hw::WifiStation station;
static hw::HttpsFetch fetch;
static hw::PacketRing ring;   // the image's packets, task to loop; allocated at the first download
static bool ringReady = false;
static hw::Battery battery;
static wifiota::PowerSense powerSense;
static wifiota::Schedule schedule;
static wifiota::Networks networks;
static bool autoOn = true;
static std::optional<bool> powerOverride;   // `DEBUG wifi power on|off`, RAM only

static Window window = Window::None;
static Phase phase = Phase::Off;
static uint32_t phaseAt = 0, powerReadAt = 0;
static uint8_t order[wifiota::Networks::kMax];
static size_t tries = 0, tried = 0;
static bool scanOnly = false;               // an Improv scan: up, scan, down
static char lastOutcome[96] = "none";

// What is asked of the next window.
static bool forced = false, scanAsked = false;
static char forcedUrl[160] = "";
static std::optional<improv::Credentials> provisionAsked, provisioning;
// On trial after an update over Wi-Fi: the token the old image handed over.
static std::optional<ota::Token> confirmToken;
static uint32_t confirmAt = 0;
static uint32_t identifyUntil = 0;

static hw::ImprovBle improvBle(ble, improv::DeviceInfo{"blorbarium", blorb::kFirmwareVersion, "ESP32-S3", ble.name()});

static void say(const char* fmt, ...) {
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  Serial.printf("[wifi] %s\n", line);
}

static void outcome(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(lastOutcome, sizeof lastOutcome, fmt, ap);
  va_end(ap);
  say("%s: %s", wifiota::windowName(window), lastOutcome);
}

static bool onPower() { return powerOverride.value_or(powerSense.onPower()); }

static wifiota::Facts facts() {
  return wifiota::Facts{
      networks.size() > 0 && autoOn,
      onPower(),
      powerPolicy.low(),
      ble.connected() || updater.asking() || (updater.active() && !updater.ownedBy(kWifiPeer)) || debugAsks() ||
          bool(shot),
      dish->duelActive() || std::strcmp(dish->fightState(), "off") != 0 || espNow.isOpen(),
  };
}

static const char* manifestUrl() { return window == Window::Forced && forcedUrl[0] ? forcedUrl : wifiota::kManifestUrl; }

static void setPhase(Phase p) {
  phase = p;
  phaseAt = millis();
}

// The window ends: the fetch stops first, then the radio goes (finishClosing).
static void closeWindow() {
  fetch.cancel();
  station.leave();
  setPhase(Phase::Closing);
}

static void finishClosing() {
  if (fetch.status() == hw::HttpsFetch::Status::Running) return;
  station.down();
  if (ringReady) ring.drain([](const uint8_t*, size_t, ota::Peer) {});   // a cancelled download's leftovers
  if (updater.ownedBy(kWifiPeer) && !updater.ready()) updater.abort();
  if (window == Window::Provision) provisioning.reset();
  window = Window::None;
  scanOnly = false;
  setPhase(Phase::Off);
}

static void joinNext() {
  if (tried >= tries) {
    if (window == Window::Provision) improvBle.machine().couldNotJoin();
    else if (window == Window::Check) schedule.failed(millis());
    outcome("joined no network (tried %u)", unsigned(tries));
    return closeWindow();
  }
  const char* ssid = window == Window::Provision ? provisioning->ssid : networks[order[tried]].ssid;
  const char* pass = window == Window::Provision ? provisioning->password : networks[order[tried]].password;
  ++tried;
  say("joining \"%s\"", ssid);
  station.join(ssid, pass);
  setPhase(Phase::Joining);
}

static void openWindow(Window w) {
  window = w;
  if (!station.up()) {
    outcome("the radio would not come up (heap %u)", unsigned(ESP.getFreeHeap()));
    if (w == Window::Provision) improvBle.machine().couldNotJoin();
    if (w == Window::Check) schedule.failed(millis());
    window = Window::None;
    return;
  }
  say("window %s opens (power=%s idle=%d heap=%u)", wifiota::windowName(w), onPower() ? "on" : "battery",
      int(powerPolicy.low()), unsigned(ESP.getFreeHeap()));
  tried = 0;
  if (w == Window::Provision) {
    tries = 1;
    return joinNext();   // the network the phone just named: no scan first
  }
  if (!station.scanStart()) {
    if (scanOnly) {
      improvBle.machine().scanned(nullptr, 0);
      outcome("the scan would not start");
      return closeWindow();
    }
    tries = networks.order(nullptr, 0, order);   // try them all, unheard
    return joinNext();
  }
  setPhase(Phase::Scanning);
}

static void scanned() {
  const int16_t n = station.scanDone();
  constexpr size_t kMaxHeard = 20;
  hw::WifiStation::Heard heard[kMaxHeard];
  const size_t count = n > 0 ? std::min<size_t>(size_t(n), kMaxHeard) : 0;
  for (size_t i = 0; i < count; ++i) heard[i] = station.heard(int16_t(i));
  station.scanForget();
  say("scan heard %d network(s)", int(n));
  if (scanOnly) {
    improv::Network nets[kMaxHeard];
    for (size_t i = 0; i < count; ++i) {
      std::memcpy(nets[i].ssid, heard[i].ssid, sizeof nets[i].ssid);
      nets[i].rssi = heard[i].rssi;
      nets[i].secured = heard[i].secured;
    }
    improvBle.machine().scanned(nets, count);
    outcome("scanned for a phone: %u network(s)", unsigned(count));
    return closeWindow();
  }
  wifiota::Networks::Heard known[kMaxHeard];
  for (size_t i = 0; i < count; ++i) known[i] = {heard[i].ssid, heard[i].rssi};
  tries = networks.order(known, count, order);
  joinNext();
}

static void joined() {
  say("joined \"%s\" rssi=%d ch=%u ip=%s heap=%u", WiFi.SSID().c_str(), int(WiFi.RSSI()), unsigned(WiFi.channel()),
      WiFi.localIP().toString().c_str(), unsigned(ESP.getFreeHeap()));
  if (window == Window::Provision) {
    networks.remember(provisioning->ssid, provisioning->password);
    const bool saved = hw::WifiStore::save(networks);
    char url[64];
    std::snprintf(url, sizeof url, "%s%s", kSiteUrl, ble.name());
    improvBle.machine().joined(url);
    outcome("provisioned \"%s\" (%s, %u remembered)", provisioning->ssid, saved ? "saved" : "SAVE FAILED",
            unsigned(networks.size()));
    return closeWindow();
  }
  if (!fetch.getText(manifestUrl())) {
    if (window == Window::Check) schedule.failed(millis());
    outcome("manifest fetch did not start: %s", fetch.error());
    return closeWindow();
  }
  say("fetching %s", manifestUrl());
  setPhase(Phase::Manifest);
}

static void readManifest() {
  if (fetch.status() == hw::HttpsFetch::Status::Running) return;
  if (fetch.status() == hw::HttpsFetch::Status::Failed) {
    if (window == Window::Check) schedule.failed(millis());
    outcome("manifest: %s (heap %u minheap %u)", fetch.error(), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()));
    return closeWindow();
  }
  const std::optional<wifiota::Manifest> m = wifiota::parseManifest(fetch.text());
  if (!m) {
    if (window == Window::Check) schedule.failed(millis());
    outcome("manifest unreadable (%u bytes)", unsigned(fetch.bytes()));
    return closeWindow();
  }
  const ota::Version& v = m->image.version;
  if (window == Window::Confirm) {
    // The trial's half the badge cannot check alone: the new image reached the site over Wi-Fi.
    const ota::Confirm c = trial.confirm(*confirmToken);
    confirmToken.reset();
    outcome("trial confirmed over Wi-Fi (%s; the site has %u.%u.%u)", c == ota::Confirm::Confirmed ? "ok" : "not on trial",
            unsigned(v.major), unsigned(v.minor), unsigned(v.patch));
    return closeWindow();
  }
  schedule.checked(millis());
  const wifiota::Offer offer = wifiota::judge(*m, BOARD_NAME, *ota::parseVersion(blorb::kFirmwareVersion), trialPort.rolledBack());
  say("manifest %s %u.%u.%u size=%u fmt=%u: %s", m->image.board, unsigned(v.major), unsigned(v.minor),
      unsigned(v.patch), unsigned(m->image.size), unsigned(m->image.fmt), wifiota::offerName(offer));
  if (offer != wifiota::Offer::Install) {
    outcome("%s, nothing to install (running %s)", wifiota::offerName(offer), blorb::kFirmwareVersion);
    return closeWindow();
  }
  uint8_t random[12];
  esp_fill_random(random, sizeof random);
  const ota::Token token = wifiota::wifiToken(random);
  if (const std::optional<ota::Refusal> no = updater.install(m->image, token, millis(), kWifiPeer)) {
    outcome("install refused: %s", no->why);
    return closeWindow();
  }
  char url[200];
  if (!wifiota::imageUrl(manifestUrl(), m->file, url, sizeof url)) {
    updater.abort();
    outcome("image url too long");
    return closeWindow();
  }
  if (!ringReady) ring.begin(), ringReady = true;
  if (!fetch.getImage(url, m->image.size, ring, kWifiPeer)) {
    updater.abort();
    outcome("image fetch did not start: %s", fetch.error());
    return closeWindow();
  }
  say("downloading %s (signature ok, pet saved, slot open) heap=%u", url, unsigned(ESP.getFreeHeap()));
  setPhase(Phase::Image);
}

static void download() {
  static uint32_t saidAt = 0;
  ring.drain([](const uint8_t* p, size_t n, ota::Peer peer) { updater.data(p, n, millis(), peer); });
  if (updater.ready()) {
    if (fetch.status() == hw::HttpsFetch::Status::Running) return;   // its last bytes are past the hash: let it end
    outcome("downloaded and verified in %u s; restarting into it once the badge is quiet",
            unsigned((millis() - phaseAt) / 1000));
    return closeWindow();
  }
  if (!updater.ownedBy(kWifiPeer)) {   // the session ended: a bad hash, a bad image, a flash error
    outcome("update failed: %s after %u bytes", ota::lastName(updater.last()), unsigned(fetch.bytes()));
    return closeWindow();
  }
  if (fetch.status() == hw::HttpsFetch::Status::Failed) {
    updater.abort();
    if (window == Window::Check) schedule.failed(millis());
    outcome("download: %s", fetch.error());
    return closeWindow();
  }
  if (millis() - saidAt >= 5000) {
    saidAt = millis();
    say("download at %u bytes heap=%u minheap=%u", unsigned(fetch.bytes()), unsigned(ESP.getFreeHeap()),
        unsigned(ESP.getMinFreeHeap()));
  }
}

static void readPower(uint32_t now) {
  const bool first = !powerReadAt;
  if (!first && now - powerReadAt < wifiota::PowerSense::kEveryMs) return;
  powerReadAt = now | 1;
  const bool was = powerSense.onPower();
  powerSense.reading(battery.cellMv(), now);
  if (first || powerSense.onPower() != was)
    say("power %s (vbat=%u mV pin=%u mV, %s)%s", powerSense.onPower() ? "on" : "battery", unsigned(powerSense.lastMv()),
        unsigned(battery.pinMv()), wifiota::powerWhyName(powerSense.why()), powerOverride ? " [overridden]" : "");
}

// Every loop.
static void poll(uint32_t now) {
  improvBle.poll();
  readPower(now);
  const wifiota::Facts f = facts();
  if (window != Window::None && phase != Phase::Closing && !wifiota::Schedule::keep(window, f)) {
    outcome("closed: %s", f.radioBusy ? "the radio is wanted for a duel" : "someone is using the badge");
    if (window == Window::Provision) improvBle.machine().couldNotJoin();
    if (window == Window::Check) schedule.failed(millis());
    closeWindow();
  }
  switch (phase) {
    case Phase::Off: break;
    case Phase::Scanning:
      if (station.scanDone() != WIFI_SCAN_RUNNING || millis() - phaseAt > kScanMs) scanned();
      return;
    case Phase::Joining:
      if (station.joined()) joined();
      else if (millis() - phaseAt > kJoinMs) station.leave(), joinNext();
      return;
    case Phase::Manifest: return readManifest();
    case Phase::Image: return download();
    case Phase::Closing: return finishClosing();
  }
  // Off: what to open, the person waiting first.
  if (provisionAsked) {
    if (f.radioBusy) {
      provisionAsked.reset();
      improvBle.machine().couldNotJoin();
      return say("provision refused: the radio is in a duel");
    }
    provisioning = provisionAsked;
    provisionAsked.reset();
    return openWindow(Window::Provision);
  }
  if (scanAsked && !f.radioBusy) {
    scanAsked = false;
    scanOnly = true;
    return openWindow(Window::Forced);   // a scan, no join: closed in scanned()
  }
  if (forced && !f.radioBusy) {
    forced = false;
    if (!networks.size()) return say("ota check: no network remembered (DEBUG wifi add <ssid> <password>)");
    return openWindow(Window::Forced);
  }
  if (confirmToken && trial.onTrial() && networks.size() && int32_t(now - confirmAt) >= 0) {
    confirmAt = now + kConfirmRetryMs;   // the next try, should this one fail
    return openWindow(Window::Confirm);
  }
  if (schedule.checkHold(f, now) == wifiota::Hold::Open) openWindow(Window::Check);
}

// A duel wants the radio: any window but the trial's gives it up now
// (RadioGate::open, main.cpp). True while the radio is still Wi-Fi's.
static bool yieldRadio() {
  if (window != Window::None && window != Window::Confirm && phase != Phase::Closing) {
    outcome("closed: the radio is wanted for a duel");
    if (window == Window::Provision) improvBle.machine().couldNotJoin();
    closeWindow();
  }
  if (phase == Phase::Closing) finishClosing();
  return station.isUp();
}

// An update fetched over Wi-Fi restarts only when nobody is using the badge:
// dark (low power) and no phone. A page's update restarts as it always has.
static bool mayRestart() { return !updater.ownedBy(kWifiPeer) || (powerPolicy.low() && !ble.connected()); }
// An update over Wi-Fi that is downloaded and switched holds the radio no longer.
static bool updateHoldsRadio() { return updater.active() && !(updater.ownedBy(kWifiPeer) && updater.ready()); }

static bool identifying() { return int32_t(identifyUntil - millis()) > 0; }

// Improv's identify: the screen wakes and says which badge this is.
static void draw(paint::Canvas240& cv) {
  if (!identifying()) return;
  constexpr uint16_t kWork = 0x471D, kInk = 0xFFFF;   // ota/screen.cpp's teal and white
  constexpr paint::Clip kAll{0, 0, 240, 240, 0};
  for (int y = 82; y < 158; ++y)
    for (int x = 0; x < 240; ++x) cv.px[y * 240 + x] = paint::blend(cv.px[y * 240 + x], 0, 200);
  for (const auto& [text, y, colour] : {std::tuple{"It's me!", 106, kWork}, std::tuple{ble.name(), 134, kInk}})
    paint::drawString(cv, paint::kFreeSans9pt7b, text, 120 - paint::textWidth(paint::kFreeSans9pt7b, text) / 2, y,
                      colour, kAll);
}

// The framework allocates mbedtls from internal RAM only, and a TLS session
// takes two 16 KB record buffers and a handshake's worth more: about 45 KB of
// a heap that has under 100 KB free with Bluetooth up. Blocks of kTlsPsramFrom
// bytes or more go to PSRAM instead (the AES and SHA engines bounce PSRAM
// buffers through internal RAM themselves); small ones, the bignums of a
// signature check among them, stay internal. If TLS ever misbehaves on the
// board, raising this past any block (SIZE_MAX) puts everything back inside.
constexpr size_t kTlsPsramFrom = 1024;
static void* tlsCalloc(size_t n, size_t size) {
  if (n && size > SIZE_MAX / n) return nullptr;
  void* p = n * size >= kTlsPsramFrom ? heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
}
static void tlsFree(void* p) { heap_caps_free(p); }

// Before ble.begin(): the networks, the switch, and whether this boot is an
// update over Wi-Fi on trial, which must reach the site again to keep itself.
static void begin() {
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  battery.begin();
  networks = hw::WifiStore::load();
  autoOn = hw::WifiStore::autoOn();
  improvBle.onJoin = [](const improv::Credentials& c) {
    say("improv: a phone sent network \"%s\"", c.ssid);
    provisionAsked = c;
  };
  improvBle.onScan = [] {
    say("improv: a phone asked for a scan");
    scanAsked = true;
  };
  improvBle.onIdentify = [] {
    say("improv: identify");
    identifyUntil = millis() + kIdentifyMs;
    powerPolicy.wakeNow(millis() + warpMs);
  };
  if (trial.onTrial())
    if (const std::optional<ota::Handover> h = trialPort.handover(); h && wifiota::isWifiToken(h->token)) {
      confirmToken = h->token;
      powerPolicy.sleepNow();   // restarted at a quiet moment: it stays dark
      say("on trial after an update over Wi-Fi: confirming over Wi-Fi, screen dark");
    }
  say("%u network(s) remembered, automatic checks %s", unsigned(networks.size()), autoOn ? "on" : "OFF (DEBUG wifi on)");
}

// ---- the cable's verbs ---------------------------------------------------------------

static void statusLine() {
  const uint32_t now = millis();
  char nets[120] = "";
  for (size_t i = 0; i < networks.size(); ++i)
    std::snprintf(nets + std::strlen(nets), sizeof nets - std::strlen(nets), "%s\"%s\"", i ? "," : "", networks[i].ssid);
  say("window=%s phase=%s nets=%u [%s] auto=%d power=%s (%s, vbat=%u mV pin=%u mV%s) hold=%s next_check_in=%us "
      "retry=%us",
      wifiota::windowName(window), phaseName(phase), unsigned(networks.size()), nets, int(autoOn),
      onPower() ? "on" : "battery", wifiota::powerWhyName(powerSense.why()), unsigned(powerSense.lastMv()),
      unsigned(battery.pinMv()), powerOverride ? ", overridden" : "", wifiota::holdName(schedule.checkHold(facts(), now)),
      unsigned(schedule.dueIn(now) / 1000), unsigned(schedule.retryMs() / 1000));
  say("station=%s%s improv=%u/%u trial=%d confirm_pending=%d ota=%s owner=%s last=\"%s\" heap=%u minheap=%u", station.isUp() ? "up" : "off",
      station.joined() ? (" \"" + WiFi.SSID() + "\" rssi=" + String(WiFi.RSSI())).c_str() : "",
      unsigned(improvBle.machine().state()), unsigned(improvBle.machine().error()), int(trial.onTrial()),
      int(bool(confirmToken)), updater.active() ? (updater.ready() ? "ready" : "active") : "idle",
      updater.ownedBy(kWifiPeer) ? "wifi" : updater.active() ? "page" : "none", lastOutcome,
      unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()));
}

// `"My Net" pass` or `MyNet pass`: the SSID, quoted if it has spaces, then the rest as the password.
static bool parseAdd(const char* s, char (&ssid)[33], char (&pass)[65]) {
  while (*s == ' ') ++s;
  const char* end;
  if (*s == '"') {
    end = std::strchr(++s, '"');
    if (!end) return false;
  } else {
    end = s;
    while (*end && *end != ' ') ++end;
  }
  const size_t n = size_t(end - s);
  if (!n || n > 32) return false;
  std::memcpy(ssid, s, n);
  ssid[n] = 0;
  s = *end == '"' ? end + 1 : end;
  while (*s == ' ') ++s;
  if (std::strlen(s) > 64) return false;
  std::snprintf(pass, sizeof pass, "%s", s);
  return true;
}

// `DEBUG wifi [forget|add <ssid> [password]|on|off|power on|off|auto]`
static void wifiVerb(const char* line) {
  char what[12] = "", arg[12] = "";
  std::sscanf(line, "%*s %11s %11s", what, arg);
  if (!*what || !std::strcmp(what, "status")) return statusLine();
  if (!std::strcmp(what, "forget")) {
    networks.forgetAll();
    const bool ok = hw::WifiStore::forget();
    say("forgot every network (%s)", ok ? "erased" : "ERASE FAILED");
    return;
  }
  if (!std::strcmp(what, "add")) {
    char ssid[33], pass[65];
    if (!parseAdd(std::strstr(line, "add") + 3, ssid, pass)) return say("want: wifi add <ssid|\"ssid with spaces\"> [password]");
    networks.remember(ssid, pass);
    const bool ok = hw::WifiStore::save(networks);
    say("remembered \"%s\" (%s password, %s), %u network(s); not tried: DEBUG ota check tries it", ssid,
        pass[0] ? "with a" : "no", ok ? "saved" : "SAVE FAILED", unsigned(networks.size()));
    return;
  }
  if (!std::strcmp(what, "on") || !std::strcmp(what, "off")) {
    autoOn = !std::strcmp(what, "on");
    const bool ok = hw::WifiStore::setAuto(autoOn);
    say("automatic checks %s (%s)", autoOn ? "on" : "off", ok ? "saved" : "SAVE FAILED");
    return;
  }
  if (!std::strcmp(what, "power")) {
    if (!std::strcmp(arg, "on")) powerOverride = true;
    else if (!std::strcmp(arg, "off")) powerOverride = false;
    else if (!std::strcmp(arg, "auto")) powerOverride.reset();
    else return say("want: wifi power on|off|auto");
    say("power taken as %s%s", onPower() ? "on" : "battery", powerOverride ? " (overridden until reset)" : " (inferred)");
    return;
  }
  say("want: wifi [status|forget|add <ssid> [password]|on|off|power on|off|auto]");
}

// `DEBUG ota check [url]`: a check now, idle or not; the url (http or https) for a test manifest.
static void otaVerb(const char* line) {
  char what[12] = "", url[sizeof forcedUrl] = "";
  std::sscanf(line, "%*s %11s %159s", what, url);
  if (std::strcmp(what, "check") != 0) return say("want: ota check [manifest url]");
  if (*url && std::strncmp(url, "https://", 8) != 0 && std::strncmp(url, "http://", 7) != 0)
    return say("ota check: the url must start http:// or https://");
  std::snprintf(forcedUrl, sizeof forcedUrl, "%s", url);
  forced = true;
  say("ota check asked (%s)%s", *url ? url : wifiota::kManifestUrl,
      window != Window::None ? ": after the open window closes" : "");
}

}  // namespace wififw

static bool wifiIdentifying() { return wififw::identifying(); }
static void wifiVerb(const char* line) { wififw::wifiVerb(line); }
static void otaVerb(const char* line) { wififw::otaVerb(line); }
