// Wi-Fi: Improv provisioning, the windows when the station is up, and the
// unattended update over HTTPS (docs/wifi-ota.md). The decisions are pure and
// tested natively (lib/wifiota, test/test_wifi_ota); the update itself is
// the same ota::Session, signature check, trial and rollback as Bluetooth's
// (lib/ota). main.cpp includes this once, after the loop's statics it uses
// (dish, ble, updater, trial, trialPort, trialWatchdog, powerPolicy, espNow,
// shot, debugAsks), so it is part of that one translation unit.
//
// Everything runs on the loop: no task, no timer. Every line it logs starts
// "[wifi] ", and every change of window, phase, power or hold is one line.
//
//   off --window opens--> joining --joined--> (manifest, read at once) --install--> image --ready--> off
//                                 --15 s----> off                      --nothing--> off
#pragma once

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/platform.h>

#include <cstdarg>
#include <cstring>
#include <optional>

#include "hw/battery.h"
#include "hw/improv_ble.h"
#include "hw/wifi_station.h"
#include "hw/wifi_store.h"
#include "wifiota/improv.h"
#include "wifiota/manifest.h"
#include "wifiota/power_sense.h"
#include "wifiota/remembered.h"
#include "wifiota/schedule.h"

namespace wififw {

namespace improv = wifiota::improv;
using wifiota::Window;

constexpr uint32_t kJoinMs = 15000;            // the join, then the window closes
constexpr uint32_t kStallMs = 15000;           // a download that hears nothing for this long has failed
constexpr uint32_t kConfirmRetryMs = 20000;    // on trial: the next try to reach the site
constexpr size_t kReadPerPassBytes = 16384;    // the image bytes one loop pass takes, so the loop keeps turning
constexpr const char* kSiteUrl = "https://blorb.hypnodroid.com/?d=";

enum class Phase : uint8_t { Off, Joining, Image };
const char* phaseName(Phase p) {
  constexpr const char* kNames[] = {"off", "joining", "image"};
  return kNames[uint8_t(p)];
}

static hw::WifiStation station;
static hw::HttpsGet get;
static hw::Battery battery;
static wifiota::PowerSense powerSense;
static wifiota::Schedule schedule;
static std::optional<improv::Credentials> network;   // the one remembered
static bool autoOn = true;                           // `DEBUG wifi on|off`, in NVS
static std::optional<bool> powerOverride;            // `DEBUG wifi power on|off`, RAM only

static Window window = Window::None;
static Phase phase = Phase::Off;
static wifiota::Hold lastHold = wifiota::Hold::Open;
static uint32_t phaseAt = 0, powerReadAt = 0;
static uint32_t imageAt = 0, heardAt = 0, saidAt = 0;   // the download: bytes handed to the session, when
static char lastOutcome[96] = "none";

// What is asked of the next window.
static bool forced = false;
static char forcedUrl[120] = "";
static std::optional<improv::Credentials> provisionAsked, provisioning;
// On trial after an update over Wi-Fi: the token the old image handed over.
static std::optional<ota::Token> confirmToken;
static uint32_t confirmAt = 0;

static hw::ImprovBle improvBle(ble, improv::DeviceInfo{"blorbarium", blorb::kFirmwareVersion, "ESP32-S3", ble.name()});

static void say(const char* fmt, ...) {
  char line[200];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  Serial.printf("[wifi] %s\n", line);
}

// How the window ended, kept for `DEBUG wifi`.
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
      network.has_value() && autoOn,
      onPower(),
      powerPolicy.low(),
      ble.connected() || updater.asking() || (updater.active() && !updater.ownedBy(kWifiPeer)) || debugAsks() ||
          bool(shot),
      dish->duelActive() || std::strcmp(dish->fightState(), "off") != 0 || espNow.isOpen(),
  };
}

static const char* manifestUrl() { return window == Window::Forced && forcedUrl[0] ? forcedUrl : wifiota::kManifestUrl; }

static void setPhase(Phase p) {
  say("%s: %s -> %s", wifiota::windowName(window), phaseName(phase), phaseName(p));
  phase = p;
  phaseAt = millis();
}

// The window ends: the fetch, the radio and an unfinished download all go at once.
static void closeWindow() {
  get.close();
  station.down();
  if (updater.ownedBy(kWifiPeer) && !updater.ready()) updater.abort();
  provisioning.reset();
  say("window %s closes (heap=%u minheap=%u)", wifiota::windowName(window), unsigned(ESP.getFreeHeap()),
      unsigned(ESP.getMinFreeHeap()));
  if (phase != Phase::Off) setPhase(Phase::Off);
  window = Window::None;
}

// A Check window that failed tries again later; a Provision window tells the phone.
static void failWindow(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(lastOutcome, sizeof lastOutcome, fmt, ap);
  va_end(ap);
  say("%s: FAILED %s", wifiota::windowName(window), lastOutcome);
  if (window == Window::Check) schedule.failed(millis());
  if (window == Window::Provision) improvBle.machine().couldNotJoin();
  closeWindow();
}

static void openWindow(Window w) {
  window = w;
  const improv::Credentials& net = w == Window::Provision ? *provisioning : *network;
  say("window %s opens: power=%s idle=%d network=\"%s\" heap=%u", wifiota::windowName(w), onPower() ? "on" : "battery",
      int(powerPolicy.low()), net.ssid, unsigned(ESP.getFreeHeap()));
  if (!station.up()) return failWindow("the radio would not come up (heap %u)", unsigned(ESP.getFreeHeap()));
  station.join(net.ssid, net.password);
  setPhase(Phase::Joining);
}

// The GET's blocking part, with the loop's numbers beside it.
static bool openGet(const char* url) {
  trialWatchdog.beat(millis());   // on trial, the stall below must not read as a hung image
  const uint32_t t0 = millis();
  const bool ok = get.open(url);
  say("GET %s: %s in %u ms (heap=%u minheap=%u big=%u stack_free=%u)", url, ok ? "200" : get.error(),
      unsigned(millis() - t0), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  trialWatchdog.beat(millis());
  return ok;
}

static void startImage(const wifiota::Manifest& m) {
  uint8_t random[12];
  esp_fill_random(random, sizeof random);
  if (const std::optional<ota::Refusal> no = updater.install(m.image, wifiota::wifiToken(random), millis(), kWifiPeer))
    return failWindow("install refused: %s", no->why);
  char url[200];
  if (!wifiota::imageUrl(manifestUrl(), m.file, url, sizeof url)) return failWindow("image url too long");
  if (!openGet(url)) return failWindow("image: %s", get.error());
  if (get.size() != int(m.image.size))
    return failWindow("image: the server says %d bytes, the manifest %u", get.size(), unsigned(m.image.size));
  say("downloading %u bytes (signature ok, pet saved, slot open)", unsigned(m.image.size));
  imageAt = 0;
  heardAt = saidAt = millis();
  setPhase(Phase::Image);
}

static void readManifest() {
  if (!openGet(manifestUrl())) return failWindow("manifest: %s", get.error());
  if (get.size() > int(wifiota::kManifestMax)) return failWindow("manifest: %d bytes is not a manifest", get.size());
  const String text = get.text();
  get.close();
  const std::optional<wifiota::Manifest> m = wifiota::parseManifest(std::string_view(text.c_str(), text.length()));
  if (!m) return failWindow("manifest unreadable (%u bytes)", unsigned(text.length()));
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
  const wifiota::Offer offer = wifiota::judge(*m, BOARD_NAME, *ota::parseVersion(blorb::kFirmwareVersion),
                                              blorb::kFormatVersion, trialPort.rolledBack());
  say("manifest %s %u.%u.%u size=%u fmt=%u auto=%d: %s (running %s fmt=%u)", m->image.board, unsigned(v.major),
      unsigned(v.minor), unsigned(v.patch), unsigned(m->image.size), unsigned(m->image.fmt), int(m->autoInstall),
      wifiota::offerName(offer), blorb::kFirmwareVersion, unsigned(blorb::kFormatVersion));
  if (offer != wifiota::Offer::Install) {
    outcome("%s, nothing to install", wifiota::offerName(offer));
    return closeWindow();
  }
  startImage(*m);
}

static void joined() {
  say("joined \"%s\" rssi=%d ch=%u ip=%s heap=%u", WiFi.SSID().c_str(), int(WiFi.RSSI()), unsigned(WiFi.channel()),
      WiFi.localIP().toString().c_str(), unsigned(ESP.getFreeHeap()));
  if (window != Window::Provision) return readManifest();
  network = provisioning;
  const bool saved = hw::WifiStore::save(*network);
  char url[64];
  std::snprintf(url, sizeof url, "%s%s", kSiteUrl, ble.name());
  improvBle.machine().joined(url);
  outcome("provisioned \"%s\" (%s)", network->ssid, saved ? "saved" : "SAVE FAILED");
  closeWindow();
}

// One pass of the download: what has arrived, up to kReadPerPassBytes, into
// ota::Session::data() as offset-prefixed packets, the path a page's writes take.
static void download() {
  uint8_t packet[4 + 1024];
  for (size_t taken = 0; taken < kReadPerPassBytes && !updater.ready();) {
    const int n = get.read(packet + 4, sizeof packet - 4);
    if (n < 0) return failWindow("the connection closed at %u bytes", unsigned(imageAt));
    if (n == 0) break;
    packet[0] = uint8_t(imageAt), packet[1] = uint8_t(imageAt >> 8), packet[2] = uint8_t(imageAt >> 16),
    packet[3] = uint8_t(imageAt >> 24);
    updater.data(packet, 4 + size_t(n), millis(), kWifiPeer);
    if (!updater.ownedBy(kWifiPeer))   // the session ended: a bad hash, a bad image, a flash error
      return failWindow("update failed at %u bytes: %s", unsigned(imageAt), ota::lastName(updater.last()));
    imageAt += uint32_t(n);
    taken += size_t(n);
    heardAt = millis();
  }
  if (updater.ready()) {
    outcome("downloaded and verified in %u s; restarting into it once the badge is quiet",
            unsigned((millis() - phaseAt) / 1000));
    return closeWindow();
  }
  if (millis() - heardAt > kStallMs) return failWindow("the download stalled");
  if (millis() - saidAt >= 5000) {
    saidAt = millis();
    say("image at %u bytes heap=%u minheap=%u", unsigned(imageAt), unsigned(ESP.getFreeHeap()),
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
  if (window != Window::None && !wifiota::Schedule::keep(window, f))
    return failWindow("closed early: %s", f.radioBusy ? "the radio is wanted for a duel" : "someone is using the badge");
  switch (phase) {
    case Phase::Joining:
      if (station.joined()) return joined();
      if (millis() - phaseAt > kJoinMs) return failWindow("could not join \"%s\" in %u s", WiFi.SSID().c_str(), unsigned(kJoinMs / 1000));
      return;
    case Phase::Image: return download();
    case Phase::Off: break;
  }
  // Off: what to open, the person waiting first.
  if (provisionAsked) {
    provisioning = provisionAsked;
    provisionAsked.reset();
    if (f.radioBusy) {
      say("provision refused: the radio is in a duel");
      improvBle.machine().couldNotJoin();
      provisioning.reset();
      return;
    }
    return openWindow(Window::Provision);
  }
  if (forced && !f.radioBusy) {
    forced = false;
    if (!network) return say("ota check: no network remembered (DEBUG wifi set <ssid> <password>)");
    return openWindow(Window::Forced);
  }
  if (confirmToken && trial.onTrial() && network && int32_t(now - confirmAt) >= 0) {
    confirmAt = now + kConfirmRetryMs;   // the next try, should this one fail
    return openWindow(Window::Confirm);
  }
  const wifiota::Hold hold = schedule.checkHold(f, now);
  if (hold != lastHold) say("hold %s -> %s", wifiota::holdName(lastHold), wifiota::holdName(hold));
  lastHold = hold;
  if (hold == wifiota::Hold::Open) openWindow(Window::Check);
}

// A duel wants the radio: any window but the trial's gives it up now
// (RadioGate::open, main.cpp). True while the radio is still Wi-Fi's.
static bool yieldRadio() {
  if (window != Window::None && window != Window::Confirm) failWindow("closed early: the radio is wanted for a duel");
  return station.isUp();
}

// An update fetched over Wi-Fi restarts only when nobody is using the badge:
// dark (low power) and no phone. A page's update restarts as it always has.
static bool mayRestart() { return !updater.ownedBy(kWifiPeer) || (powerPolicy.low() && !ble.connected()); }
// An update over Wi-Fi that is downloaded and switched holds the radio no longer.
static bool updateHoldsRadio() { return updater.active() && !(updater.ownedBy(kWifiPeer) && updater.ready()); }

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

// Before ble.begin(): the network, the switch, and whether this boot is an
// update over Wi-Fi on trial, which must reach the site again to keep itself.
// Reads only: nothing is written before boot has decided (provision.h).
static void begin() {
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  battery.begin();
  network = hw::WifiStore::load();
  autoOn = hw::WifiStore::autoOn();
  improvBle.onJoin = [](const improv::Credentials& c) {
    say("improv: a phone sent network \"%s\"", c.ssid);
    provisionAsked = c;
  };
  if (trial.onTrial())
    if (const std::optional<ota::Handover> h = trialPort.handover(); h && wifiota::isWifiToken(h->token)) {
      confirmToken = h->token;
      powerPolicy.sleepNow();   // restarted at a quiet moment: it stays dark
      say("on trial after an update over Wi-Fi: confirming over Wi-Fi, screen dark");
    }
  say("network %s%s%s, automatic checks %s", network ? "\"" : "", network ? network->ssid : "none",
      network ? "\"" : "", autoOn ? "on" : "OFF (DEBUG wifi on)");
}

// ---- the cable's verbs ---------------------------------------------------------------

static void statusLine() {
  const uint32_t now = millis();
  say("window=%s phase=%s network=%s%s%s auto=%d power=%s (%s, vbat=%u mV pin=%u mV%s) hold=%s next_check_in=%us",
      wifiota::windowName(window), phaseName(phase), network ? "\"" : "", network ? network->ssid : "none",
      network ? "\"" : "", int(autoOn), onPower() ? "on" : "battery", wifiota::powerWhyName(powerSense.why()),
      unsigned(powerSense.lastMv()), unsigned(battery.pinMv()), powerOverride ? ", overridden" : "",
      wifiota::holdName(schedule.checkHold(facts(), now)), unsigned(schedule.dueIn(now) / 1000));
  say("station=%s improv=%u/%u trial=%d confirm_pending=%d ota=%s owner=%s last=\"%s\" heap=%u minheap=%u",
      station.joined() ? "joined" : station.isUp() ? "up" : "off", unsigned(improvBle.machine().state()),
      unsigned(improvBle.machine().error()), int(trial.onTrial()), int(bool(confirmToken)),
      updater.active() ? (updater.ready() ? "ready" : "active") : "idle",
      updater.ownedBy(kWifiPeer) ? "wifi" : updater.active() ? "page" : "none", lastOutcome,
      unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()));
}

// `"My Net" pass` or `MyNet pass`: the SSID, quoted if it has spaces, then the rest as the password.
static std::optional<improv::Credentials> parseSet(const char* s) {
  while (*s == ' ') ++s;
  const char* end;
  if (*s == '"') {
    end = std::strchr(++s, '"');
    if (!end) return std::nullopt;
  } else {
    end = s;
    while (*end && *end != ' ') ++end;
  }
  const std::string_view ssid(s, size_t(end - s));
  s = *end == '"' ? end + 1 : end;
  while (*s == ' ') ++s;
  return wifiota::credentials(ssid, s);
}

// `DEBUG wifi [status|forget|set <ssid> [password]|on|off|power on|off|auto]`
static void wifiVerb(const char* line) {
  char what[12] = "", arg[12] = "";
  std::sscanf(line, "%*s %11s %11s", what, arg);
  if (!*what || !std::strcmp(what, "status")) return statusLine();
  if (!std::strcmp(what, "forget")) {
    network.reset();
    say("forgot the network (%s)", hw::WifiStore::forget() ? "erased" : "ERASE FAILED");
    return;
  }
  if (!std::strcmp(what, "set")) {
    const std::optional<improv::Credentials> c = parseSet(std::strstr(line, "set") + 3);
    if (!c) return say("want: wifi set <ssid|\"ssid with spaces\"> [password]");
    network = c;
    say("network \"%s\" (%s password, %s); DEBUG ota check tries it", c->ssid, c->password[0] ? "with a" : "no",
        hw::WifiStore::save(*c) ? "saved" : "SAVE FAILED");
    return;
  }
  if (!std::strcmp(what, "on") || !std::strcmp(what, "off")) {
    autoOn = !std::strcmp(what, "on");
    say("automatic checks %s (%s)", autoOn ? "on" : "off", hw::WifiStore::setAuto(autoOn) ? "saved" : "SAVE FAILED");
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
  say("want: wifi [status|forget|set <ssid> [password]|on|off|power on|off|auto]");
}

// `DEBUG ota check [url]`: a check now, idle or not; the url (http or https) for a test manifest.
static void otaVerb(const char* line) {
  char what[12] = "", url[sizeof forcedUrl] = "";
  std::sscanf(line, "%*s %11s %119s", what, url);
  if (std::strcmp(what, "check") != 0) return say("want: ota check [manifest url]");
  if (*url && std::strncmp(url, "https://", 8) != 0 && std::strncmp(url, "http://", 7) != 0)
    return say("ota check: the url must start http:// or https://");
  std::snprintf(forcedUrl, sizeof forcedUrl, "%s", url);
  forced = true;
  say("ota check asked (%s)%s", *url ? url : wifiota::kManifestUrl,
      window != Window::None ? ": after the open window closes" : "");
}

}  // namespace wififw

static void wifiVerb(const char* line) { wififw::wifiVerb(line); }
static void otaVerb(const char* line) { wififw::otaVerb(line); }
