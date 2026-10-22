// The firmware loop, and the only file that includes Arduino, LovyanGFX or
// NimBLE. sim/sim.cpp includes this file whole, so the simulator runs this
// very loop and draws with these very objects.
#include <Arduino.h>
#include <esp_mac.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "blorb/dish.h"
#include "blorb/founding.h"
#include "blorb/knocks.h"
#include "paint/duel.h"
#include "paint/phases.h"
#include "paint/shot.h"
#include "paint/sprite_pack.h"
#include "paint/tween.h"

#if defined(BADGE_BOARD_SIM)
#include "board_sim.h"
#include "mem_storage.h"
using PetStorage = blorbtest::MemStorage;   // every run founds the same pet: the goldens need it
#elif defined(BADGE_BOARD_LCD146)
#error "the 1.46's panel comes after build unit 20, which brings up the 1.28"
#else
#include <esp_heap_caps.h>
#include <soc/soc_memory_layout.h>
#include "hw/board_lcd128.h"
#include "hw/paint_core0.h"
#include "hw/storage_nvs_fs.h"
#include "hw/system_clock.h"
#include "hw/air_espnow.h"
#include "hw/net_faults.h"
#include "hw/link_nus.h"
#include "hw/ota_esp.h"
#include "hw/ota_pubkey.h"
#include "hw/home_store.h"
#include "ota/held_storage.h"
#include "ota/screen.h"
#include "ota/trial_guard.h"
#include "debugble/console.h"
#include <sdkconfig.h>
#if defined(CONFIG_PM_ENABLE)
#include <driver/uart.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#endif
using PetStorage = hw::NvsFsStorage;
#endif

#include "hw/feel.h"
#include "hw/home.h"
#include "hw/imu_qmi8658.h"
#include "hw/orient.h"
#include "hw/poses.h"
#include "hw/power.h"
#include "hw/turn_yes.h"
#include "debug_verbs.h"
#include "latency.h"

// The cable's last byte, for light sleep (main.cpp, "Low power"): a byte holds it off a while.
static uint32_t cableHeardAt = 0;
static inline void cableByte() { cableHeardAt = millis(); }

// The protocol over the serial line. A cable has no connect event, so the
// owner counts as near once a line has arrived. A `DEBUG ` line is held back
// from the protocol for the loop to run after the tick (src/debug_verbs.h).
class SerialLink : public blorb::Link {
  char line_[256];   // past the protocol's 200, so an overlong line still reads as overlong
  size_t len_ = 0;
  bool heard_ = false;
  // A few DEBUG lines queue, so a script's back-to-back lines are all run, in order
  // (one slot dropped all but the last). 128 bytes holds the longest, `DEBUG wifi
  // set "<32-byte SSID>" <64-byte password>` (src/wifi_ota.h). The queue is
  // made in PSRAM at the first DEBUG line: internal RAM is the scarce kind
  // (a duel's radio takes about 37 KB of it), and a cable is rare.
  static constexpr uint8_t kDebugQueue = 8;
  static constexpr size_t kDebugLine = 128;
  struct Queue {
    char lines[kDebugQueue][kDebugLine];
    char taken[kDebugLine];
  };
  Queue* q_ = nullptr;
  uint8_t debugHead_ = 0, debugTail_ = 0;

  bool queue() {
#if !defined(BADGE_BOARD_SIM)
    if (!q_) q_ = static_cast<Queue*>(heap_caps_calloc(1, sizeof(Queue), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    if (!q_) q_ = static_cast<Queue*>(std::calloc(1, sizeof(Queue)));
    return q_ != nullptr;
  }

 public:
  bool connected() override { return heard_; }
  const char* takeDebug() {
    if (debugHead_ == debugTail_ || !q_) return nullptr;
    std::memcpy(q_->taken, q_->lines[debugTail_++ % kDebugQueue], sizeof(q_->taken));
    return q_->taken;
  }
  std::optional<std::string_view> readLine() override {
    while (Serial.available() > 0) {
      const int c = Serial.read();
      cableByte();
      if (c == '\r') continue;
      if (c != '\n') {
        if (len_ < sizeof(line_)) line_[len_++] = char(c);
        continue;
      }
      heard_ = true;
      const size_t n = len_;
      len_ = 0;
      if (n > 6 && std::memcmp(line_, "DEBUG ", 6) == 0) {
        if (!queue()) continue;
        if (uint8_t(debugHead_ - debugTail_) >= kDebugQueue) ++debugTail_;   // full: the oldest goes
        char* d = q_->lines[debugHead_++ % kDebugQueue];
        const size_t k = std::min(n - 6, kDebugLine - 1);
        std::memcpy(d, line_ + 6, k);
        d[k] = 0;
        continue;
      }
      return std::string_view(line_, n);
    }
    return std::nullopt;
  }
  void writeLine(std::string_view s) override {
    Serial.write(s.data(), s.size());
    Serial.write("\n", 1);
  }
};

// A frame at most every kFrameMs. Awake, the board runs flat out (240 MHz, the
// panel's SPI at BLORB_LCD_SPI_MHZ) and draws up to 50 a second: he lives on
// the 100 ms ticks, and every frame between them draws him on his way from one
// to the next (paint/tween.h), so his motion, the hint filling, fight mode's
// line and the rings all move on the clock; a frame that changed nothing sends
// the panel nothing (boardPresent). Low power draws none. The simulator keeps
// the 25 fps its frame goldens were filmed at (--frame-ms 20 films the badge's 50).
#if defined(BADGE_BOARD_SIM)
static uint32_t kFrameMs = 40;
#else
static constexpr uint32_t kFrameMs = 20;
#endif

static PetStorage store;
#if defined(BADGE_BOARD_SIM)
static PetStorage& dishStore = store;
#else
static ota::HeldStorage dishStore(store);   // writes nothing while this image is on trial
#endif
static SerialLink phone;
#if defined(BADGE_BOARD_SIM)
static blorb::Link& host = phone;
#else
static hw::NusLink ble;
// Updates over the air (docs/ota.md). The session answers OTA lines before
// the Dish sees them; what it needs from the pet is DishOtaHost, below.
struct DishOtaHost : ota::Host {
  bool busy() override;
  bool save() override;
  uint16_t chunk() override { return ble.mtu() > 7 ? ble.mtu() - 7 : 16; }   // ATT header and the offset
  bool onTrial() override;
  bool handOver(ota::Version to, const ota::Token& token) override;
  void forgetHandover() override;
  ota::Confirm confirmTrial(const ota::Token& token) override;
  std::optional<ota::Version> rolledBack() override;
};
static DishOtaHost otaHost;
static hw::EspSlot otaSlot;
static hw::PacketRing otaPackets;
static hw::OtaData otaData(otaPackets, ble);
static ota::Session updater(otaSlot, otaHost, ble,
                            ota::Config{BOARD_NAME, *ota::parseVersion(blorb::kFirmwareVersion), hw::kOtaPublicKey});
// The unattended update's data comes from here (src/wifi_ota.h): never a
// Bluetooth connection's number, which counts up from 1.
static constexpr ota::Peer kWifiPeer = 0xffffffffu;
static hw::OtaLink otaLink(ble, updater);
static ota::Trial trial;
static hw::EspTrialPort trialPort;
static hw::TrialWatchdog trialWatchdog;
static hw::TwoLinks links(phone, otaLink);   // DEBUG lines stay on the cable: only SerialLink holds them back

// OTA's interlock, from the duel's side (docs/ota.md, "Other users of the
// radio"): no duel opens while an update session is active or this image is
// still on trial, so a trial image never writes a wound, a book entry or a
// friend egg a rollback would meet. Every call runs on the loop task.
// The radio's opens and refusals, for the debug link (drained with the Dish's lines).
static blorb::DiagLines radioDiag;
struct RadioGate : blorb::Air {
  blorb::Air* inner = nullptr;   // FaultAir, set by setup() once it is made
  const char* why = nullptr;     // the last open()'s refusal
  const char* logged = "";       // the last one logged, so a waiting retry each second logs once
  bool open() override;
  const char* refusal() const override { return why ? why : "radio_closed"; }
  void close() override { inner->close(); }
  bool isOpen() const override { return inner->isOpen(); }
  blorb::PeerAddr self() const override { return inner->self(); }
  void broadcast(const uint8_t* p, size_t n) override { inner->broadcast(p, n); }
  void send(const blorb::PeerAddr& to, const uint8_t* p, size_t n) override { inner->send(to, p, n); }
  std::optional<blorb::Datagram> receive() override { return inner->receive(); }
};
// Internal RAM is the scarce kind: a duel's radio takes about 37 KB of it and
// must find it free each time fight mode opens (#53). Objects only tasks touch
// (never an interrupt, and nothing in them is a lock or an atomic) can live in
// PSRAM. PSRAM joins the heap in initArduino(), after the static constructors
// have run, so such an object is made by setup()'s first lines (make()), not
// as a static: internal only if PSRAM is not there (`[hw] psram: ...` at boot
// says where each landed). After wifi-ota-2's a602752.
//
// #53's A/B builds, one variable each (all default to this commit's choice):
//   -DBLORB_PSRAM_OBJECTS=0   FaultAir and the latency recorder in internal RAM
//   -DBLORB_SERIAL_TX=2048    the cable's TX queue
//   -DBLORB_HEAP_BALLAST=N    N bytes of internal heap held from boot (to stand
//                             where main's heap stood without moving anything back)
//   -DBLORB_ROWHASH_PSRAM=0   the panel's chunk hashes in internal RAM (board_lcd128.h)
//   -DBLORB_AIR_TX_QDBM=52    the air's TX power, quarter dBm (air_espnow.h)
#ifndef BLORB_PSRAM_OBJECTS
#define BLORB_PSRAM_OBJECTS 1
#endif
#ifndef BLORB_SERIAL_TX
#define BLORB_SERIAL_TX 512
#endif
#ifndef BLORB_HEAP_BALLAST
#define BLORB_HEAP_BALLAST 0
#endif
template <class T>
class InPsram {
 public:
  template <class... A>
  void make(A&&... a) {
    if (p_) return;
    void* m = BLORB_PSRAM_OBJECTS ? heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    p_ = m ? new (m) T(std::forward<A>(a)...) : new T(std::forward<A>(a)...);
  }
  T& operator*() const { return *p_; }
  T* operator->() const { return p_; }
  T* get() const { return p_; }

 private:
  T* p_ = nullptr;
};
static hw::EspNowAir espNow;
// `DEBUG net ...` makes the room worse between the gate and the radio (hw/net_faults.h).
static InPsram<hw::FaultAir> faultAir;   // made by setup()
static RadioGate radioGate;
// Survives ESP.restart() (not a power cut): `DEBUG reboot quiet` boots with the radio blacked out
// from the first tick, so a soak sets its room before two badges can pair.
static constexpr uint32_t kQuietMagic = 0x51E7B007u;
RTC_NOINIT_ATTR static uint32_t quietBoot;
// On trial nothing he does is written, so the twists that write his lineage wait.
static ota::TrialGuard host(links, [] { return trial.onTrial(); });
// The debug link's unlock question (src/debug_ble.h) holds the button as the update's does.
static bool debugAsks();
static bool debugPress(uint32_t ms);
// Wi-Fi's question, "Join Wi-Fi? Turn me around!" (src/wifi_ota.h): a hand's yes before a phone gives it a network.
static bool wifiAsks();
static bool wifiPress(uint32_t ms);
static bool wifiQuietBoot();
#endif
static std::optional<blorb::Dish> dish;    // built in setup(), once storage is up
// The duel radio the Dish is handed (DishOptions::air): ESP-NOW behind
// RadioGate on the board; the simulator sets its own here before setup(),
// since it includes this file whole.
static blorb::Air* duelAir = nullptr;
static BoardDisplay display;
static paint::Canvas240 canvas;
static bool imuOk = false, tapReady = false;
static uint32_t lastSample = 0, lastFrame = 0;
static blorb::BodySample lastBody;
// Low power after BLORB_IDLE_SECONDS (600) with no user input (src/hw/power.h, docs/power.md).
static power::Policy powerPolicy;
// Which way up is home on this badge: the build's ORIENT_HOME until setup()
// reads the badge's own from NVS (src/hw/home.h), and `DEBUG home` sets it.
static volatile uint8_t homeRot = ORIENT_HOME;
static const char* homeWhy = "built";
static uint32_t framesDrawn = 0;
#if !defined(BADGE_BOARD_SIM)
static uint32_t cpuMaxMhz = 240;   // what the clock is set to run at (with DFS it dips below)
static void pmBegin();             // below, with the rest of low power
static const char* sleepState();
static uint32_t sleptMs();

// What frames cost, since `DEBUG fps` last asked (or boot): drawing the canvas,
// sending the panel what changed, and the worst whole frame.
//   DEBUG fps      [fps] fps=48.6 frames=486 over_ms=10001 draw_ms=11.9 push_ms=1.6 worst_ms=33 rows=21 ...
// rows= is the rows sent a frame (240 is a whole frame), px= the pixels (57,600); cpu_now= the clock as
// it runs, against cpu=, the clock it was set to. fps=, frames= and the ms are
// the frames drawn, each one that changed; same= the frames not drawn because
// nothing in them had (framememo). A second line splits a
// frame's time by phase, ms a frame (paint/phases.h):
//   [fps] phases look=0.9 bg=4.1 fig=6.0 rim=0.2 text=0.0 rings=0.0 mask=0.4 hint=0.3 over=0.5 push=1.6 wait=8.2
// look= is the Dish working out his Appearance and memo= the overlays and the
// frame memo's test (draw_ms counts both), over= what
// is drawn over the dish after it (the update's screen, the debug link's
// mark, the idle bar), wait= the rest of the loop between frames. push= is the
// loop's part of sending (the row hashes, the copy to the send task, and any
// wait for the last frame's rows still going out), send= the send task's time
// on the bus, on core 0 (async; sync: push= is all of it and send= is 0).
//   DEBUG fps bench    what the canvas's memory and the renderer's parts cost, once each
static blorb::Appearance shown();                          // below
static void drawFrame(const blorb::Appearance& a, uint32_t now);
namespace fps {
static uint32_t since = 0, frames = 0, worstUs = 0, rows0 = 0, px0 = 0, send0 = 0, sames = 0;
static void same() { ++sames; }
static uint64_t lookUs = 0, memoUs = 0, drawUs = 0, pushUs = 0;
static uint32_t now() { return uint32_t(micros()); }
// look: the Dish's Appearance (the engine's), memo: the overlays and whether
// the frame changed, draw: the canvas, push: the panel.
static void note(uint32_t look, uint32_t memo, uint32_t draw, uint32_t push) {
  look += memo;   // draw_ms and worst count both, as before
  ++frames, lookUs += look, memoUs += memo, drawUs += draw, pushUs += push;
  if (look + draw + push > worstUs) worstUs = look + draw + push;
}
// "a.b": tenths of a ms from a total of us over n.
static void ms(char* out, size_t cap, uint64_t us, uint32_t n) {
  const uint32_t t = uint32_t(us / n / 100);
  std::snprintf(out, cap, "%u.%u", unsigned(t / 10), unsigned(t % 10));
}
static void report() {
  const uint32_t t = millis(), over = t - since, n = frames ? frames : 1, rows = boardRowsSent() - rows0;
  const uint32_t tenths = over ? uint32_t(uint64_t(frames) * 10000 / over) : 0;
  const uint32_t draw = uint32_t((lookUs + drawUs) / n), push = uint32_t(pushUs / n);
  Serial.printf("[fps] fps=%u.%u frames=%u over_ms=%u draw_ms=%u.%u push_ms=%u.%u worst_ms=%u rows=%u cpu=%u cpu_now=%u "
                "spi_mhz=%u.%u frame_ms=%u power=%s same=%u cores=%d px=%u\n",
                unsigned(tenths / 10), unsigned(tenths % 10), unsigned(frames), unsigned(over), unsigned(draw / 1000),
                unsigned(draw % 1000 / 100), unsigned(push / 1000), unsigned(push % 1000 / 100), unsigned(worstUs / 1000),
                unsigned(rows / n), unsigned(cpuMaxMhz), unsigned(getCpuFrequencyMhz()), unsigned(display.writeKhz() / 1000),
                unsigned(display.writeKhz() % 1000 / 100), unsigned(kFrameMs), powerPolicy.low() ? "low" : "full", unsigned(sames),
                paintcore0::on ? 2 : 1, unsigned((boardPixelsSent() - px0) / n));
  using paint::Phase;
  const uint32_t* spent = paint::phaseClock.spent;
  uint64_t painted = 0;
  for (int i = 0; i < int(Phase::Count); ++i) painted += spent[i];
  const uint64_t loopUs = uint64_t(over) * 1000;
  char f[13][12];
  const Phase order[] = {Phase::Background, Phase::Figure, Phase::Rim, Phase::Text, Phase::Rings, Phase::Mask, Phase::Hint};
  ms(f[0], sizeof f[0], lookUs - memoUs, n);
  ms(f[12], sizeof f[12], memoUs, n);
  for (int i = 0; i < 7; ++i) ms(f[1 + i], sizeof f[1 + i], spent[uint8_t(order[i])], n);
  ms(f[8], sizeof f[8], drawUs > painted ? drawUs - painted : 0, n);
  ms(f[9], sizeof f[9], pushUs, n);
  const uint64_t busy = lookUs + drawUs + pushUs;
  ms(f[10], sizeof f[10], loopUs > busy ? loopUs - busy : 0, n);
  ms(f[11], sizeof f[11], boardSendUs() - send0, n);
  Serial.printf("[fps] phases look=%s memo=%s bg=%s fig=%s rim=%s text=%s rings=%s mask=%s hint=%s over=%s push=%s wait=%s "
                "send=%s (%s)\n",
                f[0], f[12], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11],
                boardSendAsync() ? "async" : "sync");
  // What each task's stack never used yet, bytes (sized from these, #53).
  Serial.printf("[fps] stack paint0=%u lcdsend=%u heap=%u big=%u\n", unsigned(paintcore0::stackFree()),
                unsigned(boardSendStackFree()), unsigned(ESP.getFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  since = t, frames = 0, sames = 0, worstUs = 0, rows0 = boardRowsSent(), px0 = boardPixelsSent(), lookUs = memoUs = drawUs = pushUs = 0, send0 = boardSendUs();
  for (uint32_t& p : paint::phaseClock.spent) p = 0;
}
// `DEBUG fps bench`: the canvas's memory (a whole-canvas memset, a read of
// every word) and the frame shown now drawn ten times, by phase, in us. The
// next frame repaints the panel whole, so what this leaves on the canvas never shows.
static void bench() {
  constexpr int kReps = 10;
  uint32_t t0 = now();
  for (int i = 0; i < kReps; ++i) std::memset(canvas.px, i, sizeof canvas.px);
  const uint32_t setUs = (now() - t0) / kReps;
  t0 = now();
  uint32_t sum = 0;
  for (int i = 0; i < kReps; ++i) {
    const volatile uint32_t* w = reinterpret_cast<const volatile uint32_t*>(canvas.px);
    for (size_t k = 0; k < sizeof canvas.px / 4; ++k) sum += w[k];
  }
  const uint32_t readUs = (now() - t0) / kReps;
  for (uint32_t& p : paint::phaseClock.spent) p = 0;
  t0 = now();
  for (int i = 0; i < kReps; ++i) (void)shown();
  const uint32_t lookUs = (now() - t0) / kReps;
  const blorb::Appearance a = shown();
  t0 = now();
  for (int i = 0; i < kReps; ++i) drawFrame(a, millis());
  const uint32_t drawUs = (now() - t0) / kReps;
  const uint32_t* spent = paint::phaseClock.spent;
  using paint::Phase;
  Serial.printf("[fps] bench canvas=%s memset_us=%u read_us=%u (%u) look_us=%u draw_us=%u bg=%u fig=%u rim=%u text=%u rings=%u "
                "mask=%u hint=%u\n",
                esp_ptr_internal(canvas.px) ? "internal" : "external", unsigned(setUs), unsigned(readUs), unsigned(sum & 1),
                unsigned(lookUs), unsigned(drawUs), unsigned(spent[uint8_t(Phase::Background)] / kReps),
                unsigned(spent[uint8_t(Phase::Figure)] / kReps), unsigned(spent[uint8_t(Phase::Rim)] / kReps),
                unsigned(spent[uint8_t(Phase::Text)] / kReps), unsigned(spent[uint8_t(Phase::Rings)] / kReps),
                unsigned(spent[uint8_t(Phase::Mask)] / kReps), unsigned(spent[uint8_t(Phase::Hint)] / kReps));
  for (uint32_t& p : paint::phaseClock.spent) p = 0;
  boardRepaintAll();
}
}  // namespace fps

// A frame drawn from the same inputs as the last one drawn is the same bytes
// (paint::draw is pure in the Appearance, and the clock only moves fight
// mode's line and pulse, the no-fight strip and the amber's words), so it is
// not drawn or sent: between his 100 ms ticks, most of them. Not while an
// overlay (the update's screen, the debug unlock) or a duel's scene (not in
// the Appearance) is up, nor when the panel needs every row again.
namespace framememo {
static blorb::Appearance last;
static uint16_t lastIdle = 0;
static bool valid = false;
static bool same(const blorb::Appearance& a, uint16_t idleBar, bool overlay) {
  if (!valid || overlay || boardStale() || a.dueling || a.lobby || a.sealed || a.noFight != blorb::NoFight::None) return false;
  // Byte for byte: equal bytes are equal values (padding can only make two equal frames look different).
  return idleBar == lastIdle && std::memcmp(static_cast<const void*>(&a), static_cast<const void*>(&last), sizeof a) == 0;
}
static void drew(const blorb::Appearance& a, uint16_t idleBar, bool overlay) {
  std::memcpy(static_cast<void*>(&last), static_cast<const void*>(&a), sizeof a);
  lastIdle = idleBar;
  valid = !overlay;   // the next frame without the overlay must draw over it
}
}  // namespace framememo

// `DEBUG lcd [MHz]`: the panel's SPI write clock until reset (BLORB_LCD_SPI_MHZ is the default).
// `DEBUG lcd sync|async`: send the rows from the loop, or from the send task on core 0 (the default).
static void lcdVerb(const char* a) {
  if (*a) {
    const long mhz = std::strtol(a, nullptr, 10);
    if (mhz < 1 || mhz > 80) return (void)Serial.printf("[hw] want: lcd <MHz 1..80>\n");
    boardPanelIdle();
    display.setWriteMhz(uint32_t(mhz));
    boardRepaintAll();   // the next frame whole, at the new clock: what to look at
  } else if (std::strcmp(a, "sync") == 0 || std::strcmp(a, "async") == 0) {
    boardSetSendAsync(a[0] == 'a');
  }
  const uint32_t khz = display.writeKhz();   // what the divider gives: `lcd 30` runs at 26.6
  Serial.printf("[hw] lcd spi_mhz=%u.%u (built %u) send=%s\n", unsigned(khz / 1000), unsigned(khz % 1000 / 100),
                unsigned(BLORB_LCD_SPI_MHZ), boardSendAsync() ? "async" : "sync");
}
#endif

// `DEBUG species <slug|id|off>` and the sim's --species pin the species drawn, to look at
// every pack on one board. RAM only: the pet and its save never see it.
static std::optional<blorb::SpeciesId> pinnedSpecies;

static blorb::Appearance shown() {
  blorb::Appearance a = dish->appearance();
  if (pinnedSpecies) a.species = *pinnedSpecies;
#if !defined(BADGE_BOARD_SIM)
  // The update's question owns the button (readBody), so it owns the hint too.
  if (updater.asking() || debugAsks() || wifiAsks()) a.boot = blorb::bootHint(blorb::BootContext::OtaQuestion, nullptr);
#endif
  return a;
}

// Frames between ticks (paint/tween.h): he moves every frame, not on the 100 ms
// ticks, and his animations run at paint::kScreenSpeed. `DEBUG smooth off` (the
// sim's --steps) draws each tick's own frame until the next, as before, to compare.
static bool smoothFrames = true;
static uint16_t tickShareAt(uint32_t now) { return paint::tickShare(now - dish->lastTickMs()); }

// The look `now` drawn as the frame `now` ms is: between the look at the tick
// before (kept here) and the look at this tick, `shown()` every frame. A frame
// after a jump of more than one tick (a warp, a slow save) has no tick before
// to come from, so it starts again from where he is.
static blorb::Appearance onScreen(const blorb::Appearance& look, uint32_t now) {
  static blorb::Appearance was, last;
  static uint32_t lastTick = 0;
  static bool seen = false;
  const uint32_t tick = dish->tickCount();
  if (!seen || tick != lastTick) {
    was = seen && tick == lastTick + 1 ? last : look;
    lastTick = tick, seen = true;
  }
  last = look;
  if (!smoothFrames) return look;
  return paint::between(was, look, dish->lastTickMs(), now);
}

// One frame of the dish, or of the duel while one has a scene. In the common
// path, not behind a board, because the simulator draws (and films) exactly
// what this draws. The scene is static: it holds two Appearances, which the
// loop task's stack should not carry every frame.
static paint::DuelScene duelScene;
static void drawFrame(const blorb::Appearance& a, uint32_t now) {
  // Each new thought once on serial, so a host can hear what he says.
  static char saidLast[sizeof a.line] = "";
  if (a.thinking && a.line[0] && std::strncmp(saidLast, a.line, sizeof saidLast) != 0) {
    std::memcpy(saidLast, a.line, sizeof saidLast);
    Serial.printf("[say] %s\n", a.line);
  }
  const paint::RimHint hint = paint::rimHint(a.boot);
  if (a.dueling) {
    // Fight mode keeps the dish, with its line and pulse (paint::draw); so does
    // a locked pair until both have heard each other, so no badge shows a fight alone.
    const blorb::DuelView v = dish->duelView();
    if (v.phase != blorb::DuelPhase::Lobby && v.phase != blorb::DuelPhase::Greeting) {
      duelScene = paint::sceneOf(v, a);
      duelScene.boot = hint;
      if (smoothFrames) paint::between(duelScene, tickShareAt(now));   // the act's motion between its ticks
      paint::drawDuel(duelScene, paint::packFor(a.species), paint::packFor(v.them.species), canvas, now);
      return;
    }
  }
  paint::draw(a, paint::packFor(a.species), canvas, now);
  paint::drawBootHint(canvas, hint);   // what BOOT does now, in every state
}

// The body is sensed at 50 Hz on a task of its own and replayed on the loop.
// A frame takes about 190 ms to draw (measured on the board, 0.6.0), so a
// loop that sampled between frames sensed at 5 Hz: a quick press or half a
// double press fell between samples, and the tilt smoothing, tuned per
// 50 Hz sample, took 3 s to aim. Each sample carries its own millis(), so
// every detector times gestures as designed however long a frame takes.
// The task is the IMU's only reader; the Dish is still only the loop's.
namespace bodyq {
struct Timed { blorb::BodySample s; uint32_t ms; bool remote; uint32_t us = 0, peakUs = 0; };   // us: its read, micros(); peakUs: its peak's
constexpr uint8_t kRing = 128;   // 2.5 s of samples: only a save or warp longer than that drops the newest, never blocks
static Timed ring[kRing];
static uint8_t head = 0, tail = 0;
#if !defined(BADGE_BOARD_SIM)
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif

// `DEBUG press` and `DEBUG double` hold BOOT down over these windows, read by
// the sense task, so the cable's presses are sampled like a thumb's.
struct Window { volatile uint32_t at = 0, ms = 0; };
static Window presses[2];
// The debug link's PRESS and DOUBLE: the pet feels them, but no question on
// the screen ever takes them for a yes (petFeels).
static Window remote[2];
static bool pressed(const Window (&ws)[2], uint32_t ms) {
  for (const Window& w : ws)
    if (ms - w.at < w.ms) return true;
  return false;
}

#if !defined(BADGE_BOARD_SIM)
// `DEBUG hang` holds the raw IMU sample upside down from home (USB-C down:
// ORIENT_HOME's opposite, rotation 0) over one window, then home over the
// next, so the real orientation settle and HangDetector decide, as with a
// kid's hands on a case that covers BOOT. Wobble adds a small hand's tremor
// and an occasional jolt.
static volatile uint32_t hangAt = 0, hangMs = 0, rightMs = 0, cradleAt = 0, cradleMs = 0;
static volatile bool hangWobble = false;
// `DEBUG side` reuses the hang's windows with another pose: the raw sample's
// gravity along +x (rotation 1) or -x (rotation 3) instead of +y.
static volatile int8_t hangSide = 0;   // 0: upside down; +1/-1: on a side
// `DEBUG knock [1|2]`: one knock, or two 300 ms apart, as a knuckle jolts the
// raw sample through the face (blorb::KnockSense decides, as for a hand).
static volatile uint8_t knockCode = 0;
// `DEBUG shake`: a hand starts moving (so no knock is heard in it), then
// swings hard both ways, 100 ms a swing: MotionDetector's five jolts.
static volatile uint32_t shakeAt = 0;
static volatile bool shaking = false;
static volatile uint32_t knockAt = 0;
static bool posed(uint32_t ms, blorb::BodySample& s) {
  if (knockCode && int32_t(ms - knockAt) >= 0) {
    const uint32_t since = ms - knockAt;
    for (uint8_t i = 0; i < knockCode; ++i) {
      const uint32_t off = since - i * 300u;
      if (off < 20) s.az = int16_t(s.az + 1800);
      else if (off < 40) s.az = int16_t(s.az - 1800);
    }
    if (since > 300u * knockCode + 100) knockCode = 0;
  }
  if (shaking && int32_t(ms - shakeAt) >= 0) {
    const uint32_t t = ms - shakeAt;
    if (t < 300) s.ax = int16_t(s.ax + (t / 40 % 2 ? -400 : 400));
    else if (t < 1100) s.ax = int16_t(s.ax + ((t - 300) / 100 % 2 ? -1100 : 1100));
    else shaking = false;
  }
  // `DEBUG cradle`: in a hand, tipped back a little and still (HeldDetector, then a Cradle at 3 s).
  if (cradleMs && ms - cradleAt < cradleMs) {
    const uint32_t r = esp_random();
    s.ax = int16_t(int32_t(r % 31) - 15), s.ay = int16_t((homeRot == 2 ? 450 : -450) + int32_t((r >> 8) % 31) - 15), s.az = 890;
    return true;
  }
  if (!hangMs && !rightMs) return false;
  const uint32_t since = ms - hangAt;
  if (since >= hangMs + rightMs) return false;
  const bool down = since < hangMs;
  // Gravity along +y in the IMU's frame settles rotation 2, along -y rotation 0
  // (orient.h with ORIENT_R0 = 1): home is USB-C up (ORIENT_HOME), the hang its opposite.
  const int16_t homeY = homeRot == 2 ? 1000 : -1000;
  s.ax = 0, s.ay = int16_t(down ? -homeY : homeY), s.az = 0;
  if (down && hangSide) s.ax = int16_t(hangSide * 1000), s.ay = 0;
  if (hangWobble) {
    const uint32_t r = esp_random();
    s.ax = int16_t(int32_t(r % 241) - 120);
    s.az = int16_t(int32_t((r >> 8) % 241) - 120);
    if ((r >> 16) % 25 == 0) s.ay = int16_t(s.ay + (s.ay > 0 ? 500 : -500));   // a jolt, about 2 a second
  }
  return true;
}
#endif
}  // namespace bodyq

// `DEBUG lean <aim>` tilts the screen-space sample toward one rim edge, as a
// hand would, from leanAt for leanMs.
static uint32_t leanAt = 0, leanMs = 0;
static int8_t leanAim = -1;

// Half a g along the aimed axis, the rest down, so the tilt detector takes it
// as steady: aim 0 raises +x, 1 raises -x, 2 lowers +y, 3 raises +y.
static void applyLean(blorb::BodySample& s, uint32_t now) {
  if (leanAim < 0 || now - leanAt >= leanMs) return;
  s.ax = leanAim == 0 ? 500 : leanAim == 1 ? -500 : 0;
  s.ay = leanAim == 2 ? -500 : leanAim == 3 ? 500 : 0;
  s.az = 866;
}

// `remoteOnly`: the button is down only by the debug link's hand.
static blorb::BodySample readBody(uint32_t ms, bool* remoteOnly = nullptr) {
  blorb::BodySample s;
  if (!imuOk || !imu::read(s)) {
    s = blorb::BodySample{};
    s.az = 1000;   // no IMU: lying flat and still, so no gesture fires by accident
  }
#if !defined(BADGE_BOARD_SIM)
  bodyq::posed(ms, s);
#endif
  const bool hand = boardButtonDown() || bodyq::pressed(bodyq::presses, ms);
  const bool far = bodyq::pressed(bodyq::remote, ms);
  s.buttonDown = hand || far;
  if (remoteOnly) *remoteOnly = far && !hand;
  return s;
}

#if !defined(BADGE_BOARD_SIM)
namespace bodyq {
// Awake, the IMU is read every kSubMs (it measures at 250 Hz) and every
// fifth read is handed on, carrying the farthest of the five as its peak
// (BodySample::peakAx..): a knock's peak lasts a few milliseconds, and at 50 Hz
// most of one on a badge lying on a table fell between samples. In low power,
// one read per sample, as before, so the chip can light-sleep.
static volatile bool fastSense = true;   // the loop: !powerPolicy.low()
constexpr uint32_t kSubMs = 4;
static void sense(void*) {
  TickType_t wake = xTaskGetTickCount();
  blorb::BodySample handed{};
  bool primed = false, far = false;
  int64_t best = -1;
  int16_t pk[3] = {};
  uint32_t pkUs = 0;
  uint8_t sub = 0;
  for (;;) {
    const bool fast = fastSense;
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(fast ? kSubMs : blorb::kSampleMs));
    const uint32_t ms = millis(), us = micros();
    bool farNow = false;
    blorb::BodySample s = readBody(ms, &farNow);
    far = far || farNow;
    const int64_t dx = s.ax - handed.ax, dy = s.ay - handed.ay, dz = s.az - handed.az;
    const int64_t d = dx * dx + dy * dy + dz * dz;
    if (d > best) best = d, pk[0] = s.ax, pk[1] = s.ay, pk[2] = s.az, pkUs = us;
    if (fast && ++sub < blorb::kSampleMs / kSubMs) continue;
    sub = 0;
    s.hasPeak = primed;
    s.peakAx = pk[0], s.peakAy = pk[1], s.peakAz = pk[2];
    portENTER_CRITICAL(&mux);
    if (uint8_t(head - tail) < kRing) ring[head++ % kRing] = Timed{s, ms, far, us, pkUs};
    portEXIT_CRITICAL(&mux);
    handed = s, primed = true, best = -1, far = false;
  }
}

static std::optional<Timed> take() {
  std::optional<Timed> t;
  portENTER_CRITICAL(&mux);
  if (head != tail) t = ring[tail++ % kRing];
  portEXIT_CRITICAL(&mux);
  return t;
}
}  // namespace bodyq
#endif

#if !defined(BADGE_BOARD_SIM)
// `DEBUG trace <seconds>`: every 50 Hz sample on the cable as the IMU read it,
// with what the shell and the detectors made of it, to set the gestures
// against real hands (tools/imu_record.py). 0 stops it.
//   [imu] ms ax ay az tap rot side inverted sideHeld button pk peakAx peakAy peakAz aim a busy b ring r
//         (aim: the aim the duel reads now; busy: a knock burst under way; ring: a knock ringing)
//   [stim] ms name        each body stimulus, on the sample that fired it
static uint32_t traceUntil = 0;
static void traceSample(const blorb::BodySample& raw, const blorb::BodySample& screen, uint8_t rot, uint32_t ms) {
  if (!traceUntil || int32_t(traceUntil - millis()) <= 0) return;
  Serial.printf("[imu] %u %d %d %d %u %u %d %d %d %d pk %d %d %d aim %d busy %d ring %d\n", unsigned(ms), raw.ax, raw.ay,
                raw.az, unsigned(raw.tapCode), unsigned(rot), int(screen.sideDown), int(screen.invertedDown),
                int(dish->sideHolding()), int(raw.buttonDown), raw.peakAx, raw.peakAy, raw.peakAz, int(dish->aimNow()),
                int(screen.knocking), int(screen.knockRinging));
  const uint64_t fired = dish->firedLastSample();
  for (const blorb::StimInfo& st : blorb::STIMULI)
    if (st.id.v < 64 && (fired >> st.id.v & 1)) Serial.printf("[stim] %u %s\n", unsigned(ms), st.name);
}
static void traceFor(const char* a) {
  const long s = std::strtol(a, nullptr, 10);
  traceUntil = s > 0 ? millis() + uint32_t(s > 3600 ? 3600 : s) * 1000 : 0;
  Serial.printf("[hw] debug trace %ld s\n", s > 0 ? s : 0L);
}
#endif

// While the dish asks about an update, the button answers it, and the pet
// feels neither that press nor the rest of that hold. A cable's DEBUG press
// answers too: the cable is a hand on the dish. The debug link's press
// (`remote`) never answers: the pet feels it only while no question shows.
static bool petFeels(bool down, bool remote) {
#if !defined(BADGE_BOARD_SIM)
  static bool wasDown = false, otaOwns = false;
  if (remote) {
    wasDown = down;
    otaOwns = false;
    return down && !updater.asking() && !debugAsks() && !wifiAsks();
  }
  const bool edge = down && !wasDown;
  if (updater.asking() && edge) updater.press(millis());
  const bool debugTook = !updater.asking() && edge && debugPress(millis());   // the debug unlock's question
  const bool wifiTook = !updater.asking() && !debugTook && edge && wifiPress(millis());   // Wi-Fi's
  otaOwns = updater.asking() || debugAsks() || wifiAsks() || debugTook || wifiTook || (otaOwns && down);
  wasDown = down;
  return down && !otaOwns;
#else
  (void)remote;
  return down;
#endif
}

// The knocks, the power policy's input, the poses and the turn into the
// screen's frame, for every sample (hw/feel.h, pure so test_feeding replays it).
static hw::Feel shell{ORIENT_R0, homeRot, powerPolicy};

#if !defined(BADGE_BOARD_SIM)
// How long each control takes to show, stage by stage (src/latency.h):
//   DEBUG latency         [lat] <control> n= words= silent= noack= seen=p50/p95/max ... panel=...  (ms after In)
//   DEBUG latency last    [lat] last <control> seen=... panel=...   the newest of each
//   DEBUG latency clear   starts over
// A few KB and a few us a sample, so it is always on.
namespace lat {
static InPsram<latency::Recorder> rec;   // made by setup()
static latency::Watch watch;
static uint32_t readUs = 0, peakUs = 0;   // the sample feel() is on: its read, and its peak's
static void felt(const hw::Felt& f, bool button, bool woke) {
  const uint64_t fired = dish->firedLastSample();
  const auto has = [&](blorb::StimId id) { return id.v < 64 && (fired >> id.v & 1); };
  latency::Sample s;
  s.readUs = readUs, s.peakUs = peakUs, s.seenUs = micros();
  s.button = button;
  s.knockWaiting = f.screen.knockWaiting, s.knockKnock = f.screen.knockKnock;
  s.rawPose = hw::Poses::raw(f.screen);
  s.rawAim = latency::rawAim(f.screen.ax, f.screen.ay, f.screen.az);
  s.sideHolding = dish->sideHolding(), s.hangCharging = dish->hangCharging(), s.aimNow = dish->aimNow();
  s.woke = woke;
  s.press = has(blorb::stim::button), s.dbl = has(blorb::stim::button_double), s.hold = has(blorb::stim::button_hold);
  s.hang = has(blorb::stim::hang), s.shake = has(blorb::stim::shake), s.knock = has(blorb::stim::knock);
  s.knockKnockWord = has(blorb::stim::double_knock);
  watch.sample(s, *rec);
}
// What a frame acknowledges before the engine has: a bit per control.
static uint32_t acks(const blorb::Appearance& a) {
  using latency::Ctl;
  using latency::flag;
  uint32_t m = 0;
  if (a.feed.heardMs <= 300) m |= flag(Ctl::KnockKnock) | flag(Ctl::Press) | flag(Ctl::Double);   // the rim's flash (feed_show.h)
  if (a.boot.knockWait) m |= flag(Ctl::Knock);                                                 // "one more"
  if (a.boot.holdFill || a.boot.holdVoid) m |= flag(Ctl::Side) | flag(Ctl::Hold) | flag(Ctl::Press);   // the hold ring
  if (a.hangCharge > blorb::Fx::zero() || a.boot.doubleFill) m |= flag(Ctl::Hang) | flag(Ctl::Double);   // the charge, the bars
  if (rec->isOpen(Ctl::Tilt) && a.dueling && watch.tiltTarget() >= 0 && dish->duelView().aim == watch.tiltTarget())
    m |= flag(Ctl::Tilt);   // the duel's aimed action
  return m;
}
// After every frame handed to the panel: seqBefore is boardFrameSeq() before it.
static void framed(const blorb::Appearance& a, uint32_t seqBefore) {
  const uint32_t seq = boardFrameSeq();
  rec->framed(micros(), seq != seqBefore ? seq : 0, acks(a));
}
// Every loop: the rows the send task has put on the panel, and events open too long.
static void poll() {
  static uint32_t polled = 0;   // each frame's own time, though the loop looks only once a pass
  const uint32_t seq = boardPanelDoneSeq();
  for (uint32_t k = seq - polled > 8 ? seq - 7 : polled + 1; int32_t(seq - k) >= 0; ++k) rec->panelDone(k, boardPanelDoneAt(k));
  polled = seq;
  rec->expire(micros());
}
static void verb(const char* a) {
  if (!std::strcmp(a, "clear")) {
    rec->clear();
    Serial.printf("[lat] cleared\n");
    return;
  }
  const bool last = !std::strcmp(a, "last");
  char line[400];
  for (uint8_t i = 0; i < latency::kCtls; ++i)
    if (last ? rec->last(i, line, sizeof line) : (rec->count(latency::Ctl(i)) && rec->line(i, line, sizeof line)))
      Serial.printf("[lat] %s\n", line);
  Serial.printf("[lat] done\n");
}
}  // namespace lat
#endif
static void feel(const blorb::BodySample& sensed, uint32_t ms, bool remote = false) {
#if !defined(BADGE_BOARD_SIM)
  const bool asking = updater.asking() || debugAsks() || wifiAsks();   // a question owns BOOT, and its twin with it
#else
  constexpr bool asking = false;
#endif
  const bool wasLow = powerPolicy.low();
  hw::Felt f = shell.sample(sensed, ms, asking);
  lastBody = f.body;
  // The screen no longer turns with the badge (owner, 2026-10-19: a turning
  // screen turned the duel's aim with it, so no attack could be picked). The
  // panel stays at home (USB-C up) and the engine feels the badge in that one
  // frame: left is left, always. The settled rotation is still read, for the
  // poses (side hold, hang) and the update's turn.
  if (display.getRotation() != homeRot) {
    boardPanelIdle();
    display.setRotation(homeRot);
    boardRepaintAll();   // the rows the panel holds were sent the other way round
  }
#if !defined(BADGE_BOARD_SIM)
  // "Turn me around!": a turn answers the update's question as a press does.
  // The debug link's HANG turns it too, but HANG is Harm: it needed a hand's
  // press to unlock, so no phone says yes for itself.
  static hw::TurnYes turnYes;
  if (turnYes.sample(updater.asking() || wifiAsks(), f.rot, millis())) {
    Serial.printf("[%s] turned: yes (rot=%u)\n", updater.asking() ? "ota" : "wifi", unsigned(f.rot));
    if (updater.asking()) updater.press(millis());
    else wifiPress(millis());
  }
#endif
  applyLean(f.screen, ms);
  f.screen.buttonDown = petFeels(f.screen.buttonDown, remote);
  dish->sample(f.screen, ms);
#if !defined(BADGE_BOARD_SIM)
  traceSample(f.body, f.screen, f.rot, ms);
  lat::felt(f, sensed.buttonDown, wasLow && !powerPolicy.low());
#else
  (void)wasLow;
#endif
}

// The engine's half of the loop: sense, then run whatever 100 ms ticks are
// due. The simulator's warp calls this with no frames between.
static void step(uint32_t now) {
#if !defined(BADGE_BOARD_SIM)
  const uint32_t ahead = now - millis();   // a DEBUG warp runs the loop's clock ahead of millis()
  while (std::optional<bodyq::Timed> t = bodyq::take()) {
    lat::readUs = t->us, lat::peakUs = t->s.hasPeak ? t->peakUs : t->us;
    feel(t->s, t->ms + ahead, t->remote);
  }
#else
  if (now - lastSample >= blorb::kSampleMs) {
    feel(readBody(now), now);
    lastSample = now;
  }
#endif
  [[maybe_unused]] const uint32_t ticks = dish->tickCount();
  dish->tick(now, host);
#if !defined(BADGE_BOARD_SIM)
  if (dish->tickCount() != ticks) lat::rec->ticked(micros());   // what spoke is taken
#endif
}

#if !defined(BADGE_BOARD_SIM)
// The loop task runs the Dish. The engine reached 4,816 B on x86-64 (DESIGN.md
// section 8); this is the starting size until the board's high-water mark sets it.
static constexpr size_t kDishStackBytes = 16 * 1024;
SET_LOOP_TASK_STACK_SIZE(kDishStackBytes);

// Diagnostics. Every line starts "[hw] " so nobody takes it for the protocol,
// whose lines start '#' or '!'.
static constexpr uint32_t kStatusMs = 10000;
static blorb::Provisioned storageState;
static hw::SystemClock systemClock;
static uint32_t lastStatus = 0;

static const char* bootName(blorb::Boot b) {
  constexpr const char* kNames[] = {"Resumed", "FellBack", "FromLineage", "Fresh", "ReadOnlyNewer", "Sealed"};
  return kNames[size_t(b)];
}

struct OccupantSummary { const char* kind; uint16_t gen; uint32_t genome, age; };

static OccupantSummary summarize(const blorb::Occupant& o) {
  struct Visit {
    OccupantSummary operator()(const blorb::Egg& e) const { return {"egg", e.generation(), e.genome().hash(), 0}; }
    OccupantSummary operator()(const blorb::Creature& c) const {
      return {"creature", c.generation(), c.genome().hash(), c.stats().ageTicks};
    }
    OccupantSummary operator()(const blorb::Clutch& k) const { return {"clutch", k.generation, k.parent.hash(), 0}; }
  };
  return std::visit(Visit{}, o);
}

// The pet's genome, whatever he is now; null when a sealed board never read him.
static const blorb::Genome* petGenome() {
  const blorb::Occupant& o = dish->occupant();
  if (dish->sealed()) return nullptr;
  if (const blorb::Creature* c = std::get_if<blorb::Creature>(&o)) return &c->genome();
  if (const blorb::Egg* e = std::get_if<blorb::Egg>(&o)) return &e->genome();
  return &std::get<blorb::Clutch>(o).parent;
}

static ota::PetMark petMark() {
  const OccupantSummary o = summarize(dish->occupant());
  return {o.gen, o.genome};
}

bool DishOtaHost::busy() {
  // A clutch is chosen with the button, which the update's question would take.
  if (std::holds_alternative<blorb::Clutch>(dish->occupant())) return true;
  // A duel holds the radio and the button: BEGIN answers ERR 409 BUSY until it closes.
  if (dish->duelActive()) return true;
  // Anything else he must finish first refuses here too.
  // A board that cannot save is refused by the save before the transfer; a
  // sealed one may update, since a newer firmware is how a sealed pet wakes.
  return false;
}
bool DishOtaHost::save() { return dish->flush(); }
static bool wifiYieldRadio();       // src/wifi_ota.h
static bool wifiUpdateHoldsRadio();
static void wifiBegin();
bool RadioGate::open() {
  // Wi-Fi gives the radio up first (docs/wifi-ota.md "Duels"): a station
  // pins the channel to its access point's, and ESP-NOW needs channel 1.
  const bool wifiUp = wifiYieldRadio();
  why = wifiUpdateHoldsRadio() ? "ota_active" : trial.onTrial() ? "trial" : wifiUp ? "wifi_up" : nullptr;
  const bool ok = !why && inner->open();
  if (!why && !ok) why = inner->refusal();
  if (ok) radioDiag.say("radio open=1"), logged = "";
  else if (std::strcmp(logged, why) != 0) radioDiag.say("radio open=0 err=%s", why), logged = why;
  return ok;
}
bool DishOtaHost::onTrial() { return trial.onTrial(); }
bool DishOtaHost::handOver(ota::Version to, const ota::Token& token) {
  // A sealed or newer-format keepsake was never read: the Dish's egg is a stand-in, not the pet.
  const bool readThePet = dish->boot() != blorb::Boot::Sealed && dish->boot() != blorb::Boot::ReadOnlyNewer;
  return hw::EspTrialPort::write(
      ota::Handover{readThePet ? std::optional<ota::PetMark>(petMark()) : std::nullopt, to, token});
}
void DishOtaHost::forgetHandover() { trialPort.forgetHandover(); }
ota::Confirm DishOtaHost::confirmTrial(const ota::Token& token) { return trial.confirm(token); }
std::optional<ota::Version> DishOtaHost::rolledBack() { return trialPort.rolledBack(); }

// The update is verified and set to boot. The session saves him and says
// which pet the new image must find; a save that will not land keeps the old
// image instead.
static void restartIntoUpdate() {
  if (!updater.prepareRestart()) {
    Serial.println("[hw] ota: save FAILED twice, update discarded");
    return;
  }
  Serial.println("[hw] ota: restarting into the update, save ok");
  Serial.flush();
  ESP.restart();
}

// What tools/ota_release.py reads back out of firmware.bin, so a release can
// only be signed as the version, board and save format the image really is.
// reportBoot() prints from it, which keeps the linker from dropping it.
template <size_t N>
constexpr std::array<char, N> stampField(const char* s) {
  std::array<char, N> a{};
  for (size_t i = 0; i + 1 < N && s[i]; ++i) a[i] = s[i];
  return a;
}
struct ReleaseStamp {
  std::array<char, 12> magic;
  std::array<char, 16> fw, board;
  uint16_t fmt;
  std::array<char, 24> commit;   // git describe --always --dirty, as fw_stamp.py saw it
};
extern const ReleaseStamp kReleaseStamp = {stampField<12>("blorbstamp1"), stampField<16>(blorb::kFirmwareVersion),
                                           stampField<16>(BOARD_NAME), blorb::kFormatVersion,
                                           stampField<24>(BLORB_FW)};

static void reportBoot() {
  Serial.printf("[hw] blorbarium %s fw=%s fmt=%u %s\n", BLORB_FW, kReleaseStamp.fw.data(), unsigned(kReleaseStamp.fmt),
                kReleaseStamp.board.data());
  Serial.printf("[hw] home rot=%u (USB-C %s, %s; built %s)\n", unsigned(homeRot), hw::home::name(homeRot), homeWhy,
                hw::home::name(ORIENT_HOME));
  Serial.printf("[hw] psram size=%u free=%u\n", unsigned(ESP.getPsramSize()), unsigned(ESP.getFreePsram()));
  Serial.printf("[hw] psram: faultAir=%d lat=%d rowhash=%d shadow=%d (1: in PSRAM)\n", int(esp_ptr_external_ram(faultAir.get())),
                int(esp_ptr_external_ram(lat::rec.get())), int(esp_ptr_external_ram(boardRowHashes())),
                int(esp_ptr_external_ram(boardShadow())));
  Serial.printf("[hw] flash chip=%u\n", unsigned(ESP.getFlashChipSize()));
  Serial.printf("[hw] storage pet: %s\n", blorb::describe(storageState.slots));
  Serial.printf("[hw] storage petfs: %s\n", blorb::describe(storageState.files));
  for (const blorb::VolumeReport* r : {&storageState.slots, &storageState.files})
    if (r->opened == blorb::Opened::HeldForTrial)
      Serial.println("[hw] !!! on trial, this image would format a partition: held, so its trial rolls it back");
  if (blorb::leftAlone(storageState) && storageState.slots.opened != blorb::Opened::HeldForTrial &&
      storageState.files.opened != blorb::Opened::HeldForTrial)
    Serial.println("[hw] !!! SEALED: a pet is on the flash and would not open; nothing reads or writes it. "
                   "Dump 0x810000+0x40000 and 0xA50000+0x5A0000 read-only for repair");
  for (const blorb::VolumeReport* r : {&storageState.slots, &storageState.files})
    if (r->opened == blorb::Opened::FormatFailed || r->opened == blorb::Opened::ReadOnly)
      Serial.println("[hw] !!! the pet is not being saved: a reset loses this boot's pet");
  if (std::optional<uint32_t> t = systemClock.unixSeconds()) Serial.printf("[hw] clock=%u\n", unsigned(*t));
  else Serial.println("[hw] clock=unset");
  Serial.printf("[hw] imu %s tap=%s\n", imuOk ? "ok" : "missing", tapReady ? "ok" : "off");
  const OccupantSummary o = summarize(dish->occupant());
  if (dish->boot() == blorb::Boot::ReadOnlyNewer)
    Serial.println("[hw] !!! SEALED: the pet was saved by newer firmware; nothing reads or writes it. "
                   "Update the firmware to wake him");
  // species= is what the pet is now (a 0.4.0 pet reads grungo until adopted);
  // none when a sealed board never read him.
  const blorb::Genome* pet = petGenome();
  Serial.printf("[hw] boot=%s occupant=%s gen=%u genome=%08x age=%u species=%s\n", bootName(dish->boot()), o.kind,
                unsigned(o.gen), unsigned(o.genome), unsigned(o.age),
                pet ? blorb::speciesInfo(blorb::speciesOf(*pet)).name : "none");
}

// From the last BodySample the loop took: no second I2C read.
static void reportStatus(uint32_t now) {
  if (now - lastStatus < kStatusMs) return;
  lastStatus = now;
  const OccupantSummary o = summarize(dish->occupant());
  char temp[12] = "?";
  if (lastBody.tempCx10 != INT16_MIN) std::snprintf(temp, sizeof temp, "%.1f", lastBody.tempCx10 / 10.0);
  Serial.printf(
      "[hw] up=%u heap=%u minheap=%u big=%u psram_free=%u stack_hwm=%u writes=%u occupant=%s gen=%u genome=%08x "
      "age=%u tick=%u imu ax=%d ay=%d az=%d mg rot=%u t=%sC tap=%u button=%d power=%s cpu=%u frames=%u\n",
      unsigned(now / 1000), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(ESP.getFreePsram()), unsigned(uxTaskGetStackHighWaterMark(nullptr)),
      unsigned(store.writes()), o.kind, unsigned(o.gen), unsigned(o.genome), unsigned(o.age), unsigned(dish->tickCount()), lastBody.ax,
      lastBody.ay, lastBody.az, unsigned(shell.up().rotation()), temp, unsigned(lastBody.tapCode), lastBody.buttonDown ? 1 : 0,
      powerPolicy.low() ? "low" : "full", unsigned(cpuMaxMhz), unsigned(framesDrawn));
}
#endif

// A device that has never had a pet waits a moment on its first boot for
// `DEBUG found <species>`: tools/flash_fleet.sh --species sends it, so each
// gift hatches the creature chosen for its kid. The pin must land before the
// founding below, and storage is blank until this boot formats it, so there
// is no earlier moment to write it. A device with a pet never waits.
[[maybe_unused]] static void awaitFoundPin() {
  Serial.println("[hw] fresh: DEBUG found <species> in 5 s pins the egg");
  for (const uint32_t t0 = millis(); millis() - t0 < 5000; delay(10)) {
    phone.readLine();
    const char* line = phone.takeDebug();
    if (!line || std::strncmp(line, "found ", 6) != 0) continue;
    const char* why = debugverbs::found(dishStore, line + 6);
    Serial.printf("[hw] found pin %s: %s\n", line + 6, why ? why : "ok");
    if (!why) return;
  }
}

#if !defined(BADGE_BOARD_SIM)
// The Bluetooth name is the species (docs/ble.md): the pet's, or the one the
// badge table will adopt him as (founding.h advertisedSpecies), whatever the
// owner named him. An image on trial keeps the old image's blorb-xxxx, so a
// trial looks to the page exactly as the board it updated: the page served
// before rel-site keys its OTA token by device name, and a reloaded one must
// find that token to CONFIRM. The species name comes with the next boot.
static const char* bleName(const uint8_t mac[6]) {
  static char name[16];
  if (trial.onTrial()) {
    std::snprintf(name, sizeof name, "blorb-%02x%02x", mac[4], mac[5]);
  } else {
    std::snprintf(name, sizeof name, "%s", blorb::speciesInfo(blorb::advertisedSpecies(petGenome(), mac)).name);
  }
  return name;
}

// A pet 0.4.0 founded becomes his badge's species (founding.h), once the
// flash can be written: on an OTA trial, only after the trial keeps the
// image, so the trial's pet check and a rollback both meet the pet the old
// image saved. Then, under the same rule, a pet 0.7.0 adopted with the Gift
// recipe's sleep takes FirstHours' (founding.h retuneFirstHours). Asked once
// a second until the Dish says each is done. A board not in the table, and
// the gift grungo (row 0), are neither adopted nor retuned.
static void adoptTableSpecies(uint32_t now) {
  static bool adopted = false, retuned = false;
  static uint32_t askedAt = 0;
  if ((adopted && retuned) || trial.onTrial() || now - askedAt < 1000) return;
  askedAt = now;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const std::optional<blorb::SpeciesId> species = blorb::tableSpecies(mac);
  if (!adopted) {
    const blorb::Dish::Adoption a =
        species ? dish->adoptSpecies(*species, paint::bandsFor(*species)) : blorb::Dish::Adoption::NotNeeded;
    if (a == blorb::Dish::Adoption::NotYet) return;
    adopted = true;
    if (a == blorb::Dish::Adoption::Adopted) {
      const OccupantSummary o = summarize(dish->occupant());
      Serial.printf("[hw] adopted species=%s gen=%u genome=%08x name=%s (the Bluetooth name follows at the next boot)\n",
                    blorb::speciesInfo(*species).name, unsigned(o.gen), unsigned(o.genome), dish->lineage().currentName());
    }
  }
  const uint32_t was = summarize(dish->occupant()).genome;
  const blorb::Dish::Adoption r = species ? dish->retuneFirstHours(*species) : blorb::Dish::Adoption::NotNeeded;
  if (r == blorb::Dish::Adoption::NotYet) return;
  retuned = true;
  if (r == blorb::Dish::Adoption::Adopted)
    Serial.printf("[hw] retuned first-hours sleep species=%s genome=%08x->%08x\n", blorb::speciesInfo(*species).name,
                  unsigned(was), unsigned(summarize(dish->occupant()).genome));
}
#endif

#if !defined(BADGE_BOARD_SIM)
// Internal heap at each step of boot (#27: an image built on badge128_pm had
// about 25 KB less on trial than badge128, too little for Wi-Fi's driver).
static void heapMark(const char* step) {
  Serial.printf("[hw] heap %s: free=%u big=%u\n", step, unsigned(ESP.getFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}
#endif

void setup() {
#if !defined(BADGE_BOARD_SIM)
  // A line on the cable is queued, not waited out: with only the UART's
  // 128-byte FIFO, the loop stood still while every longer line went out at
  // 115200 baud (the 10 s status line, ~330 bytes: ~20 ms; a reply to the
  // protocol: more), and a gesture's samples and frame waited with it
  // (DEBUG latency: seen p95 51 ms while a probe asked STATE).
  // 512 B: a status line's worth; every byte of it is internal RAM (#53).
  Serial.setTxBufferSize(BLORB_SERIAL_TX);
#endif
  Serial.begin(115200);
#if !defined(BADGE_BOARD_SIM)
  heapMark("start");
  // In PSRAM (InPsram above), before anything uses them.
  if (BLORB_HEAP_BALLAST) {   // #53's A/B: never freed, never touched
    static void* ballast = heap_caps_malloc(BLORB_HEAP_BALLAST, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("[hw] heap ballast %u bytes %s\n", unsigned(BLORB_HEAP_BALLAST), ballast ? "held" : "FAILED");
  }
  faultAir.make(espNow, [] { return uint32_t(millis()); }, [] { return uint32_t(esp_random()); });
  radioGate.inner = faultAir.get();
  lat::rec.make();
#endif
  boardButtonBegin();
#if !defined(BADGE_BOARD_SIM)
#endif
  imuOk = imu::begin(PIN_IMU_SDA, PIN_IMU_SCL, tapReady);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t lineage = 0;
  for (uint8_t b : mac) lineage = lineage << 8 | b;
  blorb::DishOptions options;
#if !defined(BADGE_BOARD_SIM)
  // An image on trial opens the pet read-only and formats nothing: until it
  // keeps itself, the flash is the old image's (docs/ota.md "The trial").
  const bool onTrial = trialPort.pending();
  storageState = store.begin(onTrial);
  dishStore.hold(onTrial);
  heapMark("storage");
  options.rtc = &systemClock;
  options.sealed = blorb::leftAlone(storageState);
  // No battery: no wall time is coming, so a fresh pet hatches into his
  // morning, not into seven hours of pet night (docs/notes/first-hours.md).
  options.foundingHour = blorb::kUnaskedFoundingHour;
  // An egg waits for a phone's TIME; the debug link's FREEHATCH lifts it.
  options.hatchNeedsPhone = true;
#else
  options.sealed = simSealed;
#endif
  // A fresh board founds its own species: found.pin's, else the badge
  // table's, else grungo (founding.h). The sim founds the starter, as
  // it always has, so its goldens hold.
  const uint32_t speciesSeed = blorb::fnv1a(mac, sizeof(mac));
  std::optional<blorb::Genome> founder;
#if !defined(BADGE_BOARD_SIM)
  if (!options.sealed && blorb::needsFounding(dishStore)) {
    awaitFoundPin();
    const blorb::SpeciesId species = blorb::foundingSpecies(dishStore, mac);
    founder = blorb::foundingGenome(species, speciesSeed, paint::bandsFor(species));
    options.founder = &*founder;
  }
#endif
#if !defined(BADGE_BOARD_SIM)
  duelAir = &radioGate;
  if (quietBoot == kQuietMagic) {   // `DEBUG reboot quiet`: deaf and mute until `DEBUG net blackout 0`
    faultAir->cfg.deaf = true;
    Serial.printf("[net] quiet boot: radio blacked out until DEBUG net blackout 0\n");
  }
  quietBoot = 0;
#endif
  options.air = duelAir;   // a sealed board never steps a duel: the Dish runs no life
  dish.emplace(dishStore, speciesSeed, lineage, options);
#if !defined(BADGE_BOARD_SIM)
  heapMark("dish");
#endif
#if !defined(BADGE_BOARD_SIM)
  // This badge's own home, before the first frame or sample (src/hw/home.h).
  {
    const hw::home::AtBoot h = hw::home::atBoot(hw::HomeStore::load(), ORIENT_HOME);
    homeRot = h.rot;
    homeWhy = h.write ? (hw::HomeStore::save(h.rot) ? "migrated: written" : "migrated: WRITE FAILED") : h.why;
    shell.setHome(h.rot);
  }
#endif
  [[maybe_unused]] bool panelUp = display.init();
  boardPresentBegin(display);   // the panel's send task, on core 0
#if !defined(BADGE_BOARD_SIM)
  heapMark("panel");
#endif
#if !defined(BADGE_BOARD_SIM)
  // An update over Wi-Fi restarts into its trial dark, in the night or on a
  // shelf: the backlight stays off until someone wakes him (src/wifi_ota.h).
  display.setBrightness(wifiQuietBoot() ? 0 : dish->settings().brightness);
  reportBoot();
#if defined(BLORB_OTA_FAIL_TRIAL)
  // For the hardware proof only (docs/ota.md): an image that reports its panel
  // down, so its trial must roll it back.
  panelUp = false;
#endif
  const bool resumed = dish->boot() == blorb::Boot::Resumed;
  trial.begin(trialPort, panelUp, resumed, petMark());
  if (trial.onTrial()) {
    const std::optional<ota::Handover> h = trialPort.handover();
    Serial.printf("[hw] ota trial: on trial, panel=%d resumed=%d handover=%s\n", panelUp, resumed,
                  !h ? "none" : !h->pet ? "any pet" : *h->pet == petMark() ? "same pet" : "ANOTHER PET");
    trialWatchdog.start();
  }
  otaPackets.begin();
  wifiBegin();
  ble.keepWhile([] { return updater.active(); });
  const char* name = ble.begin(bleName(mac), &otaData);
  Serial.printf("[hw] ble name=%s heap=%u big=%u\n", name, unsigned(ESP.getFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  xTaskCreate(bodyq::sense, "sense", 4096, nullptr, 2, nullptr);
  pmBegin();
  heapMark("sense+pm");
  paint::phaseClock.now = fps::now;   // DEBUG fps's phases
  paintcore0::begin();                // the renderer's second half on core 0
  heapMark("paint core0");
#if defined(CONFIG_PM_ENABLE)
  Serial.println("[hw] pm: light sleep in low power, ble modem sleep");
#else
  Serial.println("[hw] pm: none (stock framework): low power is 80 MHz and a dark panel");
#endif
#endif
}

#if !defined(BADGE_BOARD_SIM)
// Powered time a DEBUG warp ran ahead of the wall: the loop's clock is millis() plus this.
static uint32_t warpMs = 0;

static void warp(uint32_t ticks) {
  for (uint32_t done = 0; done < ticks; done += 10) {
    warpMs += 10 * blorb::kTickMs;
    dish->tick(millis() + warpMs, host);    // at most 10 ticks a call
    if (done % 2000 == 0) delay(1);          // let the idle task run
  }
}

// Set by `DEBUG bletest`, so its report prints whatever its size.
static bool bleTestPending = false;

// Queues `bytes` of a known pattern straight to the BLE link: lines of 'a' to
// 'z' repeating, each byte at stream offset i being 'a' + i % 26, '\n' included.
static void bleTest(const char* a) {
  const long n = std::strtol(a, nullptr, 10);
  if (n <= 0 || n > 65536) return (void)Serial.printf("[hw] debug bletest: bad size '%s'\n", a);
  if (!ble.connected()) return (void)Serial.printf("[hw] debug bletest: no central connected\n");
  char line[161];
  for (long at = 0; at < n;) {
    const long len = std::min<long>(160, n - at - 1);
    for (long i = 0; i < len; ++i) line[i] = char('a' + (at + i) % 26);
    ble.writeLine(std::string_view(line, size_t(len < 0 ? 0 : len)));
    at += (len < 0 ? 0 : len) + 1;
  }
  bleTestPending = true;
  Serial.printf("[hw] debug bletest: queued %ld bytes\n", n);
}

// Set by `DEBUG shot`: the canvas goes out a line a loop (paint/shot.h) and is
// not repainted until the last line has, so the picture is one whole frame.
// The Dish keeps sampling and ticking meanwhile; only the glass holds still.
// Serial only, like every DEBUG line: the BLE link never carries it.
static std::optional<paint::ShotEncoder> shot;

// True while a shot is going out.
static bool sendShotLine() {
  if (!shot) return false;
  if (const char* line = shot->next()) Serial.printf("%s\n", line);
  else shot.reset();
  return bool(shot);
}

static void press(const char* a) {
  const long ms = *a ? std::strtol(a, nullptr, 10) : 150;   // the sim's !button
  if (ms <= 0 || ms > 10000) return (void)Serial.printf("[hw] debug press: bad ms '%s'\n", a);
  bodyq::presses[1].ms = 0;
  bodyq::presses[0].ms = uint32_t(ms);
  bodyq::presses[0].at = millis();
  Serial.printf("[hw] debug press %ld ms\n", ms);
}

// Two 100 ms presses 150 ms apart, inside the 400 ms window: the gesture that opens a duel.
static void doublePress() {
  const uint32_t t = millis();
  bodyq::presses[0].ms = bodyq::presses[1].ms = 100;
  bodyq::presses[0].at = t;
  bodyq::presses[1].at = t + 250;
  // A duel opens only from Awake (15); Asleep is 14 (boot.h's BootContext).
  Serial.printf("[hw] debug double, boot context %u\n", unsigned(dish->bootContext()));
}

static void leanFor(uint8_t aim, uint32_t ms) {
  leanAim = int8_t(aim);
  leanAt = millis() + warpMs;
  leanMs = ms;
  Serial.printf("[hw] debug lean %u for %u ms\n", unsigned(aim), unsigned(ms));
}

static void lean(const char* line) {
  long aim = -1, ms = 3000;
  if (std::sscanf(line, "%*s %ld %ld", &aim, &ms) < 1 || aim < 0 || aim > 3 || ms <= 0 || ms > 60000)
    return (void)Serial.printf("[hw] debug lean: want <aim 0..3> [ms]\n");
  leanFor(uint8_t(aim), uint32_t(ms));
}

// One line per duel event, so the two boards' logs alone prove a duel.
static void reportDuel() {
  static blorb::DuelPhase phase = blorb::DuelPhase::Closed;
  static uint8_t peers = 0, turn = 0;
  static uint32_t metHash = 0, refusedAt = 0;
  if (const blorb::DishDuel::Refusal& r = dish->duelRefusal(); r.any && r.tick != refusedAt) {
    refusedAt = r.tick;
    Serial.printf("[duel] refused why=%u (0: the radio would not open) boot_context=%u\n", unsigned(r.why),
                  unsigned(dish->bootContext()));
  }
  if (!dish->duelActive() && phase == blorb::DuelPhase::Closed) return;
  constexpr const char* kPhase[] = {"closed",  "lobby",     "greeting", "staking", "choosing", "waiting",
                                    "revealing", "fainting", "mercy",    "fallen",  "friends"};
  constexpr const char* kStance[] = {"pounce", "guard", "trick"};
  constexpr const char* kStakes[] = {"spar", "scrap", "mortal"};
  const blorb::DuelView v = dish->duelView();
  if (v.phase != phase) {
    Serial.printf("[duel] phase %s -> %s end=%u stakes=%s hearts %u-%u\n", kPhase[size_t(phase)],
                  kPhase[size_t(v.phase)], unsigned(v.ended), kStakes[size_t(v.agreed)], unsigned(v.mine.hearts),
                  unsigned(v.theirs.hearts));
    phase = v.phase;
    if (phase == blorb::DuelPhase::Closed) peers = 0, turn = 0;
  }
  if (v.phase == blorb::DuelPhase::Lobby && v.peerCount != peers) {
    peers = v.peerCount;
    Serial.printf("[duel] lobby hears %u friend(s)%s species=", unsigned(peers),
                  peers && v.peers[0].wantsMe ? ", the first wants me" : "");
    for (uint8_t i = 0; i < peers; ++i) Serial.printf("%s%s", i ? "," : "", blorb::speciesInfo(v.peers[i].species).name);
    Serial.printf("\n");
  }
  if (v.hasLast && v.lastTurn != turn) {
    turn = v.lastTurn;
    Serial.printf("[duel] turn %u me=%s%s them=%s%s lost %u/%u hearts %u-%u\n", unsigned(turn),
                  kStance[size_t(v.last.move[0].stance)], v.last.move[0].byPet ? "(pet)" : "",
                  kStance[size_t(v.last.move[1].stance)], v.last.move[1].byPet ? "(pet)" : "",
                  unsigned(v.last.hit[0]), unsigned(v.last.hit[1]), unsigned(v.mine.hearts),
                  unsigned(v.theirs.hearts));
  }
  if (const std::optional<blorb::Meeting>& m = dish->lastMeeting(); m && m->outcomeHash != metHash) {
    metHash = m->outcomeHash;
    constexpr const char* kEnding[] = {"ongoing", "A won", "B won", "draw"};
    Serial.printf("[duel] met side=%c ending=%s turns=%u outcome_hash=%08x battle=%08x/%u them=%s\n",
                  m->me == blorb::Side::A ? 'A' : 'B', kEnding[size_t(m->ending)], unsigned(m->turns),
                  unsigned(m->outcomeHash), unsigned(m->battle.seed), unsigned(m->battle.rematch),
                  blorb::speciesInfo(dish->duelView().them.species).name);
  }
}

static void pinSpecies(const char* a) {
  if (!std::strcmp(a, "off")) {
    pinnedSpecies.reset();
    return (void)Serial.printf("[hw] debug species off: drawn as the pet's own\n");
  }
  blorb::SpeciesId s;
  if (!debugverbs::speciesByName(a, s))
    return (void)Serial.printf("[hw] debug species: '%s' is not a species.def name or 0..255\n", a);
  pinnedSpecies = s;
  Serial.printf("[hw] debug species %u: drawn as %s until reset, not saved\n", unsigned(s.v),
                blorb::speciesInfo(s).name);
}

static void pinFounding(const char* a) {
  const char* why = debugverbs::found(dishStore, a);
  Serial.printf("[hw] debug found %s: %s\n", a, why ? why : "ok, the next fresh founding's nest is this egg");
}

// `DEBUG hang [ms] [wobble]`: upside down for ms (default 2500), then upright 3 s.
static void hangFor(uint32_t ms, bool wobble);
static void hang(const char* line) {
  long ms = 2500, wobble = 0;
  std::sscanf(line, "%*s %ld %ld", &ms, &wobble);
  if (ms <= 0 || ms > 30000) return (void)Serial.printf("[hw] debug hang: want [ms 1..30000] [wobble 0|1]\n");
  hangFor(uint32_t(ms), wobble != 0);
}

static void hangFor(uint32_t ms, bool wobble) {
  bodyq::hangMs = 0;
  bodyq::hangSide = 0;
  bodyq::rightMs = 3000;
  bodyq::hangWobble = wobble;
  bodyq::hangAt = millis();
  bodyq::hangMs = ms;
  Serial.printf("[hw] debug hang %u ms wobble=%d, then upright 3000 ms\n", unsigned(ms), int(wobble));
}

// `DEBUG side [ms] [l|r]`: turned onto a side for ms (default 2500), then
// upright 3 s: the hold's twin, through the real orientation settle and
// SideHoldDetector. r (the default) is rotation 1, l rotation 3.
static void sideFor(uint32_t ms, bool left) {
  bodyq::hangMs = 0;
  bodyq::rightMs = 3000;
  bodyq::hangWobble = false;
  bodyq::hangSide = int8_t(left ? -1 : 1);
  bodyq::hangAt = millis();
  bodyq::hangMs = ms;
  Serial.printf("[hw] debug side %s %u ms, then upright 3000 ms\n", left ? "l" : "r", unsigned(ms));
}

static void side(const char* line) {
  long ms = 2500;
  char which[4] = "r";
  std::sscanf(line, "%*s %ld %3s", &ms, which);
  if (ms <= 0 || ms > 30000 || (which[0] != 'l' && which[0] != 'r'))
    return (void)Serial.printf("[hw] debug side: want [ms 1..30000] [l|r]\n");
  sideFor(uint32_t(ms), which[0] == 'l');
}

// `DEBUG knock [1|2]`: one knock, or a knock knock (default), as the tap engine reports it.
static void knockOnce(uint8_t code) {
  bodyq::knockAt = millis() + 100;   // on a sample to come, not one already read
  bodyq::knockCode = code;
  Serial.printf("[hw] debug knock %u\n", unsigned(code));
}

static void knock(const char* a) {
  const long code = *a ? std::strtol(a, nullptr, 10) : 2;
  if (code != 1 && code != 2) return (void)Serial.printf("[hw] debug knock: want [1|2]\n");
  knockOnce(uint8_t(code));
}

// `DEBUG cradle [ms]`: held still in a hand for ms (default 5000): a cuddle, through the real detectors.
static void cradleFor(uint32_t ms) {
  bodyq::cradleMs = 0;
  bodyq::cradleAt = millis();
  bodyq::cradleMs = ms;
  Serial.printf("[hw] debug cradle %u ms\n", unsigned(ms));
}

static void shake() {
  bodyq::shakeAt = millis() + 40;   // on a read to come
  bodyq::shaking = true;
  Serial.printf("[hw] debug shake\n");
}

static void cradle(const char* a) {
  const long ms = *a ? std::strtol(a, nullptr, 10) : 5000;
  if (ms <= 0 || ms > 30000) return (void)Serial.printf("[hw] debug cradle: want [ms 1..30000]\n");
  cradleFor(uint32_t(ms));
}

// `DEBUG nap`: he falls asleep now, as at bedtime (to prove the hang wakes him for a fight).
static void nap() {
  blorb::Creature* c = std::get_if<blorb::Creature>(&dish->occupant());
  if (!c) return (void)Serial.printf("[hw] debug nap: no creature\n");
  c->force(blorb::action::sleep);
  Serial.printf("[hw] debug nap: forced sleep\n");
}

// `DEBUG tidy`: what a test run leaves behind, taken away: every pellet on
// the floor (they rot, and he eats them rotten: a probe that fed a sleeping
// pet killed him, 2026-10-21), and his toxin and injury back to none, as
// `DEBUG stage` settles them. The pantry is left as it is.
static void tidy() {
  unsigned pellets = 0;
  for (blorb::Pellet& p : dish->habitat().pellets) pellets += p.present, p.present = false;
  blorb::Creature* c = std::get_if<blorb::Creature>(&dish->occupant());
  if (c) c->inject(blorb::chem::toxin, blorb::Fx::zero()), c->inject(blorb::chem::injury, blorb::Fx::zero());
  Serial.printf("[hw] debug tidy: %u pellets taken, %s\n", pellets, c ? "toxin and injury 0" : "no creature");
}

// `DEBUG sleepiness <pct>`: sets his sleepiness drive, so a soak can make him
// rested (a wake for a fight is allowed) or too sleepy (it is refused) at will.
static void setSleepiness(const char* a) {
  blorb::Creature* c = std::get_if<blorb::Creature>(&dish->occupant());
  if (!c) return (void)Serial.printf("[hw] debug sleepiness: no creature\n");
  const long pct = std::strtol(a, nullptr, 10);
  c->inject(blorb::driveChem(blorb::drive::sleepiness), blorb::Fx::ratio(pct < 0 ? 0 : pct > 100 ? 100 : pct, 100));
  Serial.printf("[hw] debug sleepiness %ld%%\n", pct);
}

static void netReport() {
  const hw::FaultAir::Config& c = faultAir->cfg;
  const hw::FaultAir::Counts& n = faultAir->n;
  Serial.printf("[net] cfg rx_drop=%u tx_drop=%u dup=%u delay=%u jitter=%u ghosts=%u/%u%s blackout_left=%ld | rx=%u "
                "rx_dropped=%u tx=%u tx_dropped=%u duped=%u delayed=%u blacked=%u overflow=%u ghost_beacons=%u open=%d\n",
                c.rxDrop, c.txDrop, c.dup, c.delayMs, c.jitterMs, c.ghosts, c.ghostMode, c.ghostsLow ? "low" : "high",
                c.deaf ? -1L : faultAir->blackedOut() ? long(c.blackoutUntil - millis()) : 0L, unsigned(n.rx), unsigned(n.rxDropped),
                unsigned(n.tx), unsigned(n.txDropped), unsigned(n.duped), unsigned(n.delayed), unsigned(n.blackedOut),
                unsigned(n.overflow), unsigned(n.ghostBeacons), int(faultAir->isOpen()));
  for (uint8_t i = 0; i < faultAir->heardCount; ++i) {
    const auto& h = faultAir->heard[i];
    Serial.printf("[net] heard %02x:%02x:%02x:%02x:%02x:%02x frames=%u rssi=%d\n", h.from.mac[0], h.from.mac[1],
                  h.from.mac[2], h.from.mac[3], h.from.mac[4], h.from.mac[5], unsigned(h.frames), int(h.rssi));
  }
}

// `DEBUG net` reports; `DEBUG net clear`; `DEBUG net rx|tx|dup <pct>`;
// `DEBUG net delay <ms> [jitter]`; `DEBUG net blackout <ms>`.
static bool net(const char* line) {
  char what[16] = "";
  long x = 0, y = 0;
  const int got = std::sscanf(line, "%*s %15s %ld %ld", what, &x, &y);
  hw::FaultAir::Config& c = faultAir->cfg;
  const auto pct = [](long v) { return uint8_t(v < 0 ? 0 : v > 100 ? 100 : v); };
  if (got <= 0) {
  } else if (!std::strcmp(what, "clear")) faultAir->clear();
  else if (!std::strcmp(what, "rx")) c.rxDrop = pct(x);
  else if (!std::strcmp(what, "tx")) c.txDrop = pct(x);
  else if (!std::strcmp(what, "dup")) c.dup = pct(x);
  else if (!std::strcmp(what, "delay")) c.delayMs = uint16_t(x < 0 ? 0 : x > 5000 ? 5000 : x), c.jitterMs = uint16_t(y < 0 ? 0 : y > 5000 ? 5000 : y);
  else if (!std::strcmp(what, "ghosts")) {
    char side[8] = "low";
    std::sscanf(line, "%*s %*s %*d %*d %7s", side);
    c.ghosts = uint8_t(x < 0 ? 0 : x > 6 ? 6 : x), c.ghostMode = uint8_t(y < 0 || y > 2 ? 0 : y);
    c.ghostsLow = std::strcmp(side, "high") != 0;
  }
  else if (!std::strcmp(what, "blackout")) c.blackoutUntil = x > 0 ? uint32_t(millis() + x) | 1 : 0, c.deaf = false;
  else return Serial.printf("[net] want: net [clear|rx <pct>|tx <pct>|dup <pct>|delay <ms> [jitter]|blackout <ms>|ghosts <n> <mode 0-2> [low|high]]\n"), false;
  netReport();
  return true;
}

// `DEBUG air [open|close]`: the duel's radio by hand, through RadioGate as
// fight mode opens it (an open Wi-Fi window gives the radio up first; a trial
// or an update refuses), to show the air still fits after a Wi-Fi window.
// EspNowAir logs its own `[hw] air open ch=1 heap A -> B ... big=`.
static void airVerb(const char* a) {
  if (!std::strcmp(a, "open")) {
    const bool ok = radioGate.open();
    return (void)Serial.printf("[hw] debug air open: %s heap=%u minheap=%u big=%u\n", ok ? "ok" : radioGate.refusal(),
                               unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
                               unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }
  if (!std::strcmp(a, "close")) radioGate.close();
  Serial.printf("[hw] debug air %s: open=%d heap=%u minheap=%u big=%u\n", *a ? a : "status", int(radioGate.isOpen()),
                unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

// `DEBUG duel`: where the duel stands right now, in one line.
static void duelNow() {
  constexpr const char* kPhase[] = {"closed",  "lobby",     "greeting", "staking", "choosing", "waiting",
                                    "revealing", "fainting", "mercy",    "fallen",  "friends"};
  const blorb::DuelView v = dish->duelView();
  Serial.printf("[duel] now active=%d phase=%s peers=%u ctx=%u hearts %u-%u\n", int(dish->duelActive()),
                kPhase[size_t(v.phase)], unsigned(v.peerCount), unsigned(dish->bootContext()), unsigned(v.mine.hearts),
                unsigned(v.theirs.hearts));
}

// Save, then restart; quiet: the radio stays blacked out until `net blackout 0`.
[[noreturn]] static void rebootNow(bool quiet) {
  quietBoot = quiet ? kQuietMagic : 0;
  Serial.printf("[hw] debug reboot%s\n", quietBoot ? " quiet" : "");
  Serial.flush();
  dish->flush();
  ESP.restart();
  for (;;) {}
}

// Low power's verbs, below debug_ble.h, which uses them.
static void powerVerb(const char* a, const char* line);
static bool powerAsk(debugble::Board::PowerAsk ask, uint32_t idleMs);
static const char* powerLine(char* out, size_t n, uint32_t now);
static bool powerHeld();
static bool remotePress(uint32_t ms, bool twice);

static void wifiVerb(const char* line);   // src/wifi_ota.h
static void otaVerb(const char* line);

// `DEBUG home [up|down]`: which way up is home on this badge, kept in NVS
// through updates and pet wipes (src/hw/home.h). Bare: what it is now.
static void homeVerb(const char* a) {
  if (*a) {
    const std::optional<uint8_t> h = hw::home::parse(a);
    if (!h) return (void)Serial.printf("[hw] debug home: want up|down (USB-C up or USB-C down is the right way up)\n");
    const bool saved = hw::HomeStore::save(*h);
    homeRot = *h;
    homeWhy = saved ? "nvs" : "set, SAVE FAILED";
    shell.setHome(*h);
  }
  const std::optional<uint8_t> stored = hw::HomeStore::load();
  Serial.printf("[hw] home rot=%u (USB-C %s, %s; stored %s; built %s)\n", unsigned(homeRot), hw::home::name(homeRot),
                homeWhy, stored ? hw::home::name(*stored) : "none", hw::home::name(ORIENT_HOME));
}

static void runDebug(const char* line) {
  char verb[16] = "", a[32] = "";
  std::sscanf(line, "%15s %31s", verb, a);
  if (std::strcmp(verb, "home") == 0) return homeVerb(a);
  if (std::strcmp(verb, "hang") == 0) return hang(line);
  if (std::strcmp(verb, "side") == 0) return side(line);
  if (std::strcmp(verb, "knock") == 0) return knock(a);
  if (std::strcmp(verb, "trace") == 0) return traceFor(a);
  if (std::strcmp(verb, "nap") == 0) return nap();
  if (std::strcmp(verb, "cradle") == 0) return cradle(a);
  if (std::strcmp(verb, "shake") == 0) return shake();
  if (std::strcmp(verb, "sleepiness") == 0) return setSleepiness(a);
  if (std::strcmp(verb, "tidy") == 0) return tidy();
  if (std::strcmp(verb, "net") == 0) return (void)net(line);
  if (std::strcmp(verb, "power") == 0) return powerVerb(a, line);
  if (std::strcmp(verb, "latency") == 0) return lat::verb(a);
  if (std::strcmp(verb, "warp") == 0) {   // `DEBUG warp <minutes>`: the pet's time runs ahead (a vigil to its clutch)
    const long min = std::strtol(a, nullptr, 10);
    if (min < 1 || min > 600) return (void)Serial.printf("[hw] debug warp: want <minutes 1..600>\n");
    warp(uint32_t(min) * blorb::kTicksPerMinute);
    return (void)Serial.printf("[hw] debug warp %ld min: occupant=%s ctx=%u\n", min, summarize(dish->occupant()).kind,
                               unsigned(dish->bootContext()));
  }
  if (std::strcmp(verb, "fps") == 0) return std::strcmp(a, "bench") == 0 ? fps::bench() : fps::report();
  if (std::strcmp(verb, "lcd") == 0) return lcdVerb(a);
  if (std::strcmp(verb, "cores") == 0) {   // `DEBUG cores 1|2`: draw on one core or both, until reset
    if (*a) paintcore0::use(std::strtol(a, nullptr, 10) >= 2);
    return (void)Serial.printf("[hw] cores draw=%d\n", paintcore0::on ? 2 : 1);
  }
  if (std::strcmp(verb, "smooth") == 0) {   // `DEBUG smooth [on|off]`: frames between ticks, or each tick's own (paint/tween.h)
    if (*a) smoothFrames = std::strcmp(a, "off") != 0;
    return (void)Serial.printf("[hw] smooth=%s speed=%u/256\n", smoothFrames ? "on" : "off", unsigned(paint::kScreenSpeedQ8));
  }
  if (std::strcmp(verb, "duel") == 0) return duelNow();
  if (std::strcmp(verb, "air") == 0) return airVerb(a);
  if (std::strcmp(verb, "wifi") == 0) return wifiVerb(line);
  if (std::strcmp(verb, "ota") == 0) return otaVerb(line);
  if (std::strcmp(verb, "reboot") == 0) rebootNow(std::strcmp(a, "quiet") == 0);
  if (std::strcmp(verb, "bletest") == 0) return bleTest(a);
  if (std::strcmp(verb, "shot") == 0) return (void)shot.emplace(canvas);
  if (std::strcmp(verb, "press") == 0) return press(a);
  if (std::strcmp(verb, "double") == 0) return doublePress();
  if (std::strcmp(verb, "lean") == 0) return lean(line);
  if (std::strcmp(verb, "species") == 0) return pinSpecies(a);
  if (std::strcmp(verb, "found") == 0) return pinFounding(a);
  if (std::strcmp(verb, "hatch") == 0) {   // the egg hatches now, as the debug link's HATCH, with no phone (FREEHATCH)
    dish->setHatchFree(true);
    const bool ok = debugble::hatch(*dish, warp) == debugble::Jump::Done &&
                    !std::holds_alternative<blorb::Egg>(dish->occupant());
    const OccupantSummary o = summarize(dish->occupant());
    return (void)Serial.printf("[hw] debug hatch: %s occupant=%s genome=%08x\n", ok ? "ok" : "no egg", o.kind,
                               unsigned(o.genome));
  }
  if (std::strcmp(verb, "stage") != 0) {
    Serial.printf("[hw] debug: unknown verb '%s' (try: stage adult, hatch, species gilly, found gilly, shot, "
                  "press [ms], double, knock [1|2], side [ms] [l|r], lean <aim> [ms], hang, shake, cradle, nap, net, duel, latency [last|clear], warp <min>, tidy, "
                  "power [low|wake|idle <s>], fps [bench], lcd [MHz|sync|async], cores [1|2], reboot, bletest <bytes>, air [open|close], home [up|down], wifi [status|set|forget|on|off], ota check [url])\n", verb);
    return;
  }
  const uint32_t t0 = millis();
  const char* why = debugverbs::growTo(*dish, a, warp);
  const OccupantSummary o = summarize(dish->occupant());
  const auto* c = std::get_if<blorb::Creature>(&dish->occupant());
  constexpr const char* kStage[] = {"hatchling", "child", "adult", "elder"};
  Serial.printf("[hw] debug stage %s: %s stage=%s occupant=%s gen=%u genome=%08x age=%u writes=%u in %u ms\n", a,
                why ? why : "ok", c ? kStage[size_t(c->stage())] : "-", o.kind, unsigned(o.gen), unsigned(o.genome),
                unsigned(o.age), unsigned(store.writes()), unsigned(millis() - t0));
}

#include "debug_ble.h"

// ---- Low power (src/hw/power.h, docs/power.md) ---------------------------------------
// Asleep: the backlight is off and the panel in SLPIN, no frame is drawn, the
// CPU runs at 80 MHz and the loop rests between passes. The pet still ticks,
// the sense task still samples at 50 Hz (that is what wakes it), Bluetooth
// still advertises. Awake: 240 MHz, SLPOUT, a fresh frame, then the light,
// and as many frames as are useful (kFrameMs): a badge is awake a little and
// low most of the time, so awake may spend (owner, 2026-10-20).
static constexpr uint32_t kFullMhz = 240, kLowMhz = 80;
static constexpr uint32_t kLowRestMs = 20;   // the loop's rest a pass in low power
static bool panelLow = false, lightAfterFrame = false;
static uint32_t seenBleGen = 0, seenAirLines = 0;

// The CPU side of low power. On a framework built with power management
// (CONFIG_PM_ENABLE: tools/pm_framework.sh, env badge128_pm) the chip
// light-sleeps whenever every task is waiting, which in low power is most
// of each 20 ms: the sense task's 50 Hz and the loop's rest wake it, and
// Bluetooth's modem sleep (CONFIG_BT_CTRL_MODEM_SLEEP) keeps the link up
// across it. Awake there is no light sleep at all, so the panel's SPI and
// the backlight's PWM never meet a gated clock. On the stock framework the
// clock just drops to 80 MHz.
#if defined(CONFIG_PM_ENABLE)
static constexpr bool kLightSleep = true;
// The cable is UART0 through the CH343, and a UART hears nothing while the
// chip light-sleeps: a byte wakes it (uart wakeup, 3 edges) but that byte and
// the next few are lost. So any byte on the cable holds light sleep off for
// kCableAwakeMs, and a script sends a few '\r' (the link ignores them), waits
// ~60 ms, then its line, which arrives whole. A quiet cable lets the chip
// sleep, so a badge on its cable still proves light sleep (slept_ms).
static constexpr uint32_t kCableAwakeMs = 3000;
static esp_pm_lock_handle_t cableLock = nullptr;
static bool cableHeld = false;
static void cablePower(uint32_t) {
  const bool hold = millis() - cableHeardAt < kCableAwakeMs;
  if (hold == cableHeld || !cableLock) return;
  cableHeld = hold;
  if (hold) esp_pm_lock_acquire(cableLock);
  else esp_pm_lock_release(cableLock);
}
static const char* sleepState() { return !powerPolicy.low() ? "off" : cableHeld ? "cable" : "light"; }
// Light-sleep time since boot, from the PM profiling's mode table (the SLEEP
// row, which it only prints while light sleep is on: awake, the last one seen).
static uint32_t sleptMs() {
  static uint32_t last = 0;
#if defined(CONFIG_PM_PROFILING)
  static char buf[1536];
  std::memset(buf, 0, sizeof buf);
  FILE* f = fmemopen(buf, sizeof buf - 1, "w");
  if (!f) return last;
  esp_pm_dump_locks(f);
  std::fclose(f);
  unsigned mhz = 0;
  unsigned long long us = 0;
  if (const char* l = std::strstr(buf, "\nSLEEP"); l && std::sscanf(l + 1, "SLEEP %u M %llu", &mhz, &us) == 2)
    last = uint32_t(us / 1000);
#endif
  return last;
}
// esp_pm_configure only records the new limits: the clock moves at the next
// lock taken or given back. So awake holds a CPU_FREQ_MAX lock, taken right
// after the limits go up and given back right after they come down: the clock
// is 240 MHz the moment the badge wakes and drops the moment it sleeps, not
// whenever some driver next takes a lock.
static esp_pm_lock_handle_t awakeLock = nullptr;
static bool awakeHeld = false;
static void cpuPower(bool low) {
  esp_pm_config_esp32s3_t c{};
  c.max_freq_mhz = low ? int(kLowMhz) : int(kFullMhz);
  c.min_freq_mhz = low ? 40 : int(kFullMhz);   // 40: the crystal, while waiting
  c.light_sleep_enable = low;
  const esp_err_t e = esp_pm_configure(&c);
  if (e != ESP_OK) Serial.printf("[power] esp_pm_configure failed err=%d\n", int(e));
  cpuMaxMhz = uint32_t(c.max_freq_mhz);
  if (awakeLock && awakeHeld == low) {
    awakeHeld = !low;
    if (awakeHeld) esp_pm_lock_acquire(awakeLock);
    else esp_pm_lock_release(awakeLock);
  }
}
static void pmBegin() {
  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "cable", &cableLock);
  esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "awake", &awakeLock);
  uart_set_wakeup_threshold(UART_NUM_0, 3);
  esp_sleep_enable_uart_wakeup(0);
  cpuPower(false);
}
#else
static const char* sleepState() { return "none"; }
static uint32_t sleptMs() { return 0; }
static void cablePower(uint32_t) {}
static constexpr bool kLightSleep = false;
static void cpuPower(bool low) {
  cpuMaxMhz = low ? kLowMhz : kFullMhz;
  setCpuFrequencyMhz(cpuMaxMhz);
}
static void pmBegin() { cpuPower(false); }   // set, not assumed: awake is 240 MHz
#endif

// What needs the screen awake: a fight (searching, waiting or on), an update
// or its question, the debug unlock's question, a DEBUG shot going out.
static bool powerHeld() {
  return dish->duelActive() || std::strcmp(dish->fightState(), "off") != 0 ||
         (updater.active() && !updater.ownedBy(kWifiPeer)) || debugAsks() || wifiAsks() || bool(shot);
}

static const char* powerLine(char* out, size_t n, uint32_t now) {
  std::snprintf(out, n, "power=%s why=%s idle_ms=%u left_ms=%u cpu=%u frames=%u changes=%u held=%d input=%s sleep=%s slept_ms=%u",
                powerPolicy.low() ? "low" : "full", power::whyName(powerPolicy.why()), unsigned(powerPolicy.idleMs()),
                unsigned(powerPolicy.msLeft(now)), unsigned(cpuMaxMhz), unsigned(framesDrawn),
                unsigned(powerPolicy.changes()), int(powerHeld()), power::whyName(powerPolicy.lastInputWhy()),
                sleepState(), unsigned(sleptMs()));
  return out;
}

// Every loop: the phone's inputs, the timeout, and the panel and CPU to match.
static void applyPower(uint32_t now) {
  if (const uint32_t g = ble.generation(); g != seenBleGen) seenBleGen = g, powerPolicy.phone(now);
  if (const uint32_t n = links.airLines(); n != seenAirLines) seenAirLines = n, powerPolicy.phone(now);
  powerPolicy.update(now, powerHeld());
  cablePower(now);
  bodyq::fastSense = !powerPolicy.low();   // 250 Hz reads awake, 50 Hz in low power
  if (powerPolicy.low() == panelLow) return;
  panelLow = powerPolicy.low();
  if (panelLow) {
    lightAfterFrame = false;
    boardPanelIdle();
    display.sleep();   // backlight to 0, then SLPIN
    cpuPower(true);
  } else {
    cpuPower(false);
    boardPanelIdle();
    display.getPanel()->setSleep(false);   // SLPOUT; the light waits for a fresh frame
    boardRepaintAll();                     // all of it: the panel's memory is not trusted across SLPIN
    lastFrame = now - kFrameMs;            // drawn this very pass
    lightAfterFrame = true;
  }
  char line[200];
  Serial.printf("[power] %s\n", powerLine(line, sizeof line, now));
  dbgfw::link.writeLine(std::string("! DEBUG ") + line);
}

// `DEBUG power [low|wake|idle <seconds>|idle default]`, and the debug link's POWER.
static bool powerAsk(debugble::Board::PowerAsk ask, uint32_t idleMs) {
  const uint32_t now = millis() + warpMs;
  using A = debugble::Board::PowerAsk;
  if (ask == A::Low && powerHeld()) return false;
  if (ask == A::Low) powerPolicy.sleepNow();
  if (ask == A::Wake) powerPolicy.wakeNow(now);
  if (ask == A::Idle) powerPolicy.setIdleMs(idleMs ? idleMs : power::kIdleMs);
  if (ask != A::Status) applyPower(now);
  return true;
}

// `DEBUG power stats`: time in each power mode since boot (CONFIG_PM_PROFILING),
// the proof that the chip really light-sleeps.
static void powerStats() {
#if defined(CONFIG_PM_PROFILING)
  static char buf[2048];
  std::memset(buf, 0, sizeof buf);
  if (FILE* f = fmemopen(buf, sizeof buf - 1, "w")) {
    esp_pm_dump_locks(f);
    std::fclose(f);
  }
  for (char* l = std::strtok(buf, "\n"); l; l = std::strtok(nullptr, "\n")) Serial.printf("[power] stats %s\n", l);
#else
  Serial.println("[power] stats: none (this framework has no CONFIG_PM_PROFILING)");
#endif
}

static void powerVerb(const char* a, const char* line) {
  using A = debugble::Board::PowerAsk;
  if (!std::strcmp(a, "stats")) return powerStats();
  A ask = A::Status;
  uint32_t idleMs = 0;
  if (!std::strcmp(a, "low")) ask = A::Low;
  else if (!std::strcmp(a, "wake")) ask = A::Wake;
  else if (!std::strcmp(a, "idle")) {
    char what[16] = "";
    std::sscanf(line, "%*s %*s %15s", what);
    const long sec = std::strtol(what, nullptr, 10);
    if (std::strcmp(what, "default") && (sec < 1 || sec > 3600))
      return (void)Serial.printf("[power] want: power idle <seconds 1..3600>|default\n");
    ask = A::Idle, idleMs = std::strcmp(what, "default") ? uint32_t(sec) * 1000 : 0;
  } else if (*a && std::strcmp(a, "status")) {
    return (void)Serial.printf("[power] want: power [status|low|wake|idle <seconds>|idle default|stats]\n");
  }
  const bool ok = powerAsk(ask, idleMs);
  char out[200];
  Serial.printf("[power] %s %s\n", ok ? "now" : "refused held_awake", powerLine(out, sizeof out, millis() + warpMs));
}

#include "wifi_ota.h"
static bool wifiYieldRadio() { return wififw::yieldRadio(); }
static bool wifiUpdateHoldsRadio() { return wififw::updateHoldsRadio(); }
static void wifiBegin() { wififw::begin(); }
static bool wifiAsks() { return wififw::asks(); }
static bool wifiQuietBoot() { return wififw::quietBoot(); }
static bool wifiPress(uint32_t ms) { return wififw::press(ms); }

// The debug link's PRESS and DOUBLE: never while a question owns the button.
static bool remotePress(uint32_t ms, bool twice) {
  if (updater.asking() || debugAsks() || wifiAsks()) return false;
  const uint32_t t = millis();
  bodyq::remote[0].ms = bodyq::remote[1].ms = 0;
  bodyq::remote[0].at = t;
  bodyq::remote[1].at = t + 250;
  bodyq::remote[1].ms = twice ? ms : 0;
  bodyq::remote[0].ms = ms;
  Serial.printf("[hw] debug ble %s %u ms\n", twice ? "double" : "press", unsigned(ms));
  return true;
}

#endif

void loop() {
#if !defined(BADGE_BOARD_SIM)
  if (const char* line = phone.takeDebug()) runDebug(line);
  const uint32_t now = millis() + warpMs;
  const bool holdFrame = sendShotLine();
#else
  const uint32_t now = millis();
  constexpr bool holdFrame = false;
#endif
  step(now);
#if !defined(BADGE_BOARD_SIM)
  dbgfw::poll();
#endif
#if !defined(BADGE_BOARD_SIM)
  applyPower(now);
  const bool dark = panelLow;
#else
  constexpr bool dark = false;
#endif
  if (!dark && !holdFrame && now - lastFrame >= kFrameMs) {
#if !defined(BADGE_BOARD_SIM)
    const uint32_t lookAt = micros();
#endif
    const blorb::Appearance look = onScreen(shown(), now);
#if !defined(BADGE_BOARD_SIM)
    const uint32_t shownAt = micros();
    // How long until it sleeps: full at any input, gone as it goes dark (docs/power.md).
    const uint16_t idleBar = uint16_t(uint64_t(powerPolicy.msLeft(now)) * 1000 / powerPolicy.idleMs());
    const std::optional<ota::View> update = updater.view(millis());
    const bool overlay = update || dbgfw::drawing() || wififw::drawing();
    const bool still = framememo::same(look, idleBar, overlay);   // the panel already shows this frame
    if (still) fps::same();
#else
    constexpr bool still = false;
#endif
    if (!still) {
#if !defined(BADGE_BOARD_SIM)
      const uint32_t drawAt = micros();
#endif
      drawFrame(look, now);
#if !defined(BADGE_BOARD_SIM)
      if (update) ota::drawUpdate(canvas, *update);
      else dbgfw::draw(canvas), wififw::draw(canvas);
      paint::drawIdleBar(canvas, idleBar);
      framememo::drew(look, idleBar, overlay);
      const uint32_t pushAt = micros(), seqBefore = boardFrameSeq();
#endif
      boardPresent(display, canvas);
#if !defined(BADGE_BOARD_SIM)
      fps::note(shownAt - lookAt, drawAt - shownAt, pushAt - drawAt, micros() - pushAt);
      lat::framed(look, seqBefore);   // a frame the memo skipped showed nothing new: it is no frame here
#endif
      ++framesDrawn;
    }
    lastFrame = now;
#if !defined(BADGE_BOARD_SIM)
    if (lightAfterFrame) {   // the first frame since the wake is on the glass: now the light
      lightAfterFrame = false, boardPanelIdle(), display.setBrightness(dish->settings().brightness);
      lat::rec->mark(latency::Ctl::Wake, latency::St::Ack, micros());
      lat::rec->mark(latency::Ctl::Wake, latency::St::AckPanel, micros());
    }
#endif
  }
#if !defined(BADGE_BOARD_SIM)
  lat::poll();
#endif
#if !defined(BADGE_BOARD_SIM)
  // The same anchor the Dish holds, so a soft reset's rtc reading counts only the unsaved gap.
  if (dish->timeKnown() && !hw::SystemClock::isSet())
    if (std::optional<uint32_t> wall = dish->wallNow()) hw::SystemClock::set(*wall);
  reportStatus(now);
  reportDuel();
  // What BOOT did, why a fight did or did not start, and the radio's answer:
  // on the cable and as `! DEBUG` events on the debug link (docs/debug-ble.md).
  for (char line[blorb::DiagLines::kLine]; dish->takeDiag(line) || radioDiag.take(line);) {
    Serial.printf("[diag] %s\n", line);
    dbgfw::link.writeLine(std::string("! DEBUG ") + line);
  }
  otaPackets.drain([](const uint8_t* p, size_t n, ota::Peer peer) { updater.data(p, n, millis(), peer); });
  updater.tick(millis());
  wififw::poll(millis());
  if (updater.rebootDue(millis()) && wififw::mayRestart()) restartIntoUpdate();
  trialWatchdog.beat(millis());
  if (trial.poll(millis()) != ota::Trial::Poll::Running) {   // kept, or a rollback that could not happen
    trialWatchdog.stop();
    store.releaseHold();
    dishStore.hold(false);
    dish->flush();
  }
  adoptTableSpecies(millis());
  if (std::optional<hw::NusLink::Drained> d = ble.takeDrained())
    if (d->bytes > 512 || bleTestPending) {
      bleTestPending = false;
      Serial.printf("[hw] ble tx chunks=%u retries=%u bytes=%u\n", unsigned(d->chunks), unsigned(d->retries),
                    unsigned(d->bytes));
    }
  // Asleep, the loop rests: the sense task's 50 Hz and the radio's tasks go on.
  if (panelLow) delay(kLowRestMs);
#endif
}
