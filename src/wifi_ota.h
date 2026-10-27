// Wi-Fi: Improv provisioning (with a hand's yes), the windows when the
// station is up, and the unattended update over HTTPS (docs/wifi-ota.md). The
// decisions are pure and tested natively (lib/wifiota, test/test_wifi_ota);
// the update itself is the same ota::Session, signature check, trial and
// rollback as Bluetooth's (lib/ota). main.cpp includes this once, after the
// loop's statics it uses (dish, ble, updater, trial, trialPort,
// trialWatchdog, powerPolicy, espNow, shot, debugAsks), so it is part of that
// one translation unit.
//
// Offline first (owner, 2026-10-21: "the use case is primarily offline car"):
// with no remembered network in range a window is one scan of about 1.6 s,
// while the badge is dark and nobody is there, a few times a day at most
// (schedule.h), and nothing on the screen, the loop, Bluetooth or a duel
// changes. Nothing here waits: the scan and the join are polled, the GET runs
// on its own task (hw/net_job.h), and the image goes into the slot a few KB
// a loop pass.
//
// Every line it logs starts "[wifi] ", and every change of window, phase or hold is one line.
//
//   off --window--> scanning --a remembered network--> joining --joined--> manifest --newer--> image --ready--> closing --> off
//                            --none in range---------> closing                      --nothing--> closing
#pragma once

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <mbedtls/platform.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <new>
#include <optional>
#include <tuple>

#include "hw/battery.h"
#include "hw/improv_ble.h"
#include "hw/net_job.h"
#include "hw/wifi_station.h"
#include "hw/wifi_store.h"
#include "paint/text.h"
#include "wifiota/consent.h"
#include "wifiota/improv.h"
#include "wifiota/manifest.h"
#include "wifiota/remembered.h"
#include "wifiota/schedule.h"

namespace wififw {

namespace improv = wifiota::improv;
using wifiota::Failure;
using wifiota::Window;

constexpr uint32_t kScanMs = 10000;            // a scan the driver has not finished by then is stopped
constexpr uint32_t kJoinMs = 15000;            // the join (with the core's one retry), then the window closes
constexpr uint32_t kConfirmRetryMs = 20000;    // on trial: the next try to reach the site
constexpr uint32_t kConfirmFirstMs = 10000;    // on trial: the first try, once boot's own allocations have settled
// Internal RAM a window wants free before it starts the Wi-Fi driver: the
// first time in a boot the core also makes its event and TCP/IP tasks and the
// netifs (about 16 KB that stay); a TLS GET wants 7 KB of stack and its
// handshake about 6 KB more. Board, #27: about 37 KB the first time, 21 after.
constexpr uint32_t kRadioNeedFirst = 52 * 1024;
constexpr uint32_t kRadioNeedAgain = 36 * 1024;
constexpr uint32_t kStuckMs = 60000;           // a GET that will not end after a cancel: its sockets are cut
constexpr size_t kTakePerPassBytes = 8192;     // image bytes a loop pass writes: about two flash sectors
constexpr uint32_t kBatteryEveryMs = 10 * 60 * 1000;
constexpr const char* kSiteUrl = "https://blorb.hypnodroid.com/?d=";
constexpr const char* kPetLinesUrl = "https://blorb.hypnodroid.com/api/pet-lines";

enum class Phase : uint8_t { Off, Scanning, Joining, Manifest, Image, Closing, Lines };
const char* phaseName(Phase p) {
  constexpr const char* kNames[] = {"off", "scanning", "joining", "manifest", "image", "closing", "lines"};
  return kNames[uint8_t(p)];
}

static hw::WifiStation station;
static hw::NetJob job;
static hw::Battery battery;
static wifiota::Schedule schedule;
static volatile uint32_t tlsInternal = 0;   // mbedtls blocks that fell back to internal RAM (tlsCalloc)

// What only Wi-Fi's own moments read, in PSRAM, made at boot (begin()):
// internal RAM is the scarce kind (a duel's radio takes about 37 KB of it).
struct Mem {
  wifiota::Networks networks;
  char lastOutcome[112] = "none";
  char forcedUrl[120] = "";   // `DEBUG ota check <url>`
  std::optional<improv::Credentials> provisionAsked, provisioning;
};
static Mem* memp = nullptr;
static Mem& mem() {
  if (!memp) {
    void* p = heap_caps_malloc(sizeof(Mem), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memp = new (p ? p : ::operator new(sizeof(Mem))) Mem;
  }
  return *memp;
}

static Window window = Window::None;
static Phase phase = Phase::Off;
static wifiota::Hold lastHold = wifiota::Hold::Open;
static uint32_t phaseAt = 0, windowAt = 0, saidAt = 0, closingSince = 0;
static bool cutSockets = false;
static uint32_t imageAt = 0;   // bytes handed to the session
static uint32_t darkSince = 0;
static bool wasDark = false;
static uint16_t cellMv = 0;
static uint32_t cellAt = 0;
static bool cellRead = false;
static uint32_t windows = 0, scans = 0;   // since boot, for DEBUG wifi

// The window a restart came in the middle of (RTC memory lives through a
// crash or a watchdog's reset, not a power cut): src/wifi_ota.h begin().
constexpr uint32_t kWindowMagic = 0x3A1F1000u;
RTC_NOINIT_ATTR static uint32_t inWindow;
RTC_NOINIT_ATTR static uint32_t crashesInARow;

// What is asked of the next window.
static bool forced = false;
// On trial after an update over Wi-Fi: the token the old image handed over.
static std::optional<ota::Token> confirmToken;
static uint32_t confirmAt = 0;

// A hand's yes before a phone may give it a network (wifiota/consent.h).
static wifiota::Consent consent;
static uint32_t consentGen = 0;   // the Bluetooth connection the yes is for

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
  std::vsnprintf(mem().lastOutcome, sizeof mem().lastOutcome, fmt, ap);
  va_end(ap);
  say("%s: %s", wifiota::windowName(window), mem().lastOutcome);
}

static bool lowBattery() { return cellRead && cellMv >= 2500 && cellMv < wifiota::Schedule::kLowBatteryMv; }

static wifiota::Facts facts(uint32_t now) {
  const bool dark = powerPolicy.low();
  return wifiota::Facts{
      !mem().networks.empty(),
      dark ? std::max<uint32_t>(1, now - darkSince) : 0,
      ble.connected() || updater.asking() || (updater.active() && !updater.ownedBy(kWifiPeer)) || debugAsks() ||
          consent.asking() || bool(shot),
      dish->duelActive() || std::strcmp(dish->fightState(), "off") != 0 || espNow.isOpen(),
      schedule.holdForBattery(lowBattery(), now),
  };
}

// A cable's URL for a forced check; on trial, the URL the update came from
// (WifiStore::source, in forcedUrl since boot); else the site's.
static const char* manifestUrl() {
  return (window == Window::Forced || window == Window::Confirm) && mem().forcedUrl[0] ? mem().forcedUrl
                                                                                       : wifiota::kManifestUrl;
}

static void setPhase(Phase p) {
  say("%s: %s -> %s (%u ms)", wifiota::windowName(window), phaseName(phase), phaseName(p), unsigned(millis() - phaseAt));
  phase = p;
  phaseAt = millis();
}

// The radio goes once the GET's task has ended (hw/net_job.h) and the
// driver has finished its scan or connection attempt (WifiStation::quiet).
static void radioDown() {
  station.off();
  job.reset();
  inWindow = 0;
  crashesInARow = 0;
  say("window %s closed after %u ms (heap=%u minheap=%u)", wifiota::windowName(window), unsigned(millis() - windowAt),
      unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()));
  if (phase != Phase::Off) setPhase(Phase::Off);
  window = Window::None;
}

// The window ends: the GET is told to stop, an unfinished download is
// dropped (the slot is not the running image's: the badge is as it was), and
// the radio goes down once the GET's task has.
static void closeWindow() {
  if (phase == Phase::Closing || window == Window::None) return;
  job.cancel();
  station.stop();   // the scan stopped, the connection left: off() once the driver is quiet
  if (updater.ownedBy(kWifiPeer) && !updater.ready()) updater.abort();
  mem().provisioning.reset();
  if (!job.busy() && !station.isUp()) return radioDown();
  closingSince = millis();
  cutSockets = false;
  setPhase(Phase::Closing);
}

// A Check that failed waits (schedule.h); a Provision tells the phone.
static void failWindow(Failure why, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(mem().lastOutcome, sizeof mem().lastOutcome, fmt, ap);
  va_end(ap);
  if (window == Window::Check) schedule.failed(why, millis());
  say("%s: FAILED %s%s", wifiota::windowName(window), mem().lastOutcome,
      window == Window::Check ? (why == Failure::Absent ? " (no network: next check later)" : " (the site: next check later)") : "");
  if (window == Window::Provision) improvBle.machine().couldNotJoin();
  closeWindow();
}

static void openWindow(Window w) {
  window = w;
  windowAt = phaseAt = millis();
  ++windows;
  // A trial's confirm window is not marked: a restart in one is the trial
  // rolling back (or the new image failing, which rolls it back), and the old
  // image it returns to did nothing wrong; marked, it waited 8 h to check again
  // and counted toward turning automatic checks off.
  inWindow = w == Window::Confirm ? 0 : kWindowMagic;
  say("window %s opens: dark=%d networks=%u heap=%u", wifiota::windowName(w), int(powerPolicy.low()),
      unsigned(mem().networks.size()), unsigned(ESP.getFreeHeap()));
  // Never start the driver short of RAM: a half-made init ("Expected to init 4
  // rx buffer") leaves it unable to start or stop until a restart (#27).
  const uint32_t need = station.everUp() ? kRadioNeedAgain : kRadioNeedFirst;
  const uint32_t have = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (have < need)
    return failWindow(Failure::Server, "not enough internal RAM to bring the radio up (%u free, %u wanted%s)",
                      unsigned(have), unsigned(need), station.everUp() ? "" : ", the first time");
  if (!station.up()) return failWindow(Failure::Server, "the radio would not come up (heap %u)", unsigned(ESP.getFreeHeap()));
  if (w == Window::Provision) {   // the phone's network, straight: it may be hidden
    say("provision: joining the phone's network without a scan");
    station.join(*mem().provisioning);
    return setPhase(Phase::Joining);
  }
  ++scans;
  if (!station.scan()) return failWindow(Failure::Absent, "the scan would not start");
  setPhase(Phase::Scanning);
}

// Networks are named in logs by their place in the list (#1 the newest),
// never by SSID: only `DEBUG wifi` and the cable's set and forget name them.
static void scanned() {
  int n = station.scanned();
  const bool late = n == WIFI_SCAN_RUNNING && millis() - phaseAt > kScanMs;
  if (n == WIFI_SCAN_RUNNING && !late) return;
  if (late) station.abandonScan();
  if (n < 0) {   // a scan that failed is no proof of absence: try the newest network the driver's own way
    say("the scan %s after %u ms (driver status %u): joining network #1 without it", late ? "did not finish" : "FAILED",
        unsigned(millis() - phaseAt), unsigned(station.scanStatus()));
    station.join(mem().networks[0]);
    return setPhase(Phase::Joining);
  }
  const std::optional<size_t> pick = mem().networks.pick(station.seen(), station.seenCount());
  say("scan done in %u ms: %d networks seen, %s", unsigned(millis() - phaseAt), n,
      pick ? "a remembered one among them" : "none of ours");
  if (!pick) return failWindow(Failure::Absent, "none of %u remembered networks in range (%d seen)", unsigned(mem().networks.size()), n);
  say("joining network #%u of %u", unsigned(*pick + 1), unsigned(mem().networks.size()));
  station.join(mem().networks[*pick]);
  setPhase(Phase::Joining);
}

static bool startGet(const char* url, bool text, const char* post = nullptr) {
  say("GET %s: heap=%u big=%u before the task (stack %u B)", std::strncmp(url, "https://", 8) == 0 ? "https" : "http",
      unsigned(ESP.getFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(std::strncmp(url, "https://", 8) == 0 ? hw::NetJob::stackTlsBytes : hw::NetJob::kStackHttpBytes));
  if (job.start(url, text, post)) return true;
  failWindow(Failure::Server, "GET %s: %s", url, job.error());
  return false;
}

static void joined() {
  say("joined in %u ms rssi=%d ch=%u ip=%s heap=%u", unsigned(millis() - phaseAt), int(WiFi.RSSI()), unsigned(WiFi.channel()),
      WiFi.localIP().toString().c_str(), unsigned(ESP.getFreeHeap()));
  if (window != Window::Provision) {
    if (startGet(manifestUrl(), true)) setPhase(Phase::Manifest);
    return;
  }
  mem().networks.remember(*mem().provisioning);
  const bool saved = hw::WifiStore::save(mem().networks);
  char url[64];
  std::snprintf(url, sizeof url, "%s%s", kSiteUrl, ble.name());
  improvBle.machine().joined(url);
  outcome("provisioned the phone's network as #1 (%s; %u remembered)", saved ? "saved" : "SAVE FAILED",
          unsigned(mem().networks.size()));
  consent.end();   // the next network needs another yes
  closeWindow();
}

static void startImage(const wifiota::Manifest& m) {
  uint8_t random[12];
  esp_fill_random(random, sizeof random);
  if (const std::optional<ota::Refusal> no = updater.install(m.image, wifiota::wifiToken(random), millis(), kWifiPeer))
    return failWindow(Failure::Server, "install refused: %s", no->why);
  char url[200];
  if (!wifiota::imageUrl(manifestUrl(), m.file, url, sizeof url)) return failWindow(Failure::Server, "image url too long");
  job.reset();
  // Where its trial will confirm: this manifest's URL when it is not the site's.
  const char* src = std::strcmp(manifestUrl(), wifiota::kManifestUrl) ? manifestUrl() : "";
  if (!hw::WifiStore::setSource(src)) say("!!! could not store the confirm URL: the trial will ask the site");
  if (!startGet(url, false)) return;
  say("downloading %u bytes (signature ok, pet saved, slot open)", unsigned(m.image.size));
  imageAt = 0;
  saidAt = millis();
  setPhase(Phase::Image);
}

// The pet's lines (#59): once a check installs nothing, the site's newest pack
// for him (POST /api/pet-lines, format "pack"), kept and said from then on.
// Whatever goes wrong here only closes the window: the lines he has stay.
static void startLines() {
  const char* body = petLinesBody();
  job.reset();
  if (!body || !startGet(kPetLinesUrl, true, body)) return closeWindow();
  setPhase(Phase::Lines);
}

static void readLines() {
  if (job.busy()) return;
  if (job.state() == hw::NetJob::State::Done) petLinesArrived(job.text(), job.textLen());
  else say("lines: none this time (%s); the ones he has stay", job.error());
  closeWindow();
}

static void readManifest() {
  if (job.busy()) return;   // the GET's task has ended: what it left is all there is
  if (job.state() != hw::NetJob::State::Done) return failWindow(Failure::Server, "manifest: %s", job.error());
  say("GET manifest: %u bytes, open %u ms, task stack %u B with %u never used; after the handshake heap=%u big=%u; "
      "now heap=%u minheap=%u; mbedtls blocks in internal RAM %u",
      unsigned(job.textLen()), unsigned(job.openMs()), unsigned(job.stackBytes()), unsigned(job.stackFree()),
      unsigned(job.heapAfterOpen()), unsigned(job.bigAfterOpen()), unsigned(ESP.getFreeHeap()),
      unsigned(ESP.getMinFreeHeap()), unsigned(tlsInternal));
  const std::optional<wifiota::Manifest> m = wifiota::parseManifest(std::string_view(job.text(), job.textLen()));
  if (!m) return failWindow(Failure::Server, "manifest unreadable (%u bytes)", unsigned(job.textLen()));
  const ota::Version& v = m->image.version;
  if (window == Window::Confirm) {
    // The trial's half the badge cannot check alone: the new image reached the
    // site over Wi-Fi, and what answered is the owner's signed manifest.
    if (!wifiota::signedBy(*m, hw::kOtaPublicKey))
      return failWindow(Failure::Server, "confirm: the manifest that came back is not signed by the owner's key");
    const ota::Confirm c = trial.confirm(*confirmToken);
    confirmToken.reset();
    if (mem().forcedUrl[0]) hw::WifiStore::setSource(""), mem().forcedUrl[0] = 0;
    outcome("trial confirmed over Wi-Fi (%s; the site has %u.%u.%u)", c == ota::Confirm::Confirmed ? "ok" : "not on trial",
            unsigned(v.major), unsigned(v.minor), unsigned(v.patch));
    schedule.checked(millis());
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
    return startLines();
  }
  startImage(*m);
}

// One pass of the download: what has arrived, up to kTakePerPassBytes, into
// ota::Session::data() as offset-prefixed packets, the path a page's writes take.
static void download() {
  uint8_t packet[4 + 1024];
  for (size_t taken = 0; taken < kTakePerPassBytes && !updater.ready();) {
    const size_t n = job.take(packet + 4, sizeof packet - 4);
    if (!n) break;
    packet[0] = uint8_t(imageAt), packet[1] = uint8_t(imageAt >> 8), packet[2] = uint8_t(imageAt >> 16),
    packet[3] = uint8_t(imageAt >> 24);
    updater.data(packet, 4 + n, millis(), kWifiPeer);
    if (!updater.ownedBy(kWifiPeer))   // the session ended: a bad hash, a bad image, a flash error
      return failWindow(Failure::Server, "update failed at %u bytes: %s", unsigned(imageAt), ota::lastName(updater.last()));
    imageAt += uint32_t(n);
    taken += n;
  }
  if (updater.ready()) {
    outcome("downloaded and verified in %u s; restarting into it once the badge is dark and alone",
            unsigned((millis() - phaseAt) / 1000));
    return closeWindow();
  }
  if (job.state() == hw::NetJob::State::Failed) return failWindow(Failure::Server, "image: %s", job.error());
  if (job.state() == hw::NetJob::State::Done && !job.waiting())
    return failWindow(Failure::Server, "image: all %u bytes came and the session is not ready", unsigned(imageAt));
  if (millis() - saidAt >= 5000) {
    saidAt = millis();
    say("image at %u bytes heap=%u minheap=%u", unsigned(imageAt), unsigned(ESP.getFreeHeap()),
        unsigned(ESP.getMinFreeHeap()));
  }
}

// The window is closing: the radio goes once the GET's task has ended.
static void closing() {
  if (!job.busy() && station.quiet()) return radioDown();
  if (!cutSockets && millis() - closingSince > kStuckMs) {
    cutSockets = true;
    say("the GET has not ended %u s after it was told to: cutting its connection", unsigned(kStuckMs / 1000));
    WiFi.disconnect();
  }
}

// The consent's question and its phone (wifiota/consent.h).
static void pollConsent(uint32_t now) {
  const uint32_t gen = ble.connected() ? ble.generation() : 0;
  if ((consent.asking() || consent.granted()) && gen != consentGen) consent.end();   // its phone went
  if (improvBle.takeOpened() && gen && improvBle.machine().state() == improv::State::AuthorizationRequired) {
    if (!consent.asking()) say("improv: a phone opened the service: asking for a yes on the screen");
    consent.ask(now);
    consentGen = gen;
  }
  consent.tick(now);
  switch (consent.take()) {
    case wifiota::Consent::Event::Granted:
      say("improv: yes (a turn or the button): the phone may send a network for %u s",
          unsigned(wifiota::Consent::kGrantMs / 1000));
      improvBle.machine().authorize();
      break;
    case wifiota::Consent::Event::Lapsed: say("improv: nobody said yes"); break;
    case wifiota::Consent::Event::Expired: say("improv: the yes ran out"); break;
    case wifiota::Consent::Event::None: break;
  }
  if (!consent.granted()) improvBle.machine().requireAuthorization();
}

static void readBattery(uint32_t now, bool force) {
  if (!force && cellRead && now - cellAt < kBatteryEveryMs) return;
  cellMv = battery.cellMv();
  cellAt = now;
  cellRead = true;
}

// Every loop.
static void poll(uint32_t now) {
  improvBle.poll();
  // Advertising paused while a GET opens (DNS, connect, TLS handshake,
  // headers), so the radio's time is the handshake's: only with no phone.
  ble.holdAdvertising((phase == Phase::Manifest || phase == Phase::Image || phase == Phase::Lines) && job.busy() &&
                      job.state() == hw::NetJob::State::Opening && !ble.connected());
  pollConsent(now);
  const bool dark = powerPolicy.low();
  if (dark && !wasDark) darkSince = now;
  wasDark = dark;
  const wifiota::Facts f = facts(now);
  if (window != Window::None && phase != Phase::Closing && !wifiota::Schedule::keep(window, f)) {
    say("%s: closed early: %s", wifiota::windowName(window),
        f.radioBusy ? "the radio is wanted for a duel" : "someone is using the badge");
    if (window == Window::Check) {   // not a failure: due again the next time it is dark and alone
      std::snprintf(mem().lastOutcome, sizeof mem().lastOutcome, "closed early: someone came");
    }
    if (window == Window::Provision) improvBle.machine().couldNotJoin();
    return closeWindow();
  }
  switch (phase) {
    case Phase::Scanning: return scanned();
    case Phase::Joining:
      switch (station.joining()) {
        case hw::WifiStation::Join::Joined: return joined();
        case hw::WifiStation::Join::Failed:
          return failWindow(Failure::Absent, "could not join in %u ms: %s (reason %u, %u disconnects, begin=%d, status %d)",
                            unsigned(millis() - phaseAt), station.reasonName(), unsigned(station.reason()),
                            unsigned(station.disconnectsSinceJoin()), station.beginStatus(), int(WiFi.status()));
        case hw::WifiStation::Join::Trying:
          if (millis() - phaseAt > kJoinMs)
            return failWindow(Failure::Absent, "could not join in %u s: last disconnect %s (reason %u, %u disconnects, status %d)",
                              unsigned(kJoinMs / 1000), station.reasonName(), unsigned(station.reason()),
                              unsigned(station.disconnectsSinceJoin()), int(WiFi.status()));
          return;
      }
      return;
    case Phase::Manifest: return readManifest();
    case Phase::Image: return download();
    case Phase::Lines: return readLines();
    case Phase::Closing: return closing();
    case Phase::Off: break;
  }
  // Off: what to open, the person waiting first.
  if (mem().provisionAsked) {
    mem().provisioning = mem().provisionAsked;
    mem().provisionAsked.reset();
    if (f.radioBusy) {
      say("provision refused: the radio is in a duel");
      improvBle.machine().couldNotJoin();
      mem().provisioning.reset();
      return;
    }
    return openWindow(Window::Provision);
  }
  if (forced && !f.radioBusy) {
    forced = false;
    if (mem().networks.empty()) return say("ota check: no network remembered (DEBUG wifi set <ssid> <password>)");
    return openWindow(Window::Forced);
  }
  if (confirmToken && trial.onTrial() && !mem().networks.empty() && int32_t(now - confirmAt) >= 0) {
    confirmAt = now + kConfirmRetryMs;   // the next try, should this one fail
    return openWindow(Window::Confirm);
  }
  wifiota::Hold hold = schedule.checkHold(f, now);
  if (hold == wifiota::Hold::Open) {   // the battery, read only when it is the last thing in the way
    readBattery(now, false);
    if (schedule.holdForBattery(lowBattery(), now)) hold = wifiota::Hold::LowBattery;
  }
  if (hold != lastHold)
    say("hold %s -> %s", wifiota::holdName(lastHold), wifiota::holdName(hold));
  lastHold = hold;
  if (hold == wifiota::Hold::Open) openWindow(Window::Check);
}

// A duel wants the radio: any window but the trial's gives it up now
// (RadioGate::open, main.cpp). True while the radio is still Wi-Fi's: the
// gate then waits, at most the GET's own timeouts.
static bool yieldRadio() {
  if (window != Window::None && window != Window::Confirm && phase != Phase::Closing) {
    say("%s: closed early: the radio is wanted for a duel", wifiota::windowName(window));
    if (window == Window::Provision) improvBle.machine().couldNotJoin();
    closeWindow();
  }
  return station.isUp();
}

// An update fetched over Wi-Fi restarts only when nobody is using the badge:
// dark (low power) and no phone. A page's update restarts as it always has.
static bool mayRestart() { return !updater.ownedBy(kWifiPeer) || (powerPolicy.low() && !ble.connected()); }
// An update over Wi-Fi that is downloaded and switched holds the radio no longer.
static bool updateHoldsRadio() { return updater.active() && !(updater.ownedBy(kWifiPeer) && updater.ready()); }

// The framework allocates mbedtls from internal RAM only, and a TLS session
// takes two 16 KB record buffers and a handshake's worth more (the parsed
// roots, the chain, the bignums): far more than the ~27 KB Bluetooth and the
// Wi-Fi driver leave (#27's board run: minheap 5.6 KB, "esp-aes: Failed to
// allocate memory"). So every mbedtls block goes to PSRAM; internal RAM only
// if PSRAM has none. What still needs internal RAM is the AES engine's DMA
// bounce of PSRAM records (about 3 KB, its own) and the GET task's stack.
// tlsInternal counts the blocks that fell back, for the GET's log line.
static void* tlsCalloc(size_t n, size_t size) {
  if (n && size > SIZE_MAX / n) return nullptr;
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p && (p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) ++tlsInternal;
  return p;
}
static void tlsFree(void* p) { heap_caps_free(p); }

// Before ble.begin(): the networks, the switch, whether the last boot ended
// inside a window, and whether this boot is an update over Wi-Fi on trial,
// which must reach the site again to keep itself. Reads only: nothing is
// written before boot has decided (provision.h).
static void begin() {
  const uint32_t heap0 = ESP.getFreeHeap();
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  battery.begin();
  if (const std::optional<wifiota::Networks> n = hw::WifiStore::load()) mem().networks = *n;
  hw::WifiStore::forgetOffSwitch();   // checks are never off (Schedule: the owner's rule)
  improvBle.onJoin = [](const improv::Credentials& c) {
    say("improv: the phone sent a network (ssid %u B, password %u B)", unsigned(std::strlen(c.ssid)),
        unsigned(std::strlen(c.password)));
    mem().provisionAsked = c;
  };
  // A crash, a watchdog or a brownout inside a window: the next check backs
  // off, more the more in a row, to a few hours at most (Schedule::crashed);
  // checks are never turned off. A power cut clears RTC memory, and the count with it.
  const esp_reset_reason_t why = esp_reset_reason();
  if (why == ESP_RST_POWERON) inWindow = 0, crashesInARow = 0;
  if (inWindow == kWindowMagic) {
    crashesInARow = crashesInARow < 100 ? crashesInARow + 1 : crashesInARow;
    schedule.crashed(millis(), crashesInARow);
    say("!!! the last boot ended inside a window (reset reason %d, %u in a row): next check in %u min", int(why),
        unsigned(crashesInARow), unsigned(schedule.dueIn(millis()) / 60000));
  } else {
    crashesInARow = 0;
  }
  inWindow = 0;
  if (trial.onTrial())
    if (const std::optional<ota::Handover> h = trialPort.handover(); h && wifiota::isWifiToken(h->token)) {
      confirmToken = h->token;
      confirmAt = millis() + kConfirmFirstMs;
      powerPolicy.sleepNow();   // restarted at a quiet moment: it stays dark
      hw::WifiStore::source(mem().forcedUrl, sizeof mem().forcedUrl);
      say("on trial after an update over Wi-Fi: confirming over Wi-Fi at %s, screen dark",
          mem().forcedUrl[0] ? mem().forcedUrl : "the site");
    }
  say("networks %u, automatic checks always on, heap %u -> %u", unsigned(mem().networks.size()), unsigned(heap0),
      unsigned(ESP.getFreeHeap()));
}

// This boot is an update over Wi-Fi on trial: it came in a quiet moment and
// stays dark (main.cpp setup(), before the backlight).
static bool quietBoot() {
  if (!trialPort.pending()) return false;
  const std::optional<ota::Handover> h = trialPort.handover();
  return h && wifiota::isWifiToken(h->token);
}

// ---- the question on the screen ------------------------------------------------------

static bool asks() { return consent.asking(); }
// The button, or a turn (main.cpp TurnYes): true when the question took it.
static bool press(uint32_t now) { return consent.yes(now); }
static bool drawing() { return consent.asking(); }
// A hard shake dismisses what is modal (#35): main.cpp's dismiss list calls
// this, as it does ota::Session::decline: the question goes as if unanswered,
// and the phone waiting on it hears "not authorized" when its yes never comes.
static void dismiss() {
  if (consent.asking()) consent.end(), say("improv: dismissed (a shake)");
}

// Drawn as the update's question and the debug unlock's are: a band over the
// pet, a rim ring that runs down, amber because it asks.
static void draw(paint::Canvas240& cv) {
  if (!consent.asking()) return;
  const uint32_t left = consent.msLeft(millis());
  constexpr uint16_t kAsk = 0xFDC6, kInk = 0xFFFF;   // ota/screen.cpp's amber and white
  constexpr paint::Clip kAll{0, 0, 240, 240, 0};
  for (int y = 82; y < 158; ++y)
    for (int x = 0; x < 240; ++x) cv.px[y * 240 + x] = paint::blend(cv.px[y * 240 + x], 0, 200);
  const int steps = int(1200 * uint64_t(left) / wifiota::Consent::kAskMs);
  for (int i = 0; i < steps; ++i) {
    const float a = float(i) / 1200 * 6.2831853f - 1.5707963f;
    for (int r = 112; r <= 118; ++r) {
      const int x = 120 + int(std::lround(r * std::cos(a))), y = 120 + int(std::lround(r * std::sin(a)));
      if (x >= 0 && x < 240 && y >= 0 && y < 240) cv.px[y * 240 + x] = kAsk;
    }
  }
  for (const auto& [text, y, colour] : {std::tuple{"Join Wi-Fi?", 106, kAsk}, std::tuple{"Turn me around!", 134, kInk}})
    paint::drawString(cv, paint::kFreeSans9pt7b, text, 120 - paint::textWidth(paint::kFreeSans9pt7b, text) / 2, y,
                      colour, kAll);
}

// ---- the cable's verbs ---------------------------------------------------------------

static void statusLine() {
  const uint32_t now = millis();
  readBattery(now, true);
  say("window=%s phase=%s networks=%u auto=%d hold=%s next_check_in=%us absent_streak=%u server_streak=%u vbat=%u mV (pin %u mV)%s",
      wifiota::windowName(window), phaseName(phase), unsigned(mem().networks.size()), 1, wifiota::holdName(schedule.checkHold(facts(now), now)),
      unsigned(schedule.dueIn(now) / 1000), unsigned(schedule.absentStreak()), unsigned(schedule.serverStreak()),
      unsigned(cellMv), unsigned(battery.pinMv()), lowBattery() ? " LOW" : "");
  say("station=%s improv=%u/%u consent=%s trial=%d confirm_pending=%d ota=%s owner=%s windows=%u scans=%u last=\"%s\" heap=%u minheap=%u",
      station.joined() ? "joined" : station.isUp() ? "up" : "off", unsigned(improvBle.machine().state()),
      unsigned(improvBle.machine().error()), consent.asking() ? "asking" : consent.granted() ? "granted" : "none",
      int(trial.onTrial()), int(bool(confirmToken)), updater.active() ? (updater.ready() ? "ready" : "active") : "idle",
      updater.ownedBy(kWifiPeer) ? "wifi" : updater.active() ? "page" : "none", unsigned(windows), unsigned(scans),
      mem().lastOutcome, unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()));
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

// `DEBUG wifi [status|due|set <ssid> [password]|forget [ssid]|list|verify|stack]`: no on or off, checks are never off
static void wifiVerb(const char* line) {
  char what[12] = "";
  std::sscanf(line, "%*s %11s", what);
  if (!*what || !std::strcmp(what, "status")) return statusLine();
  // `DEBUG wifi due`: the next automatic check is due now (Schedule::dueNow),
  // for a test run whose DEBUG warp moves the pet's clock but not the real one
  // the schedule keeps. It still opens only dark and alone; the status follows.
  if (!std::strcmp(what, "due")) {
    schedule.dueNow();
    say("due: the next automatic check is due now (it opens once dark %u s and alone)",
        unsigned(wifiota::Schedule::kDarkMs / 1000));
    return statusLine();
  }
  if (!std::strcmp(what, "forget")) {
    const char* rest = std::strstr(line, "forget") + 6;
    while (*rest == ' ') ++rest;
    if (*rest) {
      const std::optional<improv::Credentials> c = parseSet(rest);   // the SSID, quoted or not
      if (!c || !mem().networks.forget(c->ssid)) return say("forget: no network \"%s\" (DEBUG wifi lists them)", rest);
    } else {
      mem().networks.clear();
    }
    say("forgot %s (%s; %u remembered)", *rest ? "it" : "every network", hw::WifiStore::save(mem().networks) ? "saved" : "SAVE FAILED",
        unsigned(mem().networks.size()));
    return;
  }
  if (!std::strcmp(what, "set")) {
    const std::optional<improv::Credentials> c = parseSet(std::strstr(line, "set") + 3);
    if (!c)   // lengths only: the line itself may hold the password
      return say("want: wifi set <ssid|\"ssid with spaces\"> [password] (an SSID of 1-32 bytes, a password of up "
                 "to 64; got %u bytes after \"set\")",
                 unsigned(std::strlen(std::strstr(line, "set") + 3)));
    mem().networks.remember(*c);
    say("network \"%s\" (%s password, %s; %u remembered, newest first); DEBUG ota check tries it now", c->ssid,
        c->password[0] ? "with a" : "no", hw::WifiStore::save(mem().networks) ? "saved" : "SAVE FAILED", unsigned(mem().networks.size()));
    return;
  }
  // `DEBUG wifi verify <ssid> <password>`, as `set` takes them: whether what
  // is stored is byte for byte what was sent. Says only lengths and yes or no.
  if (!std::strcmp(what, "verify")) {
    const std::optional<improv::Credentials> c = parseSet(std::strstr(line, "verify") + 6);
    if (!c) return say("verify: want verify <ssid|\"ssid with spaces\"> [password]");
    const std::optional<size_t> at = mem().networks.find(c->ssid);
    if (!at) return say("verify: no remembered network has that SSID (%u B sent)", unsigned(std::strlen(c->ssid)));
    const improv::Credentials& n = mem().networks[*at];
    const bool same = !std::strcmp(n.password, c->password);
    say("verify: network #%u: ssid matches (%u B); password %s (sent %u B, stored %u B)", unsigned(*at + 1),
        unsigned(std::strlen(n.ssid)), same ? "MATCHES" : "DIFFERS", unsigned(std::strlen(c->password)),
        unsigned(std::strlen(n.password)));
    return;
  }
  // `DEBUG wifi list`: the remembered networks by name, newest first. The only
  // line that names them (with set's and forget's own replies).
  if (!std::strcmp(what, "list")) {
    for (size_t i = 0; i < mem().networks.size(); ++i)
      say("network #%u \"%s\" (%s password)", unsigned(i + 1), mem().networks[i].ssid,
          mem().networks[i].password[0] ? "with a" : "no");
    return say("networks %u of %u", unsigned(mem().networks.size()), unsigned(wifiota::kMaxNetworks));
  }
  // `DEBUG wifi stack <KB>`: the TLS GET task's stack until reset (each GET logs what it never used).
  if (!std::strcmp(what, "stack")) {
    char kb[8] = "";
    std::sscanf(line, "%*s %*s %7s", kb);
    const long k = std::strtol(kb, nullptr, 10);
    if (k >= 5 && k <= 20) hw::NetJob::stackTlsBytes = uint32_t(k) * 1024;
    else if (*kb) return say("stack: want 5..20 (KB)");
    return say("TLS GET stack %u B (default %u)", unsigned(hw::NetJob::stackTlsBytes), unsigned(hw::NetJob::kStackTlsDefault));
  }
  if (!std::strcmp(what, "on") || !std::strcmp(what, "off"))
    return say("automatic checks are always on: there is no switch (docs/ota.md, the owner's rule)");
  say("want: wifi [status|due|set <ssid> [password]|list|forget [ssid]|verify <ssid> [password]|stack <KB>]");
}

// `DEBUG ota check [url]`: a check now, idle or not; the url (http or https) for a test manifest.
static void otaVerb(const char* line) {
  char what[12] = "", url[sizeof mem().forcedUrl] = "";
  std::sscanf(line, "%*s %11s %119s", what, url);
  if (std::strcmp(what, "check") != 0) return say("want: ota check [manifest url]");
  if (*url && std::strncmp(url, "https://", 8) != 0 && std::strncmp(url, "http://", 7) != 0)
    return say("ota check: the url must start http:// or https://");
  // A cable's check: now, dark or not, alone or not. Only a duel's radio, an
  // open window, or no network at all hold it, and each says so.
  if (mem().networks.empty()) return say("ota check: refused, no network remembered (DEBUG wifi set first)");
  std::snprintf(mem().forcedUrl, sizeof mem().forcedUrl, "%s", url);
  forced = true;
  const wifiota::Facts f = facts(millis());
  say("ota check asked (%s), whatever the power and whoever is there%s", *url ? url : wifiota::kManifestUrl,
      window != Window::None ? ": waits for the open window to close"
      : f.radioBusy          ? ": waits for the duel or fight mode to give the radio back"
                             : ": opening now");
}

}  // namespace wififw

static void wifiVerb(const char* line) { wififw::wifiVerb(line); }
static void otaVerb(const char* line) { wififw::otaVerb(line); }
