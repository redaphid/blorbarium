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

#include <esp_core_dump.h>
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
#include "hw/log_tee.h"
#include "hw/net_job.h"
#include "hw/wifi_station.h"
#include "hw/wifi_store.h"
#include "paint/shot.h"
#include "paint/text.h"
#include "rdebug/remote.h"
#include "rdebug/shot.h"
#include "wifiota/consent.h"
#include "wifiota/improv.h"
#include "wifiota/manifest.h"
#include "wifiota/net_clock.h"
#include "wifiota/remembered.h"
#include "wifiota/schedule.h"

namespace wififw {

namespace improv = wifiota::improv;
using wifiota::Failure;
using wifiota::Window;

constexpr uint32_t kScanMs = 10000;            // a scan the driver has not finished by then is stopped
constexpr uint8_t kTriesAfterRollback = 3;      // a version that rolled back this many times here is not tried again
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

enum class Phase : uint8_t { Off, Scanning, Joining, Manifest, Image, Closing, Lines, Report, Shot };
const char* phaseName(Phase p) {
  constexpr const char* kNames[] = {"off", "scanning", "joining", "manifest", "image", "closing", "lines", "report", "shot"};
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
  improv::Credentials joining;   // what the window's join is trying: a network's own password, or a new one beside it
  // How each network's last join went, for `DEBUG wifi`: kept by SSID (never
  // printed), so it follows its network through remember and forget.
  struct Note {
    char ssid[33] = "";
    char result[96] = "";
  };
  Note notes[wifiota::kMaxNetworks + 1];
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
// The network the window is joining (its place in the list) and whether it
// had its second try: a weak signal often fails one join (TIMEOUT, AUTH_EXPIRE)
// and takes the next, and a failed Check waits long (Failure::Absent).
static std::optional<size_t> joinPick;
static bool joinRetried = false;

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
// The remote debug report under way (src/remote_debug.h): a restart in it is
// the report's, never the update check's (inWindow is cleared when it starts).
constexpr uint32_t kReportMagic = 0x2E9027D0u;
RTC_NOINIT_ATTR static uint32_t inReport;
RTC_NOINIT_ATTR static uint32_t crashesInARow;

// What is asked of the next window.
static bool forced = false;
// A window a phone or a hand asked for (a provision, a forced check) waits
// for Bluetooth to pause first (NusLink::pauseForWifi): the phone let go,
// nothing advertising, for at most kPauseWaitMs.
constexpr uint32_t kPauseWaitMs = 3000;
static Window pendingWindow = Window::None;
static uint32_t pendingAt = 0;
// A provision's end, on the screen for kResultMs (and in `DEBUG wifi`, the log, the report).
constexpr uint32_t kResultMs = 8000;
static uint32_t resultAt = 0;
static bool resultShowing = false, resultOk = false;
static char resultWhy[40] = "";
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

// The note kept for this SSID: its own, else one no remembered network owns.
static Mem::Note& noteFor(const char* ssid) {
  Mem& mm = mem();
  for (Mem::Note& n : mm.notes)
    if (!std::strcmp(n.ssid, ssid)) return n;
  for (Mem::Note& n : mm.notes)
    if (!n.ssid[0] || !mm.networks.find(n.ssid)) {
      std::snprintf(n.ssid, sizeof n.ssid, "%s", ssid);
      n.result[0] = 0;
      return n;
    }
  return mm.notes[0];
}

// The log as it leaves the badge (Bluetooth's LOG and DEBUG, the report's
// log and state): every remembered SSID and password, the phone's network
// in flight and any network forgotten this boot, replaced
// (rdebug::maskSecrets). The badge's own lines name networks by number.
static size_t masked(const char* in, size_t n, char* out, size_t cap) {
  static constexpr const char* kNames[wifiota::kMaxNetworks] = {"<net #1>", "<net #2>", "<net #3>"};
  rdebug::Secret secrets[2 * wifiota::kMaxNetworks + 2 * 3 + wifiota::kMaxNetworks + 1];   // the list, three in flight or beside, the notes
  size_t k = 0;
  const Mem& mm = mem();
  for (size_t i = 0; i < mm.networks.size(); ++i) {
    secrets[k++] = {mm.networks[i].ssid, kNames[i]};
    secrets[k++] = {mm.networks[i].password, "<password>"};
  }
  for (const std::optional<improv::Credentials>* c : {&mm.provisioning, &mm.provisionAsked, &mm.networks.beside()})
    if (*c) secrets[k++] = {(*c)->ssid, "<the phone's network>"}, secrets[k++] = {(*c)->password, "<password>"};
  for (const Mem::Note& note : mm.notes)
    if (note.ssid[0] && !mm.networks.find(note.ssid)) secrets[k++] = {note.ssid, "<a forgotten network>"};
  return rdebug::maskSecrets(std::string_view(in, n), secrets, k, out, cap);
}

static void showResult(bool ok, const char* why) {
  resultShowing = true, resultOk = ok, resultAt = millis();
  std::snprintf(resultWhy, sizeof resultWhy, "%s", why);
}

// A failed join's reason in a few words, for the screen (the full reason is in
// `DEBUG wifi` and the log): from the driver's last disconnect.
static const char* joinWhy() {
  switch (station.reason()) {
    case 15: case 204: return "wrong password?";   // 4WAY_HANDSHAKE_TIMEOUT, HANDSHAKE_TIMEOUT
    case 201: return "network not found";           // NO_AP_FOUND
    case 2: case 202: case 203: return "weak signal?";   // AUTH_EXPIRE, AUTH_FAIL, ASSOC_FAIL
    default: break;
  }
  return hw::WifiStation::associated() ? "no address from the router" : "could not connect";
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
  inReport = 0;
  crashesInARow = 0;
  const auto low = [](uint32_t v) { return v == UINT32_MAX ? 0u : unsigned(v); };   // 0: no sample
  say("window %s closed after %u ms (heap=%u minheap=%u; this window's low, sampled: tls=%u io=%u loop=%u)",
      wifiota::windowName(window), unsigned(millis() - windowAt), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      low(hw::netLow.tls), low(hw::netLow.io), low(hw::netLow.loop));
  if (phase != Phase::Off) setPhase(Phase::Off);
  window = Window::None;
  if (ble.pausedForWifi()) ble.pauseForWifi(false);   // the phone may come back (and read how it went)
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

// How the join of the window's network went, in its note. A Check's (or a
// cable's) try of an untried network is its try: the mark goes, and the next
// check picks as ever.
static void noteJoin(const char* fmt, ...) {
  const improv::Credentials* c = window == Window::Provision ? (mem().provisioning ? &*mem().provisioning : nullptr)
                                 : joinPick && *joinPick < mem().networks.size() ? &mem().networks[*joinPick]
                                                                                 : nullptr;
  if (!c) return;
  Mem::Note& n = noteFor(c->ssid);
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(n.result, sizeof n.result, fmt, ap);
  va_end(ap);
}

// A join's end, kept (wifiota::Networks::joinedWith, failedWith): an untried
// network or new password has had its try; a new password beside an old one
// replaces it once it joins, and goes once the old one does.
static bool wrongPassword() { return station.reason() == 15 || station.reason() == 204; }   // the handshake timed out
static void triedJoin(bool ok) {
  const bool beside = mem().networks.beside() && !std::strcmp(mem().networks.beside()->ssid, mem().joining.ssid);
  const bool wasNew = beside && !std::strcmp(mem().networks.beside()->password, mem().joining.password);
  if (ok) mem().networks.joinedWith(mem().joining);
  else mem().networks.failedWith(mem().joining, wrongPassword());
  const std::optional<size_t> at = mem().networks.find(mem().joining.ssid);
  if (beside)
    say("network #%u: its %s password %s: %s (%s)", unsigned(at.value_or(0) + 1), wasNew ? "new" : "old", ok ? "joined" : "failed",
        ok ? (wasNew ? "it replaces the old one" : "the new one beside it goes") : "both kept",
        hw::WifiStore::save(mem().networks) ? "saved" : "SAVE FAILED");
  else
    hw::WifiStore::save(mem().networks);
}

static void joinNet(const improv::Credentials& c) {
  mem().joining = c;
  station.join(c);
}

// The phone's network is saved the moment it arrives, marked untried, before
// any join (owner, 2026-10-29): whatever then goes wrong (a weak signal, a
// duel, someone came), the next Check, dark with no phone and advertising
// held, tries it before any other (wifiota/remembered.h).
// A network the badge knows, sent with another password, never replaces the
// one that works: the new one is kept beside it until one of them joins
// (wifiota::Networks::rememberBeside).
static void provisionSave() {
  const improv::Credentials& c = *mem().provisioning;
  const size_t at = mem().networks.rememberBeside(c);
  const bool beside = mem().networks.besideUntried() && !std::strcmp(mem().networks.beside()->password, c.password);
  if (!beside) std::snprintf(noteFor(c.ssid).result, sizeof Mem::Note::result, "saved from a phone; not tried yet");
  say("provision: saved %s network #%u, untried (ssid %u B, password %u B; %s; %u remembered)",
      beside ? "as a new password beside the one" : "as", unsigned(at + 1), unsigned(std::strlen(c.ssid)),
      unsigned(std::strlen(c.password)), hw::WifiStore::save(mem().networks) ? "saved" : "SAVE FAILED",
      unsigned(mem().networks.size()));
}

// A provision that did not join: the phone hears it could not join (Improv
// has no words for why), and the why goes on the screen, in `DEBUG wifi`
// (WIFI over Bluetooth, `debug wifi` over Wi-Fi) and the log. The network
// stays saved, untried.
static void provisionFailed(const char* why, const char* shortWhy) {
  improvBle.machine().couldNotJoin();
  showResult(false, shortWhy);
  if (!mem().provisioning) return;
  if (why) std::snprintf(noteFor(mem().provisioning->ssid).result, sizeof Mem::Note::result, "%s", why);
  const std::optional<size_t> at = mem().networks.find(mem().provisioning->ssid);
  say("provision: NOT joined (%s); network #%u stays saved, untried: the next check, dark and alone, tries it first", shortWhy,
      unsigned(at.value_or(0) + 1));
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
  if (phase == Phase::Joining || window == Window::Provision) noteJoin("%s", mem().lastOutcome);
  // A provision's failure keeps what it sent untried (provisionFailed); a check's try is over.
  if (phase == Phase::Joining && window != Window::Provision) triedJoin(false);
  if (window == Window::Provision)
    provisionFailed(nullptr, phase == Phase::Joining ? joinWhy() : "busy: try again soon");
  closeWindow();
}

static void openWindow(Window w) {
  window = w;
  windowAt = phaseAt = millis();
  hw::netLow.reset();
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
    joinPick.reset();   // the phone's network is tried once: the page says whether it joined
    joinNet(*mem().provisioning);
    return setPhase(Phase::Joining);
  }
  joinRetried = false;
  ++scans;
  if (!station.scan()) return failWindow(Failure::Absent, "the scan would not start");
  setPhase(Phase::Scanning);
}

// Networks are named by their place in the list (#1 the newest), never by
// SSID, in every line on every transport (`DEBUG wifi` gives the SSID's length).
static void scanned() {
  int n = station.scanned();
  const bool late = n == WIFI_SCAN_RUNNING && millis() - phaseAt > kScanMs;
  if (n == WIFI_SCAN_RUNNING && !late) return;
  if (late) station.abandonScan();
  if (n < 0) {   // a scan that failed is no proof of absence: try the newest network (an untried one first) the driver's own way
    joinPick = mem().networks.next(nullptr, 0).value_or(0);
    say("the scan %s after %u ms (driver status %u): joining network #%u without it", late ? "did not finish" : "FAILED",
        unsigned(millis() - phaseAt), unsigned(station.scanStatus()), unsigned(*joinPick + 1));
    joinNet(mem().networks.credsFor(*joinPick));
    return setPhase(Phase::Joining);
  }
  // An untried network first, in range or not (it may be hidden: joined without a channel), else the strongest.
  // The line says which: 0.7.35 said "a remembered one among them" of an untried one the scan never saw.
  const std::optional<size_t> pick = mem().networks.next(station.seen(), station.seenCount());
  const bool inRange = mem().networks.pick(station.seen(), station.seenCount()).has_value();
  say("scan done in %u ms: %d networks seen, %s", unsigned(millis() - phaseAt), n,
      inRange ? "a remembered one among them" : pick ? "none of ours, but an untried one is tried anyway" : "none of ours");
  if (!pick) return failWindow(Failure::Absent, "none of %u remembered networks in range (%d seen)", unsigned(mem().networks.size()), n);
  say("joining network #%u of %u%s", unsigned(*pick + 1), unsigned(mem().networks.size()),
      mem().networks.untried(*pick) ? " (untried: a provision could not join it; its first try)" : "");
  joinPick = *pick;
  joinNet(mem().networks.credsFor(*pick));
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

static void statusLine();
#include "remote_debug.h"

// The window's last step, once the manifest was read: the remote debug report
// (src/remote_debug.h), in a Check or a cable's check, then the radio goes.
static void finishWithReport() {
  // A provision's report is its check-in: the site's page waits for it
  // (/api/badge/online). It and a cable's check send a light one (rdebug::reportShape).
  if ((window == Window::Check || window == Window::Forced || window == Window::Provision) &&
      rd::start(window != Window::Check))
    return setPhase(Phase::Report);
  closeWindow();
}

static void readReport() {
  if (job.busy()) return;
  const bool home = job.state() == hw::NetJob::State::Done;
  if (window == Window::Provision) {   // the check-in the phone's page waits for
    showResult(true, home ? "online" : "joined; the site did not answer");
    outcome("provisioned: joined, and %s", home ? "checked in with the site" : "the check-in did not reach the site");
  }
  rd::arrived(home, job.text(), job.textLen(), job.error());
  if (rd::startShot(job.state() == hw::NetJob::State::Done)) return setPhase(Phase::Shot);   // a screenshot the owner asked for: alone, after
  closeWindow();
}

static void readShot() {
  if (job.busy()) return;
  rd::shotArrived(job.state() == hw::NetJob::State::Done, job.text(), job.textLen(), job.error());
  closeWindow();
}

static void joined() {
  say("joined in %u ms rssi=%d ch=%u ip=%s heap=%u", unsigned(millis() - phaseAt), int(WiFi.RSSI()), unsigned(WiFi.channel()),
      WiFi.localIP().toString().c_str(), unsigned(ESP.getFreeHeap()));
  noteJoin("joined (%s) rssi=%d ch=%u", wifiota::windowName(window), int(WiFi.RSSI()), unsigned(WiFi.channel()));
  station.heardJoin(mem().joining.ssid);   // heard, scan or none (a provision joins without one)
  triedJoin(true);
  if (window != Window::Provision) {
    if (startGet(manifestUrl(), true)) setPhase(Phase::Manifest);
    return;
  }
  // Saved when it came (provisionSave); triedJoin kept that it joined.
  const std::optional<size_t> at = mem().networks.find(mem().provisioning->ssid);
  const bool saved = hw::WifiStore::save(mem().networks);
  char url[64];
  std::snprintf(url, sizeof url, "%s%s", kSiteUrl, ble.name());
  improvBle.machine().joined(url);
  improvBle.provisioned();   // its phone coming back to read how it went is no new provision
  outcome("provisioned: joined the phone's network, #%u (%s; %u remembered); checking in with the site",
          unsigned(at.value_or(0) + 1), saved ? "saved" : "SAVE FAILED", unsigned(mem().networks.size()));
  showResult(true, "checking in with the site");
  consent.end();   // the next network needs another yes
  finishWithReport();
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
  if (!body) return finishWithReport();   // no pet: the report alone
  if (!startGet(kPetLinesUrl, true, body)) return;   // failWindow closed it
  setPhase(Phase::Lines);
}

static void readLines() {
  if (job.busy()) return;
  if (job.state() == hw::NetJob::State::Done) petLinesArrived(job.text(), job.textLen());
  else say("lines: none this time (%s); the ones he has stay", job.error());
  finishWithReport();
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
  // A version that rolled back is tried kTriesAfterRollback times in all before
  // it is refused for good: a trial that could not reach the site (a weak
  // signal for 5 minutes) is not a bad image, and the owner is far away.
  const std::optional<ota::Version> rb = trialPort.rolledBack();
  const uint8_t rbs = rb ? trialPort.rollbacks() : 0;
  const wifiota::Offer offer = wifiota::judge(*m, BOARD_NAME, *ota::parseVersion(blorb::kFirmwareVersion),
                                              blorb::kFormatVersion, rbs >= kTriesAfterRollback ? rb : std::nullopt);
  say("manifest %s %u.%u.%u size=%u fmt=%u auto=%d: %s (running %s fmt=%u)%s", m->image.board, unsigned(v.major),
      unsigned(v.minor), unsigned(v.patch), unsigned(m->image.size), unsigned(m->image.fmt), int(m->autoInstall),
      wifiota::offerName(offer), blorb::kFirmwareVersion, unsigned(blorb::kFormatVersion),
      rb && *rb == v && offer == wifiota::Offer::Install ? " (it rolled back here before: trying it again)" : "");
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
// Every HTTPS reply carries the time: the pet learns it from the network as
// from a phone's TIME (Dish::netTime), so his day and his wall-clock lines
// need no phone. No window, no reply: nothing changes.
static void heardClock(const hw::NetJob::Clock& c) {
  if (!dish) return;
  const bool knew = dish->timeKnown();
  dish->netTime(c.unixSeconds, c.tzMinutes);
  char zone[16] = "none sent";
  if (c.tzMinutes) std::snprintf(zone, sizeof zone, "%+d min", int(*c.tzMinutes));
  say("clock: %u from the server (zone %s); the time was %s", unsigned(c.unixSeconds), zone,
      knew ? "known already" : "unknown until now");
}

static void poll(uint32_t now) {
  if (window != Window::None) hw::NetLow::sample(hw::netLow.loop);
  improvBle.poll();
  if (const std::optional<hw::NetJob::Clock> c = job.takeClock(millis())) heardClock(*c);
  // Advertising paused while joining (the test board's joins failed AUTH_EXPIRE
  // and TIMEOUT more often than not at RSSI -77 with it on) and while a GET
  // opens (DNS, connect, TLS handshake, headers), so the radio's time is
  // Wi-Fi's: only with no phone.
  ble.holdAdvertising((phase == Phase::Joining ||
                       ((phase == Phase::Manifest || phase == Phase::Image || phase == Phase::Lines || phase == Phase::Report ||
                         phase == Phase::Shot) &&
                        job.busy() && job.state() == hw::NetJob::State::Opening)) &&
                      !ble.connected());
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
    if (window == Window::Provision) {
      outcome("provision: closed early: %s", f.radioBusy ? "fight mode or a duel wanted the radio" : "someone was using the badge");
      provisionFailed(mem().lastOutcome, f.radioBusy ? "in a fight: leave fight mode" : "busy: try again soon");
    }
    return closeWindow();
  }
  switch (phase) {
    case Phase::Scanning: return scanned();
    case Phase::Joining:
      // The join (with the core's one retry) for station.joinBudgetMs(), then
      // one more, then the window closes. On WPA3 the next try waits until 30 s
      // after the last began (hw::WifiStation::retryWaitMs).
      if (const hw::WifiStation::Join j = station.joining();
          j != hw::WifiStation::Join::Joined && joinPick && !joinRetried &&
          (j == hw::WifiStation::Join::Failed || millis() - phaseAt > station.joinBudgetMs())) {
        if (station.retryWaitMs()) return;
        joinRetried = true;
        say("join of network #%u failed (%s, reason %u): one more try", unsigned(*joinPick + 1), station.reasonName(),
            unsigned(station.reason()));
        station.join(mem().joining);
        phaseAt = millis();
        return;
      }
      switch (station.joining()) {
        case hw::WifiStation::Join::Joined: return joined();
        case hw::WifiStation::Join::Failed:
          return failWindow(Failure::Absent, "could not join in %u ms: %s (reason %u, %u disconnects, begin=%d, status %d)",
                            unsigned(millis() - phaseAt), station.reasonName(), unsigned(station.reason()),
                            unsigned(station.disconnectsSinceJoin()), station.beginStatus(), int(WiFi.status()));
        case hw::WifiStation::Join::Trying:
          if (millis() - phaseAt > station.joinBudgetMs())
            return failWindow(Failure::Absent, "could not join in %u s: last disconnect %s (reason %u, %u disconnects, status %d)%s",
                              unsigned(station.joinBudgetMs() / 1000), station.reasonName(), unsigned(station.reason()),
                              unsigned(station.disconnectsSinceJoin()), int(WiFi.status()),
                              hw::WifiStation::associated() ? ": no DHCP reply" : "");
          return;
      }
      return;
    case Phase::Manifest: return readManifest();
    case Phase::Image: return download();
    case Phase::Lines: return readLines();
    case Phase::Report: return readReport();
    case Phase::Shot: return readShot();
    case Phase::Closing: return closing();
    case Phase::Off: break;
  }
  // Off: the owner's remote commands, one a pass (src/remote_debug.h); then what to open, the person waiting first.
  rd::poll(now);
  // A window a phone or a hand asked for opens once Bluetooth has paused.
  if (pendingWindow != Window::None) {
    if (!ble.quietForWifi() && now - pendingAt < kPauseWaitMs) return;
    const Window w = pendingWindow;
    pendingWindow = Window::None;
    if (f.radioBusy) {   // fight mode opened while Bluetooth paused
      if (w == Window::Provision) {
        outcome("provision: not tried: fight mode or a duel has the radio (leave fight mode; the next check tries it)");
        provisionFailed(mem().lastOutcome, "in a fight: leave fight mode");
        mem().provisioning.reset();
      } else {
        say("ota check: not now: fight mode or a duel has the radio");
      }
      return ble.pauseForWifi(false);
    }
    return openWindow(w);
  }
  if (mem().provisionAsked) {
    mem().provisioning = mem().provisionAsked;
    mem().provisionAsked.reset();
    provisionSave();
    if (f.radioBusy) {
      outcome("provision: not tried: fight mode or a duel has the radio (leave fight mode; the next check tries it)");
      provisionFailed(mem().lastOutcome, "in a fight: leave fight mode");
      mem().provisioning.reset();
      return;
    }
    say("provision: joining now; Bluetooth pauses for the join and the check-in, then comes back");
    pendingWindow = Window::Provision, pendingAt = now;
    return ble.pauseForWifi(true);
  }
  if (forced && !f.radioBusy) {
    forced = false;
    if (mem().networks.empty()) return say("ota check: no network remembered (DEBUG wifi set <ssid> <password>)");
    say("ota check: Bluetooth pauses for the join and the check, then comes back");
    pendingWindow = Window::Forced, pendingAt = now;
    return ble.pauseForWifi(true);
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
    if (window == Window::Provision) {
      outcome("provision: closed early: a duel wanted the radio");
      provisionFailed(mem().lastOutcome, "in a fight: leave fight mode");
    }
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
  hw::NetLow::sample(hw::netLow.tls);
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
  if (rd::quietBoot()) powerPolicy.sleepNow(millis());   // a remote reboot came at a quiet moment: it stays dark
  rd::boot();
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
  if (why == ESP_RST_POWERON) inWindow = 0, crashesInARow = 0, inReport = 0;
  if (inReport == kReportMagic) rd::crashedInReport();
  inReport = 0;
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
      powerPolicy.sleepNow(millis());   // restarted at a quiet moment: it stays dark
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
  if (rd::quietBoot()) return true;
  if (!trialPort.pending()) return false;
  const std::optional<ota::Handover> h = trialPort.handover();
  return h && wifiota::isWifiToken(h->token);
}

// ---- the question on the screen ------------------------------------------------------

static bool asks() { return consent.asking(); }
// No window open: the radio is off, so the chip may nap (main.cpp).
static bool idle() { return window == Window::None && pendingWindow == Window::None; }
// Until the next automatic check is due (0: due now), or UINT32_MAX when none
// can open: the nap sleeps no longer than this (power::napSleepMs).
static uint32_t checkInMs(uint32_t now) {
  if (mem().networks.empty()) return UINT32_MAX;
  return schedule.dueIn(now);
}
// The button, or a turn (main.cpp TurnYes): true when the question took it.
static bool press(uint32_t now) { return consent.yes(now); }
static bool drawing() {
  if (resultShowing && millis() - resultAt >= kResultMs) resultShowing = false;
  return consent.asking() || resultShowing;
}
// A hard shake dismisses what is modal (#35): main.cpp's dismiss list calls
// this, as it does ota::Session::decline: the question goes as if unanswered,
// and the phone waiting on it hears "not authorized" when its yes never comes.
static void dismiss() {
  if (consent.asking()) consent.end(), say("improv: dismissed (a shake)");
}

// Drawn as the update's question and the debug unlock's are: a band over the
// pet, a rim ring that runs down, amber because it asks.
static void draw(paint::Canvas240& cv) {
  constexpr uint16_t kAsk = 0xFDC6, kInk = 0xFFFF, kGood = 0x87F0;   // ota/screen.cpp's amber and white; a green
  constexpr paint::Clip kAll{0, 0, 240, 240, 0};
  if (!consent.asking()) {   // how a provision went, a few seconds (drawing())
    if (!drawing()) return;
    for (int y = 82; y < 158; ++y)
      for (int x = 0; x < 240; ++x) cv.px[y * 240 + x] = paint::blend(cv.px[y * 240 + x], 0, 200);
    const char* head = resultOk ? "Wi-Fi joined!" : "Wi-Fi: not joined";
    for (const auto& [text, y, colour] : {std::tuple{head, 106, resultOk ? kGood : kAsk}, std::tuple{(const char*)resultWhy, 134, kInk}})
      paint::drawString(cv, paint::kFreeSans9pt7b, text, 120 - paint::textWidth(paint::kFreeSans9pt7b, text) / 2, y,
                        colour, kAll);
    return;
  }
  const uint32_t left = consent.msLeft(millis());
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
  for (const auto& [text, y, colour] : {std::tuple{"Join Wi-Fi?", 106, kAsk}, std::tuple{"Tip me on my side!", 134, kInk}})
    paint::drawString(cv, paint::kFreeSans9pt7b, text, 120 - paint::textWidth(paint::kFreeSans9pt7b, text) / 2, y,
                      colour, kAll);
}

// ---- the cable's verbs ---------------------------------------------------------------
// The cable's, Bluetooth's (DEBUG, WIFI) and Wi-Fi's (`debug`): one badge, three transports.

// Each remembered network by number: its SSID's length (never the SSID), its
// password's, its security and signal when it was joined since the last
// scan, else in the last scan, whether it waits for its first try, and how
// its last join this boot went.
static void networkLines() {
  for (size_t i = 0; i < mem().networks.size(); ++i) {
    const improv::Credentials& c = mem().networks[i];
    char heard[64] = "no scan yet this boot";
    if (const std::optional<hw::WifiStation::Heard> h = station.heard(c.ssid))
      std::snprintf(heard, sizeof heard, "%s ch %u rssi %d %s", h->security, unsigned(h->channel), h->rssi,
                    h->joined ? "when joined" : "in the last scan");
    else if (scans)
      std::snprintf(heard, sizeof heard, "not in the last scan");
    const Mem::Note& n = noteFor(c.ssid);
    char beside[64] = "";
    if (const std::optional<improv::Credentials>& b = mem().networks.beside(); b && !std::strcmp(b->ssid, c.ssid))
      std::snprintf(beside, sizeof beside, ", a new password beside it (%u B%s)", unsigned(std::strlen(b->password)),
                    mem().networks.besideUntried() ? ", UNTRIED: tried first" : ", tried: the old one first");
    say("network #%u: ssid %u B, password %u B, %s%s%s; last join: %s", unsigned(i + 1), unsigned(std::strlen(c.ssid)),
        unsigned(std::strlen(c.password)), heard, mem().networks.untried(i) ? ", UNTRIED (the next check tries it first)" : "",
        beside, n.result[0] ? n.result : "none this boot");
  }
  say("networks %u of %u; last outcome: %s", unsigned(mem().networks.size()), unsigned(wifiota::kMaxNetworks), mem().lastOutcome);
}

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
  networkLines();
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
  // `DEBUG wifi forget [<n>|<ssid>]`: by its number in `DEBUG wifi`, or its SSID; bare, every network.
  if (!std::strcmp(what, "forget")) {
    const char* rest = std::strstr(line, "forget") + 6;
    while (*rest == ' ') ++rest;
    if (*rest) {
      char* end = nullptr;
      const unsigned long k = std::strtoul(rest, &end, 10);
      while (end && *end == ' ') ++end;
      const bool byNumber = end != rest && !*end && k >= 1 && mem().networks.forgetAt(k - 1);
      if (!byNumber) {
        const std::optional<improv::Credentials> c = parseSet(rest);   // the SSID, quoted or not
        if (!c || !mem().networks.forget(c->ssid))
          return say("forget: no network #%.3s or with that SSID (%u B sent; DEBUG wifi numbers them)", rest,
                     unsigned(std::strlen(rest)));
      }
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
    mem().networks.setUntried(0, true);   // never joined yet: the next check (or DEBUG ota check) tries it first
    std::snprintf(noteFor(c->ssid).result, sizeof Mem::Note::result, "set by hand; not tried yet");
    say("network #1: ssid %u B, password %u B (%s; %u remembered, newest first), untried; DEBUG ota check tries it now",
        unsigned(std::strlen(c->ssid)), unsigned(std::strlen(c->password)),
        hw::WifiStore::save(mem().networks) ? "saved" : "SAVE FAILED", unsigned(mem().networks.size()));
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
  // `DEBUG wifi list`: the remembered networks by number, newest first (as `DEBUG wifi` ends).
  if (!std::strcmp(what, "list")) return networkLines();
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
  say("want: wifi [status|due|set <ssid> [password]|list|forget [<n>|ssid]|verify <ssid> [password]|stack <KB>]");
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
static size_t maskLog(const char* in, size_t n, char* out, size_t cap) { return wififw::masked(in, n, out, cap); }

// `DEBUG wpa3 [plain|both|hnp|h2e] [cpu] [psnone] [mac=<12 hex>]`: how WPA3 joins are made until reset
// (hw::Wpa3Cell). psnone turns Bluetooth off for good: `DEBUG reboot` after.
static void wpa3Verb(const char* line) {
  hw::Wpa3Cell c;
  c.asked = true;
  c.plain = std::strstr(line, " plain") != nullptr;
  if (std::strstr(line, " hnp")) c.pwe = WPA3_SAE_PWE_HUNT_AND_PECK;
  if (std::strstr(line, " h2e")) c.pwe = WPA3_SAE_PWE_HASH_TO_ELEMENT;
  c.cpuMax = std::strstr(line, " cpu") != nullptr;
  c.psNone = std::strstr(line, " psnone") != nullptr;
  if (const char* m = std::strstr(line, " mac=")) {
    unsigned v[6];
    if (std::sscanf(m + 5, "%2x%2x%2x%2x%2x%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
      c.setMac = true;
      for (int i = 0; i < 6; ++i) c.mac[i] = uint8_t(v[i]);
    }
  }
  if (c.psNone && esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
    NimBLEDevice::deinit(false);   // deinit(true) frees the link's static callbacks and panics
    Serial.printf("[wifi] wpa3 cell: bluetooth off for ps none: controller=%d\n", int(esp_bt_controller_get_status()));
  }
  hw::wpa3Cell = c;
  Serial.printf("[wifi] wpa3 cell next: %s pwe=%u cpu=%d psnone=%d mac=%s\n", c.plain ? "plain" : "option-b", unsigned(c.pwe),
                int(c.cpuMax), int(c.psNone), c.setMac ? "set" : "own");
}
static void otaVerb(const char* line) { wififw::otaVerb(line); }
