// Remote debug (docs/remote-debug.md): the badge's side. src/wifi_ota.h
// includes this inside namespace wififw, after its statics, so it is part of
// main.cpp's one translation unit and sees the window, the schedule and the
// update's state.
//
// Each Check or Forced window that reaches the site ends with one more POST,
// /api/badge/report: the firmware, the uptime and why the last boot ended, a
// crash's backtrace from the core dump, the update checks' state, the heap,
// the battery, the signal, the pet, the results of the last commands, and the
// newest 4 KB of the serial log (hw/log_tee.h). The reply is the owner's queued
// commands (lib/rdebug: an allowlist, each id once), run once the window has
// closed, their results in the next report. A provision's report and a
// cable's forced check's are light (rdebug::reportShape): 1 KB of the log, no
// state capture, no screenshot after; those wait for the next Check.
//
// Everything here is PSRAM, made at the first report: the body, the log's
// copy, the state capture, a screenshot. Internal RAM: these few words, and
// two RTC words for a remote reboot's result.
//
// Whatever goes wrong here only closes the window: a report that does not
// reach the site is sent again (newer) next time, its results with it.
namespace rd {

constexpr const char* kReportUrl = "https://blorb.hypnodroid.com/api/badge/report";
// The report stays under rdebug::kReportMax (5 KB): the log's tail gets what the
// rest leaves, at most 4 KB; a state capture is 2 KB. Only a screenshot the
// owner asked for goes past it, once (a failed one is dropped).
constexpr size_t kStateBytes = rdebug::kStateMax;
// Internal RAM the report wants free before its GET task starts (7 KB of stack
// and the handshake's bounce buffers); short of it, no report this window.
constexpr uint32_t kReportHeap = 24 * 1024, kReportBig = 9 * 1024;
constexpr const char* kShotUrl = "https://blorb.hypnodroid.com/api/debug";   // device debug, kind "shot"
constexpr uint32_t kStateSettleMs = 1500;   // STATE's reply goes out from the protocol's next pass
constexpr size_t kResults = 8, kPending = 8;

struct Result {
  uint32_t id;
  bool ok;
  char text[80];
};

struct Mem {
  Result results[kResults];
  uint8_t nResults = 0, sentResults = 0;   // sentResults: in the report on its way
  rdebug::Command pending[kPending], got[kPending];
  uint8_t nPending = 0;
  char log[hw::LogTee::kRingBytes];
  char logMasked[hw::LogTee::kRingBytes + 2048];   // a mask may be longer than what it hides
  char state[kStateBytes + 1];
  size_t stateLen = 0;
  bool crash = false;
  esp_core_dump_summary_t dump;
  char text[160];   // one escaped short string
};
static Mem* m = nullptr;
static char* body = nullptr;
static size_t bodyCap = 0;
// A `shot`: 120x120 as the cable's shot lines (rdebug/shot.h), in PSRAM until its one try.
static char* shotText = nullptr;
static char* shotBody = nullptr;
static rdebug::ShotSlot shotSlot;
static uint32_t lastId = 0;        // the newest command id seen this boot
static uint64_t stateMark = 0;
static uint32_t stateAt = 0, stateId = 0;
static char stateWhat[24] = "state";   // what asked for the capture: `state`, or `debug <its verb>`
static uint32_t reports = 0, reportFails = 0;
static rdebug::ReportShape shape = rdebug::reportShape(false);   // the report under way's
static bool stateSent = false;   // the report under way carries the state capture
static bool noReports = false;      // the last boot restarted inside a report: none this boot
static esp_reset_reason_t resetWhy = ESP_RST_UNKNOWN;
// A remote reboot's id, kept through the restart (RTC memory): its result goes in the first report after.
constexpr uint32_t kRebootMagic = 0x2EB0071Du;
RTC_NOINIT_ATTR static uint32_t rebootMagic;
RTC_NOINIT_ATTR static uint32_t rebootId;

static bool ready() {
  if (m) return true;
  void* p = heap_caps_malloc(sizeof(Mem), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) return false;
  m = new (p) Mem;
  return true;
}

static void result(uint32_t id, bool ok, const char* fmt, ...) {
  if (!ready()) return;
  if (m->nResults == kResults) {   // full: the oldest goes
    std::memmove(m->results, m->results + 1, sizeof(Result) * (kResults - 1));
    --m->nResults;
    if (m->sentResults) --m->sentResults;
  }
  Result& r = m->results[m->nResults++];
  r.id = id, r.ok = ok;
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(r.text, sizeof r.text, fmt, ap);
  va_end(ap);
  say("remote %u: %s %s", unsigned(id), ok ? "ok" : "REFUSED", r.text);
}

static const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "ext";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}
static bool crashed(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

// Boot (wififw::begin): why the last boot ended, its crash's summary when it
// crashed (the core dump in flash, CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH), and a
// remote reboot's result.
static void boot() {
  resetWhy = esp_reset_reason();
  if (!ready()) return;
  if (crashed(resetWhy)) {
    m->crash = esp_core_dump_get_summary(&m->dump) == ESP_OK;
    say("remote: the last boot ended in %s%s", resetName(resetWhy), m->crash ? "; its core dump's summary goes in the next report" : "");
  }
  if (rebootMagic == kRebootMagic) {
    lastId = rebootId;
    result(rebootId, resetWhy == ESP_RST_SW, "restarted (reset reason %s)", resetName(resetWhy));
  }
  rebootMagic = 0;
}
// wififw::begin: the boot before ended inside a report (a crash, a watchdog).
// The update checks go on as ever; the reports stop until the next restart,
// so a fault in the report can never loop.
static void crashedInReport() {
  noReports = true;
  say("!!! the last boot ended inside a remote debug report: no reports this boot (update checks as ever)");
}

// Restarted by a remote reboot: it came in a quiet moment and stays dark (wififw::quietBoot).
static bool quietBoot() { return rebootMagic == kRebootMagic && esp_reset_reason() == ESP_RST_SW; }

// ---- the body ------------------------------------------------------------------

struct Out {
  char* p;
  size_t cap, n = 0;
  bool full = false;
  void f(const char* fmt, ...) {
    if (full) return;
    va_list ap;
    va_start(ap, fmt);
    const int k = std::vsnprintf(p + n, cap - n, fmt, ap);
    va_end(ap);
    if (k < 0 || size_t(k) >= cap - n) full = true, p[n] = 0;
    else n += size_t(k);
  }
  void drop() {   // the trailing comma
    if (!full && n) --n;
  }
  void str(const char* key, const char* s, size_t len) {   // "key":"<escaped>",
    if (full) return;
    f("\"%s\":\"", key);
    if (full || cap - n < 4) return void(full = true);
    n += rdebug::jsonEscape(s, len, p + n, cap - n - 3);
    f("\",");
  }
  // "key":"<the newest of s, escaped into at most `room` bytes>",
  void tail(const char* key, const char* s, size_t len, size_t room) {
    if (full) return;
    f("\"%s\":\"", key);
    if (full || cap - n < 4) return void(full = true);
    const size_t most = std::min(room + 1, cap - n - 3);
    n += rdebug::jsonEscapeTail(s, len, p + n, most);
    f("\",");
  }
};

static const char* build(size_t& len) {   // as `shape` says
  const size_t want = rdebug::kReportMax + 64;
  if (bodyCap < want) {
    heap_caps_free(body);
    body = static_cast<char*>(heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    bodyCap = body ? want : 0;
  }
  if (!body || !ready()) return nullptr;
  Out o{body, bodyCap};
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const uint32_t now = millis();
  o.f("{\"v\":1,\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"fw\":\"%s\",\"board\":\"%s\",\"uptime_s\":%u,\"reset\":\"%s\",",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], blorb::kFirmwareVersion, BOARD_NAME, unsigned(now / 1000),
      resetName(resetWhy));
  o.f("\"heap\":%u,\"minheap\":%u,\"big\":%u,\"psram\":%u,\"vbat_mv\":%u,\"rssi\":%d,\"dark\":%d,",
      unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), unsigned(cellMv), int(WiFi.RSSI()), int(powerPolicy.low()));
  // The update checks: never off (the owner's rule); auto=0 only while no network is remembered.
  const std::optional<ota::Version> back = trialPort.rolledBack();
  o.f("\"ota\":{\"auto\":%d,\"window\":\"%s\",\"next_s\":%u,\"every_min\":%u,\"absent_streak\":%u,\"server_streak\":%u,"
      "\"on_trial\":%d,\"rolled_back\":",
      int(facts(now).enabled), wifiota::windowName(window), unsigned(schedule.dueIn(now) / 1000), unsigned(schedule.everyMs() / 60000),
      unsigned(schedule.absentStreak()), unsigned(schedule.serverStreak()), int(trial.onTrial()));
  if (back) o.f("\"%u.%u.%u\",", unsigned(back->major), unsigned(back->minor), unsigned(back->patch));
  else o.f("null,");
  o.f("\"crashes_in_window\":%u,\"windows\":%u,\"networks\":%u,\"tls_internal\":%u,",
      unsigned(crashesInARow), unsigned(windows), unsigned(mem().networks.size()), unsigned(tlsInternal));
  o.str("last", mem().lastOutcome, std::strlen(mem().lastOutcome));
  o.drop();
  o.f("},\"reports\":%u,\"report_fails\":%u,", unsigned(reports), unsigned(reportFails));
  o.f("\"pet\":");
  if (!o.full) o.n += petReport(o.p + o.n, o.cap - o.n);
  o.f(",\"crash\":");
  if (m->crash) {
    const esp_core_dump_summary_t& d = m->dump;
    o.f("{\"task\":\"");
    if (!o.full) o.n += rdebug::jsonEscape(d.exc_task, strnlen(d.exc_task, sizeof d.exc_task), o.p + o.n, o.cap - o.n);
    o.f("\",\"pc\":\"0x%08x\",\"cause\":%u,\"vaddr\":\"0x%08x\",\"corrupt\":%d,\"bt\":[", unsigned(d.exc_pc),
        unsigned(d.ex_info.exc_cause), unsigned(d.ex_info.exc_vaddr), int(d.exc_bt_info.corrupted));
    for (uint32_t i = 0; i < d.exc_bt_info.depth && i < 16; ++i) o.f("%s\"0x%08x\"", i ? "," : "", unsigned(d.exc_bt_info.bt[i]));
    o.f("],\"elf\":\"");
    for (size_t i = 0; i < 16 && i < sizeof d.app_elf_sha256 && d.app_elf_sha256[i]; ++i) o.f("%c", char(d.app_elf_sha256[i]));
    o.f("\"}");
  } else {
    o.f("null");
  }
  o.f(",\"results\":[");
  for (uint8_t i = 0; i < m->nResults; ++i) {
    const Result& r = m->results[i];
    rdebug::jsonEscape(r.text, std::strlen(r.text), m->text, sizeof m->text);
    o.f("%s{\"id\":%u,\"ok\":%s,\"text\":\"%s\"}", i ? "," : "", unsigned(r.id), r.ok ? "true" : "false", m->text);
  }
  o.f("],");
  if (m->stateLen && shape.state) o.str("state", m->state, m->stateLen);   // a light one leaves it for the next
  // The log's newest bytes, in what is left of the shape's max (the 2 closing bytes kept).
  // Masked: no Wi-Fi SSID or password leaves the badge (wififw::masked).
  const size_t logLen = masked(m->log, hw::logTee.tail(m->log, sizeof m->log), m->logMasked, sizeof m->logMasked);
  o.tail("log", m->logMasked, logLen, rdebug::logRoom(o.n + std::strlen("\"log\":\"\",}"), shape));
  o.drop();
  o.f("}");
  if (o.full) return nullptr;
  m->sentResults = m->nResults;
  stateSent = shape.state && m->stateLen;
  len = o.n;
  return body;
}

// ---- the window's last step ------------------------------------------------------

// After the manifest (and the pet's lines): the report, then the window closes.
// `light`: a provision's check-in or a cable's check (rdebug::reportShape).
static bool start(bool light) {
  if (noReports) return false;
  // The last GET's ended task first (NetJob::reset reaps it): its 7 KB stack
  // is internal RAM, and counted here it left a Check after the manifest and
  // the pet's lines at 19.9 KB, under kReportHeap, so no Check reported (0.7.38's soak).
  job.reset();
  const uint32_t have = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 big = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (have < kReportHeap || big < kReportBig) {
    say("remote: no report this window: internal RAM %u free, %u largest (wants %u, %u)", unsigned(have), unsigned(big),
        unsigned(kReportHeap), unsigned(kReportBig));
    return false;
  }
  size_t len = 0;
  shape = rdebug::reportShape(light);
  const char* b = build(len);
  if (!b) {
    say("remote: no report this time (no PSRAM, or it did not fit)");
    return false;
  }
  say("remote: %s report of %u bytes: heap=%u big=%u before the task", light ? "light" : "full", unsigned(len),
      unsigned(ESP.getFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  // A restart from here on is the report's: it never counts against the update check.
  inWindow = 0;
  inReport = kReportMagic;
  if (job.start(kReportUrl, true, b, len)) return true;
  inReport = 0;
  say("remote: the report could not start: %s", job.error());   // not the check's failure: the manifest was read
  return false;
}

// The reply: the report is home (what it carried is dropped), and the commands it brought queue.
static void arrived(bool done, const char* text, size_t len, const char* error) {
  if (!ready()) return;
  inReport = 0;
  if (!done) {
    ++reportFails;
    m->sentResults = 0;
    return say("remote: the report did not go (%s); the next window sends a fresh one (paced %u ms since boot)", error,
               unsigned(hw::ChunkedTls::pacedMs));
  }
  ++reports;
  std::memmove(m->results, m->results + m->sentResults, sizeof(Result) * (m->nResults - m->sentResults));
  m->nResults -= m->sentResults;
  m->sentResults = 0;
  if (stateSent) m->stateLen = 0;   // a light report left it for the next
  stateSent = false;
  if (!rdebug::trustedReplyUrl(kReportUrl)) return;   // only the site, over HTTPS with its pinned roots, may ask
  rdebug::Command* got = m->got;   // PSRAM: a Command holds a marquee's text
  const size_t n = rdebug::parseCommands(std::string_view(text, len), lastId, got, kPending - m->nPending);
  for (size_t i = 0; i < n; ++i) {
    lastId = got[i].id;
    if (!got[i].ok) result(got[i].id, false, "%s: %s", rdebug::verbName(got[i].verb), got[i].why);
    else m->pending[m->nPending++] = got[i];
  }
  say("remote: report home; %u command%s (paced %u ms since boot)", unsigned(n), n == 1 ? "" : "s",
      unsigned(hw::ChunkedTls::pacedMs));
}

// ---- the commands, once the window has closed ------------------------------------------

static void dropShot() {
  heap_caps_free(shotText), heap_caps_free(shotBody);
  shotText = shotBody = nullptr;
}

// The screen's last frame (it is dark: nothing repaints the canvas while this
// runs, in one pass), halved and coded; it waits for a window of its own POST.
static void shot(uint32_t id) {
  const auto ps = [](size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); };
  constexpr size_t kPx = size_t(rdebug::kShotSide) * rdebug::kShotSide;
  auto* small = static_cast<uint16_t*>(ps(kPx * 2));
  auto* packed = static_cast<uint8_t*>(ps(kPx * 2 + kPx / 64 + 16));
  if (!shotText) shotText = static_cast<char*>(ps(rdebug::kShotMax));
  size_t len = 0;
  if (small && packed && shotText) {
    rdebug::halve565(canvas.px, 240, 240, small);
    const size_t n = rdebug::packBits565(small, kPx, packed, kPx * 2 + kPx / 64 + 16);
    if (n) len = rdebug::shotLines(packed, n, rdebug::kShotSide, rdebug::kShotSide, rdebug::crc565(small, kPx), shotText, rdebug::kShotMax);
  }
  heap_caps_free(small), heap_caps_free(packed);
  if (!len) {
    dropShot();
    return result(id, false, "shot: no PSRAM for it");
  }
  shotSlot.hold(id, len);
  say("remote %u: shot taken, 120x120 in %u bytes; it goes in the next window, alone", unsigned(id), unsigned(len));
}

// After the report: the shot, in its own POST, when there is RAM for it; one try only.
static bool startShot(bool reportWent) {
  if (noReports || !reportWent || !shotSlot.waiting()) return false;   // the site did not answer: it waits
  if (!shape.shot) {   // a light report's window: the next Check sends it
    say("remote: the shot waits for a Check (this window's report is light)");
    return false;
  }
  const uint32_t have = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 big = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!shotSlot.go(have, big)) {
    say("remote: the shot waits for a later window: internal RAM %u free, %u largest", unsigned(have), unsigned(big));
    return false;
  }
  const size_t cap = rdebug::kShotMax + rdebug::kShotMax / 64 + 512;
  if (!shotBody) shotBody = static_cast<char*>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!shotBody) return false;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Out o{shotBody, cap};
  o.f("{\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"fw\":\"%s\",\"via\":\"wifi\",\"items\":[{\"kind\":\"shot\","
      "\"summary\":\"screenshot 120x120 for command #%u\",\"data\":{",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], blorb::kFirmwareVersion, unsigned(shotSlot.id()));
  o.str("lines", shotText, shotSlot.bytes());
  o.drop();
  o.f("}}]}");
  if (o.full) {
    shotSlot.tried();
    dropShot();
    result(shotSlot.id(), false, "shot: did not fit its body");
    return false;
  }
  job.reset();
  say("remote: shot of %u bytes, alone: heap=%u big=%u before the task", unsigned(o.n), unsigned(have), unsigned(big));
  inWindow = 0;
  inReport = kReportMagic;   // a restart in it is remote debug's, never the update check's
  shotSlot.tried();          // one try, whatever comes of it
  if (job.start(kShotUrl, true, shotBody, o.n)) return true;
  inReport = 0;
  dropShot();
  result(shotSlot.id(), false, "shot: could not start (%s); dropped", job.error());
  return false;
}

static void shotArrived(bool done, const char* text, size_t len, const char* error) {
  inReport = 0;
  dropShot();
  if (!done) return result(shotSlot.id(), false, "shot: the upload failed (%s); dropped", error);
  const std::string_view reply(text, len);
  const size_t at = reply.find("\"ids\":[");   // {"ok":true,"ids":[<its device debug id>]}
  const unsigned id = at == std::string_view::npos ? 0 : unsigned(std::strtoul(text + at + 7, nullptr, 10));
  result(shotSlot.id(), true, "shot: 120x120 uploaded as #%u", id);
}

// One a loop pass, with the radio down; a reboot waits for dark and alone.
static void poll(uint32_t now) {
  if (!m) return;
  if (stateId && now - stateAt >= kStateSettleMs) {
    const size_t n = hw::logTee.since(stateMark, m->log, sizeof m->log);
    m->stateLen = masked(m->log, n, m->state, sizeof m->state);
    result(stateId, true, "%s: %u bytes captured", stateWhat, unsigned(m->stateLen));
    stateId = 0;
  }
  if (!m->nPending || window != Window::None || stateId) return;
  const rdebug::Command c = m->pending[0];
  if (c.verb == rdebug::Verb::Reboot && !(powerPolicy.low() && !ble.connected() && !updater.active() && !debugAsks()))
    return;   // waits for a quiet moment
  std::memmove(m->pending, m->pending + 1, sizeof(rdebug::Command) * (m->nPending - 1));
  --m->nPending;
  switch (c.verb) {
    case rdebug::Verb::Check:
      schedule.dueNow();
      return result(c.id, true, "check: due now (opens once dark and alone)");
    case rdebug::Verb::Interval:
      if (!schedule.setEvery(c.arg * 60000, now)) return result(c.id, false, "interval: out of bounds");
      return result(c.id, true, "interval: every %u min until a restart; next in %u min", unsigned(c.arg),
                    unsigned(schedule.dueIn(now) / 60000));
    case rdebug::Verb::State:
      stateMark = hw::logTee.mark();
      stateAt = now;
      stateId = c.id;
      std::snprintf(stateWhat, sizeof stateWhat, "state");
      remoteStateVerbs();
      return statusLine();
    case rdebug::Verb::Debug: {
      // Any of the cable's DEBUG lines (CLAUDE.md: one badge, three
      // transports). The owner's token is this transport's gate; past it the
      // badge refuses what Bluetooth's DEBUG refuses for a sealed dish, a
      // trial or an update (debugble::serialReach), a reboot that would not
      // wait for a quiet moment, and forgetting the last network it could
      // come home on. What it prints goes in the next report's state, masked.
      char verb[12] = "";
      std::sscanf(c.text, "%11s", verb);
      const debugble::Reach reach = debugble::serialReach(c.text);
      if (const std::optional<debugble::Refusal> no =
              debugble::refuseFor(reach, dish->sealed(), trial.onTrial(), updater.active()))
        return result(c.id, false, "debug %s: refused, %s", verb, no->text);
      if (const char* no = rdebug::debugRefusal(c.text, mem().networks.size()))
        return result(c.id, false, "debug %s: refused, %s", verb, no);
      stateMark = hw::logTee.mark();
      stateAt = now;
      stateId = c.id;
      std::snprintf(stateWhat, sizeof stateWhat, "debug %s", verb);
      return runDebug(c.text);
    }
    case rdebug::Verb::Shot:
      return shot(c.id);
    case rdebug::Verb::Reboot:
      rebootId = c.id;
      rebootMagic = kRebootMagic;
      say("remote %u: reboot", unsigned(c.id));
      return rebootNow(false);
    case rdebug::Verb::Marquee:
      showMarquee(c.text);
      return result(c.id, true, "marquee: %u characters shown", unsigned(std::strlen(c.text)));
    case rdebug::Verb::Unknown: return;
  }
}

}  // namespace rd
