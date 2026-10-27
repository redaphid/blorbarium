// Remote debug (docs/remote-debug.md): the badge's side. src/wifi_ota.h
// includes this inside namespace wififw, after its statics, so it is part of
// main.cpp's one translation unit and sees the window, the schedule and the
// update's state.
//
// Each Check or Forced window that reaches the site ends with one more POST,
// /api/badge/report: the firmware, the uptime and why the last boot ended, a
// crash's backtrace from the core dump, the update checks' state, the heap,
// the battery, the signal, the pet, the results of the last commands, and the
// last 8 KB of the serial log (hw/log_tee.h). The reply is the owner's queued
// commands (lib/rdebug: an allowlist, each id once), run once the window has
// closed, their results in the next report.
//
// Everything here is PSRAM, made at the first report: the body, the log's
// copy, the state capture, a screenshot. Internal RAM: these few words, and
// two RTC words for a remote reboot's result.
//
// Whatever goes wrong here only closes the window: a report that does not
// reach the site is sent again (newer) next time, its results with it.
namespace rd {

constexpr const char* kReportUrl = "https://blorb.hypnodroid.com/api/badge/report";
constexpr size_t kBodyBytes = 28 * 1024;   // all but a shot: the log's 8 KB escaped, the state's 6 KB escaped, the rest
constexpr size_t kStateBytes = 6 * 1024;
constexpr size_t kShotBytes = 180 * 1024;   // a 240x240 PackBits frame as the cable's shot lines, at worst
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
  char state[kStateBytes + 1];
  size_t stateLen = 0;
  bool crash = false;
  esp_core_dump_summary_t dump;
  char text[160];   // one escaped short string
};
static Mem* m = nullptr;
static char* body = nullptr;
static size_t bodyCap = 0;
static char* shotText = nullptr;   // the cable's shot lines, '\n' between
static size_t shotLen = 0;
static uint32_t lastId = 0;        // the newest command id seen this boot
static uint64_t stateMark = 0;
static uint32_t stateAt = 0, stateId = 0;
static uint32_t reports = 0, reportFails = 0;
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
};

static const char* build(size_t& len) {
  const size_t want = kBodyBytes + (shotText ? shotLen + shotLen / 64 + 64 : 0);
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
  if (m->stateLen) o.str("state", m->state, m->stateLen);
  if (shotText) o.str("shot", shotText, shotLen);
  const size_t logLen = hw::logTee.tail(m->log, sizeof m->log);
  o.str("log", m->log, logLen);
  o.drop();
  o.f("}");
  if (o.full) return nullptr;
  m->sentResults = m->nResults;
  len = o.n;
  return body;
}

// ---- the window's last step ------------------------------------------------------

// After the manifest (and the pet's lines): the report, then the window closes.
static bool start() {
  size_t len = 0;
  const char* b = build(len);
  if (!b) {
    say("remote: no report this time (no PSRAM, or it did not fit)");
    return false;
  }
  job.reset();
  say("remote: report of %u bytes%s: heap=%u big=%u before the task", unsigned(len), shotText ? " with a screenshot" : "",
      unsigned(ESP.getFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  if (job.start(kReportUrl, true, b, len)) return true;
  say("remote: the report could not start: %s", job.error());   // not the check's failure: the manifest was read
  return false;
}

// The reply: the report is home (what it carried is dropped), and the commands it brought queue.
static void arrived(bool done, const char* text, size_t len, const char* error) {
  if (!ready()) return;
  if (!done) {
    ++reportFails;
    m->sentResults = 0;
    return say("remote: the report did not go (%s); it goes again next time", error);
  }
  ++reports;
  std::memmove(m->results, m->results + m->sentResults, sizeof(Result) * (m->nResults - m->sentResults));
  m->nResults -= m->sentResults;
  m->sentResults = 0;
  m->stateLen = 0;
  if (shotText) heap_caps_free(shotText), shotText = nullptr, shotLen = 0;
  if (!rdebug::trustedReplyUrl(kReportUrl)) return;   // only the site, over HTTPS with its pinned roots, may ask
  rdebug::Command* got = m->got;   // PSRAM: a Command holds a marquee's text
  const size_t n = rdebug::parseCommands(std::string_view(text, len), lastId, got, kPending - m->nPending);
  for (size_t i = 0; i < n; ++i) {
    lastId = got[i].id;
    if (!got[i].ok) result(got[i].id, false, "%s: %s", rdebug::verbName(got[i].verb), got[i].why);
    else m->pending[m->nPending++] = got[i];
  }
  say("remote: report home; %u command%s", unsigned(n), n == 1 ? "" : "s");
}

// ---- the commands, once the window has closed ------------------------------------------

static void shot() {
  if (!shotText) shotText = static_cast<char*>(heap_caps_malloc(kShotBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!shotText) return void(shotLen = 0);
  shotLen = 0;
  paint::ShotEncoder enc(canvas);   // dark: nothing repaints the canvas while this runs (one pass)
  for (const char* line; (line = enc.next());) {
    const size_t k = std::strlen(line);
    if (shotLen + k + 2 > kShotBytes) {
      heap_caps_free(shotText), shotText = nullptr, shotLen = 0;
      return;
    }
    std::memcpy(shotText + shotLen, line, k);
    shotLen += k;
    shotText[shotLen++] = '\n';
  }
}

// One a loop pass, with the radio down; a reboot waits for dark and alone.
static void poll(uint32_t now) {
  if (!m) return;
  if (stateId && now - stateAt >= kStateSettleMs) {
    m->stateLen = hw::logTee.since(stateMark, m->state, kStateBytes);
    result(stateId, true, "state: %u bytes captured", unsigned(m->stateLen));
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
      remoteStateVerbs();
      return statusLine();
    case rdebug::Verb::Shot:
      shot();
      return shotText ? result(c.id, true, "shot: %u bytes of the screen's last frame", unsigned(shotLen))
                      : result(c.id, false, "shot: no PSRAM for it");
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
