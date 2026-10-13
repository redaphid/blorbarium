// The firmware loop, and the only file that includes Arduino, LovyanGFX or
// NimBLE. sim/sim.cpp includes this file whole, so the simulator runs this
// very loop and draws with these very objects.
#include <Arduino.h>
#include <esp_mac.h>

#include <array>
#include <optional>
#include <string_view>

#include "blorb/dish.h"
#include "grungo_pack.h"
#include "paint/shot.h"
#include "paint/sprite_pack.h"

#if defined(BADGE_BOARD_SIM)
#include "board_sim.h"
#include "mem_storage.h"
using PetStorage = blorbtest::MemStorage;   // every run founds the same pet: the goldens need it
#elif defined(BADGE_BOARD_LCD146)
#error "the 1.46's panel comes after build unit 20, which brings up the 1.28"
#else
#include <esp_heap_caps.h>
#include "hw/board_lcd128.h"
#include "hw/storage_nvs_fs.h"
#include "hw/system_clock.h"
#include "hw/link_nus.h"
#include "hw/ota_esp.h"
#include "hw/ota_pubkey.h"
#include "ota/held_storage.h"
#include "ota/screen.h"
#include "ota/trial_guard.h"
using PetStorage = hw::NvsFsStorage;
#endif

#include "hw/imu_qmi8658.h"
#include "hw/orient.h"
#include "debug_verbs.h"

// The protocol over the serial line. A cable has no connect event, so the
// owner counts as near once a line has arrived. A `DEBUG ` line is held back
// from the protocol for the loop to run after the tick (src/debug_verbs.h).
class SerialLink : public blorb::Link {
  char line_[256];   // past the protocol's 200, so an overlong line still reads as overlong
  size_t len_ = 0;
  bool heard_ = false;
  char debug_[sizeof(line_) + 1] = "";
  char taken_[sizeof(line_) + 1] = "";

 public:
  bool connected() override { return heard_; }
  const char* takeDebug() {
    if (!debug_[0]) return nullptr;
    std::memcpy(taken_, debug_, sizeof(taken_));
    debug_[0] = 0;
    return taken_;
  }
  std::optional<std::string_view> readLine() override {
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == '\r') continue;
      if (c != '\n') {
        if (len_ < sizeof(line_)) line_[len_++] = char(c);
        continue;
      }
      heard_ = true;
      const size_t n = len_;
      len_ = 0;
      if (n > 6 && std::memcmp(line_, "DEBUG ", 6) == 0) {
        std::memcpy(debug_, line_ + 6, n - 6);
        debug_[n - 6] = 0;
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

static constexpr uint32_t kFrameMs = 40;   // 25 fps: the SPI push is 23 ms of every frame

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
static hw::OtaLink otaLink(ble, updater);
static ota::Trial trial;
static hw::EspTrialPort trialPort;
static hw::TrialWatchdog trialWatchdog;
static hw::TwoLinks links(phone, otaLink);   // DEBUG lines stay on the cable: only SerialLink holds them back
// On trial nothing he does is written, so the twists that write his lineage wait.
static ota::TrialGuard host(links, [] { return trial.onTrial(); });
#endif
static std::optional<blorb::Dish> dish;    // built in setup(), once storage is up
static BoardDisplay display;
static paint::Canvas240 canvas;
static bool imuOk = false, tapReady = false;
static uint32_t lastSample = 0, lastFrame = 0;
static blorb::BodySample lastBody;
static orient::Up up{ORIENT_R0};

// The "tap me with your phone" marquee asks for the time. Until the website
// exists nothing can answer, and with no battery every replug forgets the
// time, so the board does not ask. The sim keeps it for its golden.
#if defined(BADGE_BOARD_SIM)
static constexpr bool kAskForTime = true;
#else
static constexpr bool kAskForTime = false;
#endif

static blorb::Appearance shown() {
  blorb::Appearance a = dish->appearance();
  if (!kAskForTime) a.timeUnknown = false;
  return a;
}

// `DEBUG press [ms]` holds BOOT down from pressAt for pressMs, so a prompt
// that waits on the button can be answered over the cable. 0 is no press.
static uint32_t pressAt = 0, pressMs = 0;

static blorb::BodySample readBody(uint32_t now) {
  blorb::BodySample s;
  if (!imuOk || !imu::read(s)) {
    s = blorb::BodySample{};
    s.az = 1000;   // no IMU: lying flat and still, so no gesture fires by accident
  }
  s.buttonDown = boardButtonDown() || now - pressAt < pressMs;
#if !defined(BADGE_BOARD_SIM)
  // While the dish asks about an update, the button answers it, and the pet
  // feels neither that press nor the rest of that hold. A cable's DEBUG press
  // answers too: the cable is a hand on the dish.
  static bool wasDown = false, otaOwns = false;
  if (updater.asking() && s.buttonDown && !wasDown) updater.press(millis());
  otaOwns = updater.asking() || (otaOwns && s.buttonDown);
  wasDown = s.buttonDown;
  if (otaOwns) s.buttonDown = false;
#endif
  return s;
}

// The engine's half of the loop: sense at 50 Hz, then run whatever 100 ms
// ticks are due. The simulator's warp calls this with no frames between.
static void step(uint32_t now) {
  if (now - lastSample >= blorb::kSampleMs) {
    lastBody = readBody(now);
    // The panel and the engine turn together, so downhill is down on the glass.
    const uint8_t rot = up.sample(lastBody, now);
    if (rot != display.getRotation()) display.setRotation(rot);
    dish->sample(up.toScreen(lastBody), now);
    lastSample = now;
  }
  dish->tick(now, host);
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

static ota::PetMark petMark() {
  const OccupantSummary o = summarize(dish->occupant());
  return {o.gen, o.genome};
}

bool DishOtaHost::busy() {
  // A clutch is chosen with the button, which the update's question would take.
  if (std::holds_alternative<blorb::Clutch>(dish->occupant())) return true;
  // Anything else he must finish first (a battle, one day) refuses here too.
  // A board that cannot save is refused by the save before the transfer; a
  // sealed one may update, since a newer firmware is how a sealed pet wakes.
  return false;
}
bool DishOtaHost::save() { return dish->flush(); }
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
  Serial.printf("[hw] psram size=%u free=%u\n", unsigned(ESP.getPsramSize()), unsigned(ESP.getFreePsram()));
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
  Serial.printf("[hw] boot=%s occupant=%s gen=%u genome=%08x age=%u\n", bootName(dish->boot()), o.kind,
                unsigned(o.gen), unsigned(o.genome), unsigned(o.age));
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
      "age=%u imu ax=%d ay=%d az=%d mg rot=%u t=%sC tap=%u button=%d\n",
      unsigned(now / 1000), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(ESP.getFreePsram()), unsigned(uxTaskGetStackHighWaterMark(nullptr)),
      unsigned(store.writes()), o.kind, unsigned(o.gen), unsigned(o.genome), unsigned(o.age), lastBody.ax,
      lastBody.ay, lastBody.az, unsigned(up.rotation()), temp, unsigned(lastBody.tapCode), lastBody.buttonDown ? 1 : 0);
}
#endif

void setup() {
  Serial.begin(115200);
  boardButtonBegin();
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
  options.rtc = &systemClock;
  options.sealed = blorb::leftAlone(storageState);
#else
  options.sealed = simSealed;
#endif
  dish.emplace(dishStore, blorb::fnv1a(mac, sizeof(mac)), lineage, options);
  [[maybe_unused]] bool panelUp = display.init();
#if !defined(BADGE_BOARD_SIM)
  display.setBrightness(dish->settings().brightness);
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
  const char* name = ble.begin(mac, &otaData);
  Serial.printf("[hw] ble name=%s heap=%u big=%u\n", name, unsigned(ESP.getFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
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
  pressAt = millis() + warpMs;
  pressMs = uint32_t(ms);
  Serial.printf("[hw] debug press %ld ms\n", ms);
}

static void runDebug(const char* line) {
  char verb[16] = "", a[32] = "";
  std::sscanf(line, "%15s %31s", verb, a);
  if (std::strcmp(verb, "bletest") == 0) return bleTest(a);
  if (std::strcmp(verb, "shot") == 0) return (void)shot.emplace(canvas);
  if (std::strcmp(verb, "press") == 0) return press(a);
  if (std::strcmp(verb, "stage") != 0) {
    Serial.printf("[hw] debug: unknown verb '%s' (try: stage adult, shot, press [ms], bletest <bytes>)\n", verb);
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
  if (!holdFrame && now - lastFrame >= kFrameMs) {
    paint::draw(shown(), grungoPack(), canvas, now);
#if !defined(BADGE_BOARD_SIM)
    if (std::optional<ota::View> v = updater.view(millis())) ota::drawUpdate(canvas, *v);
#endif
    boardPresent(display, canvas);
    lastFrame = now;
  }
#if !defined(BADGE_BOARD_SIM)
  // The same anchor the Dish holds, so a soft reset's rtc reading counts only the unsaved gap.
  if (dish->timeKnown() && !hw::SystemClock::isSet())
    if (std::optional<uint32_t> wall = dish->wallNow()) hw::SystemClock::set(*wall);
  reportStatus(now);
  otaPackets.drain([](const uint8_t* p, size_t n, ota::Peer peer) { updater.data(p, n, millis(), peer); });
  updater.tick(millis());
  if (updater.rebootDue(millis())) restartIntoUpdate();
  trialWatchdog.beat(millis());
  if (trial.poll(millis()) != ota::Trial::Poll::Running) {   // kept, or a rollback that could not happen
    trialWatchdog.stop();
    store.releaseHold();
    dishStore.hold(false);
    dish->flush();
  }
  if (std::optional<hw::NusLink::Drained> d = ble.takeDrained())
    if (d->bytes > 512 || bleTestPending) {
      bleTestPending = false;
      Serial.printf("[hw] ble tx chunks=%u retries=%u bytes=%u\n", unsigned(d->chunks), unsigned(d->retries),
                    unsigned(d->bytes));
    }
#endif
}
