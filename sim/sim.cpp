// The dish, in a window or headless.
//
// The firmware is included whole rather than linked, so setup(), loop(),
// `dish`, `display` and `canvas` are the very objects src/main.cpp uses: there
// is no second implementation here to drift. What this file adds is what a
// desk lacks: a hand on the IMU (Wire.h), a serial line, a clock, and a few
// debug verbs that reach into the engine.
//
//   sim                                  protocol on stdin, replies on stdout
//   sim --headless ...                   no window and no display
//   sim --clock fixed                    time is a counter the sim moves: one feed, the same bytes every run
//   sim --script feed.txt                lines stamped `@<ms>`; brings the fixed clock
//   sim --shot out.bmp --after 3000      one frame, then quit
//   sim --record dir/ --fps 20 --for 8000   every frame to dir/, then quit
//   sim --sealed                         boot as a board whose keepsake would not open
//   sim --species <slug|id>              draw the pet as that species (DEBUG species does it live)
//   sim --air-fd <n> [--air-weather loss=30,dup=10,reorder=20,late=800,latency=3,rssi=-45,seed=1]
//                                        the duel radio over a socket another sim holds the far end of
//                                        (sim/serial_air.h; tools/duel_film.py joins two)
//   sim --kid <seed>[:<from ms>[:<turn ms>]]   a kid playing a duel on the hand (sim/duel_bot.h)
//
// Keys with a window: space the button, k a knock, d a double knock, s held to
// shake, arrows tilt.
//
// Script and stdin lines:
//   !shake [ms]  !knock  !dtap  !tilt <x> <y>  !flip  !lid [ms]  !hold [ms]  !button [ms]  !care <ms>  !shot <path>
//   !air open  !air close  !air ping <word>   (the far sim answers a ping with `pong <word>`)
//   DEBUG warp <ticks>  DEBUG inject <chem> <level>  DEBUG force <action>  DEBUG die  DEBUG time <unix> [tzMinutes]
//   DEBUG stage <hatchling|child|adult|elder>  DEBUG species <slug|id|off>
// Anything else is a protocol line, as a phone would send it.
#include "../src/main.cpp"
#include "duel_bot.h"
#include "serial_air.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

// The loop spins in 5 ms steps, so main.cpp samples at exactly 50 Hz and
// draws every 40 ms, as it does on the board. A coarser step would sample at
// the step's rate, and the detectors' thresholds assume 50 Hz.
static constexpr uint32_t kStepMs = 5;

// 24-bit BMP, bottom-up. rgb888_t is b,g,r in memory, which is what the
// format wants. Read from the panel, not the canvas: a shot is what the glass shows.
static bool filming = false;
static void shoot(const char* path) {
  static uint8_t px[CANVAS_W * CANVAS_H * 3];
  display.readRect(0, 0, CANVAS_W, CANVAS_H, reinterpret_cast<lgfx::rgb888_t*>(px));
  const int stride = (CANVAS_W * 3 + 3) & ~3;
  const uint32_t bytes = uint32_t(stride) * CANVAS_H;
  const uint32_t size = 54 + bytes;
  FILE* f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "sim: cannot write %s\n", path); exit(1); }
  const uint8_t head[54] = {
      'B', 'M', uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24),
      0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
      uint8_t(CANVAS_W), uint8_t(CANVAS_W >> 8), 0, 0, uint8_t(CANVAS_H), uint8_t(CANVAS_H >> 8), 0, 0,
      1, 0, 24, 0, 0, 0, 0, 0,
      uint8_t(bytes), uint8_t(bytes >> 8), uint8_t(bytes >> 16), uint8_t(bytes >> 24),
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  fwrite(head, 1, sizeof(head), f);
  const uint8_t pad[3] = {0, 0, 0};
  for (int y = CANVAS_H - 1; y >= 0; y--) {
    fwrite(&px[y * CANVAS_W * 3], 1, CANVAS_W * 3, f);
    fwrite(pad, 1, stride - CANVAS_W * 3, f);
  }
  fclose(f);
  if (!filming) fprintf(stderr, "sim: wrote %s\n", path);
}

// A feed that knows when each line goes: `@<ms> <line>`, counted on the
// script's clock (which a warp does not move), and an unstamped line at once.
struct ScriptLine { uint32_t at; std::string text; };
static std::vector<ScriptLine> script;
static size_t scriptNext = 0;

static bool scriptLoad(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) { fprintf(stderr, "sim: cannot read script %s\n", path); return false; }
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0] || line[0] == '#') continue;
    uint32_t at = 0;
    const char* text = line;
    if (line[0] == '@') {
      char* rest = nullptr;
      at = uint32_t(strtoul(line + 1, &rest, 10));
      if (rest == line + 1 || *rest != ' ') {
        fprintf(stderr, "sim: script line needs `@<ms> <line>`: %s\n", line);
        fclose(f);
        return false;
      }
      text = rest + 1;
    }
    script.push_back({at, text});
  }
  fclose(f);
  std::stable_sort(script.begin(), script.end(), [](const ScriptLine& a, const ScriptLine& b) { return a.at < b.at; });
  return true;
}

// ---- the hand ------------------------------------------------------------------
// A press is a pin held LOW until a time and released by release() each step;
// the firmware times holds itself, which is the point.
struct SimPress { int pin; uint32_t until; };
static SimPress presses[8];

static void press(int pin, uint32_t ms) {
  for (auto& p : presses) {
    if (p.until) continue;
    p = {pin, millis() + ms};
    lgfx::v1::gpio_lo(pin);
    return;
  }
}

static void release() {
  const uint32_t now = millis();
  for (auto& p : presses) {
    if (!p.until || simBefore(now, p.until)) continue;
    lgfx::v1::gpio_hi(p.pin);
    p.until = 0;
  }
}

static uint32_t argMs(const char* arg, uint32_t fallback) { return arg && *arg ? uint32_t(atoi(arg)) : fallback; }

// ---- the radio ---------------------------------------------------------------
// Every frame that crosses is one stderr line, which is what a film reads:
//   air <ms> sent|received|lost|deaf <seq> <the other mac> <len> <text, or hex>
static std::optional<simair::SerialAir> air;

static void logAir(const simair::Event& e) {
  static const char* const kWhat[] = {"sent", "received", "lost", "deaf"};
  char text[2 * blorb::kDatagramMax + 1];
  bool printable = true;
  for (size_t i = 0; i < e.len; i++) printable &= e.bytes[i] >= ' ' && e.bytes[i] < 127;
  if (printable) snprintf(text, sizeof(text), "%.*s", int(e.len), (const char*)e.bytes);
  else for (size_t i = 0; i < e.len; i++) snprintf(text + 2 * i, 3, "%02x", e.bytes[i]);
  const uint8_t* m = e.peer.mac;
  fprintf(stderr, "air %u %s %u %02x%02x%02x%02x%02x%02x %u %s\n", e.at, kWhat[int(e.what)], e.seq, m[0], m[1], m[2],
          m[3], m[4], m[5], unsigned(e.len), text);
}

// Until the Dish takes the radio (D9's DishOptions::air), the sim reads it
// itself and answers pings, so two linked sims can be seen talking.
static void airLine(const char* verb, const char* word) {
  if (!air) { fprintf(stderr, "sim: !air %s needs --air-fd\n", verb); return; }
  if (!strcmp(verb, "open")) air->open();
  else if (!strcmp(verb, "close")) air->close();
  else if (!strcmp(verb, "ping")) {
    const std::string s = std::string("ping ") + word;
    air->broadcast((const uint8_t*)s.data(), s.size());
  } else fprintf(stderr, "sim: !air open|close|ping <word>, not %s\n", verb);
}

static void answerPings() {
  while (auto d = air->receive()) {
    if (d->len < 5 || memcmp(d->bytes, "ping ", 5)) continue;
    d->bytes[1] = 'o';
    air->send(d->from, d->bytes, d->len);
  }
}

static std::optional<duelbot::KidBot> kid;

// ---- the caretaker -------------------------------------------------------------
// `!care <ms>`: for that long, warps included, an owner glances at the glass
// every 30 s and makes the gesture the care hint names (care.def), then rests,
// as test/support/scripted_owner.h does from his drives. BOOT only when the
// dish shows no pellet: a second would rot before he wanted it, and he eats rot.
static uint32_t careUntil = 0, careNextLook = 0;

static void care() {
  constexpr uint32_t kLookMs = 30000, kRestMs = 2000, kPressMs = 200, kCradleMs = 4000, kTuckMs = 10 * 60000;
  const uint32_t now = millis();
  if (!simBefore(now, careUntil) || simBefore(now, careNextLook)) return;
  careNextLook = now + kLookMs;
  const blorb::Appearance a = dish->appearance();
  if (!a.hasHint) return;
  blorb::StimId gesture{};
  for (const auto& c : blorb::CARES)
    if (c.id.v == a.hint.v) gesture = c.gesture;
  SimHand& h = Wire.hand;
  if (gesture.v == blorb::stim::button.v) {
    for (uint8_t i = 0; i < a.itemCount; i++)
      if (a.items[i].what != blorb::Appearance::Item::What::Marble) return;
    press(PIN_BOOT_BUTTON, kPressMs);
    careNextLook = now + kPressMs + kRestMs;
  } else if (gesture.v == blorb::stim::cradle.v) {
    h.holdUntil = now + kCradleMs;
    careNextLook = now + kCradleMs + kRestMs;
  } else if (gesture.v == blorb::stim::knock.v) {
    h.tap = 1;
  } else if (gesture.v == blorb::stim::lid_down.v) {
    h.lidUntil = now + kTuckMs;
    careNextLook = now + kTuckMs + kRestMs;
  }
}

static void hand(const char* word, const char* a, const char* b) {
  SimHand& h = Wire.hand;
  const uint32_t now = millis();
  if (!strcmp(word, "shake")) h.shakeUntil = now + argMs(a, 1200);
  else if (!strcmp(word, "knock")) h.tap = 1;
  else if (!strcmp(word, "dtap")) h.tap = 2;
  else if (!strcmp(word, "tilt")) { h.tiltX = atoi(a); h.tiltY = atoi(b); h.lidded = false; h.lidUntil = 0; }
  else if (!strcmp(word, "flip")) h.flipUntil = now + 600;   // past kFaceDownMg and back, short of a lid
  else if (!strcmp(word, "lid")) { if (*a) h.lidUntil = now + argMs(a, 0); else h.lidded = true; }
  else if (!strcmp(word, "hold")) h.holdUntil = now + argMs(a, 5000);
  else if (!strcmp(word, "button")) press(PIN_BOOT_BUTTON, argMs(a, 150));
  else if (!strcmp(word, "care")) { careUntil = now + argMs(a, 0); careNextLook = now; }
  else if (!strcmp(word, "shot")) shoot(a);
  else if (!strcmp(word, "air")) airLine(a, b);
  else fprintf(stderr, "sim: unknown hand line !%s\n", word);
}

// ---- debug verbs ---------------------------------------------------------------
// They reach into the engine through the Dish's own accessors and never cross
// the wire, so nothing here can leak into the protocol (src/debug_verbs.h).

static blorb::Creature* creature() { return std::get_if<blorb::Creature>(&dish->occupant()); }

static void warp(uint32_t ticks) {
  const uint32_t until = millis() + ticks * blorb::kTickMs;
  while (simBefore(millis(), until)) {
    simWarpMs += blorb::kSampleMs;
    release();
    care();
    step(millis());
  }
}

static void debug(const char* verb, const char* a, const char* b) {
  if (!strcmp(verb, "warp")) { warp(uint32_t(strtoul(a, nullptr, 10))); return; }
  if (!strcmp(verb, "time")) {
    // What the TIME verb does, without a phone in range: feed the phone
    // source, let the Dish catch up, then snap the pet day to the wall.
    const uint32_t unix = uint32_t(strtoul(a, nullptr, 10));
    dish->phoneTime().set(unix);
    dish->checkWall();
    dish->clock().alignToWall(unix, int16_t(atoi(b)));
    return;
  }
  if (!strcmp(verb, "species")) {
    blorb::SpeciesId s;
    if (!strcmp(a, "off")) pinnedSpecies.reset();
    else if (debugverbs::speciesByName(a, s)) pinnedSpecies = s;
    else fprintf(stderr, "sim: DEBUG species: %s is not a species.def name or 0..255\n", a);
    return;
  }
  if (!strcmp(verb, "stage")) {
    if (const char* why = debugverbs::growTo(*dish, a, warp)) fprintf(stderr, "sim: DEBUG stage %s: %s\n", a, why);
    return;
  }
  blorb::Creature* c = creature();
  if (!c) { fprintf(stderr, "sim: DEBUG %s needs a creature in the dish\n", verb); return; }
  if (!strcmp(verb, "inject")) {
    blorb::ChemId id;
    if (!debugverbs::chemByName(a, id)) { fprintf(stderr, "sim: no chemical or drive named %s\n", a); return; }
    c->inject(id, debugverbs::parseLevel(b));
  } else if (!strcmp(verb, "force")) {
    for (const auto& act : blorb::ACTIONS)
      if (!strcmp(act.name, a)) { c->force(act.id); return; }
    fprintf(stderr, "sim: no action named %s\n", a);
  } else if (!strcmp(verb, "think")) {
    for (const auto& t : blorb::THOUGHTS)
      if (!strcmp(t.name, a)) { dish->think(t.id); return; }
    fprintf(stderr, "sim: no thought named %s\n", a);
  } else if (!strcmp(verb, "die")) {
    // Life to zero: the genome's own receptors on low life write die and
    // cause, so the death runs the path a real one does.
    c->inject(blorb::chem::life, blorb::Fx::zero());
  } else {
    fprintf(stderr, "sim: unknown DEBUG %s\n", verb);
  }
}

static void simLine(const std::string& line) {
  char word[32] = "", a[480] = "", b[64] = "";
  if (line[0] == '!') {
    sscanf(line.c_str() + 1, "%31s %479s %63s", word, a, b);
    hand(word, a, b);
  } else {
    sscanf(line.c_str() + 6, "%31s %479s %63s", word, a, b);
    debug(word, a, b);
  }
}

// ---- the loop ------------------------------------------------------------------
static const char* shotPath = nullptr;
static uint32_t shotAfterMs = 3000;
static const char* recordDir = nullptr;
static uint32_t recordEveryMs = 50;
static uint32_t recordForMs = 6000;

static int run(bool* running) {
  setup();
  const uint32_t started = millis();
  uint32_t nextFrameAt = 0, frameNo = 0;
  while (*running) {
    const uint32_t frame = millis();
    const uint32_t since = frame - started - simWarpMs;   // the script's clock
    if (air) air->begin(since);
    while (scriptNext < script.size() && script[scriptNext].at <= since) Serial.arrive(script[scriptNext++].text.c_str());
    for (duelbot::Move m; kid && kid->next(since, m);) {
      if (*m.why) fprintf(stderr, "kid %u %s\n", since, m.why);
      simPending.push_back(m.line);
    }
    std::vector<std::string> due;
    due.swap(simPending);   // a warp reads stdin and may queue more; those wait a frame
    for (const std::string& line : due) simLine(line);
    release();
    care();
    loop();
    if (air) answerPings();
    const uint32_t after = millis() - started - simWarpMs;
    if (shotPath && after >= shotAfterMs) {
      shoot(shotPath);
      exit(0);
    }
    if (recordDir) {
      if (after >= nextFrameAt) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%05u.bmp", recordDir, frameNo++);
        shoot(path);
        nextFrameAt += recordEveryMs;   // stepped, so a slow frame never drifts the clip
      }
      if (after >= recordForMs) exit(0);
    }
    delay(kStepMs);
    // On the fixed clock that delay was spent instantly. One real millisecond
    // a frame anyway, so a window's SDL thread still gets the machine.
    if (simClockFixed && frame / kFrameMs != millis() / kFrameMs) lgfx::v1::delay(1);
  }
  return 0;
}

int main(int argc, char** argv) {
  int airFd = -1;
  simair::Weather weather;
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "--shot") && i + 1 < argc) shotPath = argv[++i];
    else if (!strcmp(a, "--after") && i + 1 < argc) shotAfterMs = uint32_t(atoi(argv[++i]));
    else if (!strcmp(a, "--record") && i + 1 < argc) { recordDir = argv[++i]; filming = true; }
    else if (!strcmp(a, "--fps") && i + 1 < argc) recordEveryMs = 1000 / uint32_t(atoi(argv[++i]));
    else if (!strcmp(a, "--for") && i + 1 < argc) recordForMs = uint32_t(atoi(argv[++i]));
    // SDL's dummy video driver: the panel still draws into its framebuffer, so
    // a shot is the same frame, but nothing asks for a display.
    else if (!strcmp(a, "--headless")) setenv("SDL_VIDEODRIVER", "dummy", 1);
    else if (!strcmp(a, "--sealed")) simSealed = true;
    else if (!strcmp(a, "--species") && i + 1 < argc) {
      blorb::SpeciesId s;
      if (!debugverbs::speciesByName(argv[++i], s)) {
        fprintf(stderr, "sim: --species %s: not a species.def name or 0..255\n", argv[i]);
        return 2;
      }
      pinnedSpecies = s;
    }
    else if (!strcmp(a, "--clock") && i + 1 < argc) {
      const char* c = argv[++i];
      if (strcmp(c, "fixed") && strcmp(c, "wall")) { fprintf(stderr, "sim: --clock fixed|wall, not %s\n", c); return 2; }
      simClockFixed = !strcmp(c, "fixed");
    }
    // A script is only worth writing if it plays the same way twice, which
    // the wall clock does not, so it brings the fixed clock with it.
    else if (!strcmp(a, "--script") && i + 1 < argc) {
      if (!scriptLoad(argv[++i])) return 2;
      simClockFixed = true;
    }
    else if (!strcmp(a, "--air-fd") && i + 1 < argc) airFd = atoi(argv[++i]);
    else if (!strcmp(a, "--air-weather") && i + 1 < argc) {
      if (const char* why = simair::parseWeather(argv[++i], weather)) {
        fprintf(stderr, "sim: --air-weather %s: %s\n", argv[i], why);
        return 2;
      }
    }
    else if (!strcmp(a, "--kid") && i + 1 < argc) {
      unsigned seed = 0, from = 1000, turn = 10000;
      if (sscanf(argv[++i], "%u:%u:%u", &seed, &from, &turn) < 1 || turn < 3000) {
        fprintf(stderr, "sim: --kid <seed>[:<from ms>[:<turn ms, at least 3000>]], not %s\n", argv[i]);
        return 2;
      }
      kid.emplace(seed, from, turn);
    }
    else { fprintf(stderr, "sim: unknown argument %s\n", a); return 2; }
  }
  if (airFd >= 0) {
    blorb::PeerAddr self;
    esp_read_mac(self.mac, ESP_MAC_WIFI_STA);
    air.emplace(airFd, self, weather);
    air->onEvent = logAir;
  }

  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
  Serial.attach(STDIN_FILENO, STDOUT_FILENO, false);

  // LovyanGFX's own shortcuts (r/l rotate, 1..6 resize) go behind alt, or the
  // dish turns sideways the first time someone types at it.
  lgfx::Panel_sdl::setShortcutKeymod(KMOD_ALT);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_SPACE, PIN_BOOT_BUTTON);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_k, SIM_PIN_KNOCK);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_d, SIM_PIN_DTAP);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_s, SIM_PIN_SHAKE);
  // Pressed is LOW, so every pin starts high or the dish boots held, knocked and shaken.
  for (int pin : {PIN_BOOT_BUTTON, SIM_PIN_KNOCK, SIM_PIN_DTAP, SIM_PIN_SHAKE,
                  SIM_PIN_UP, SIM_PIN_DOWN, SIM_PIN_LEFT, SIM_PIN_RIGHT})
    lgfx::v1::gpio_hi(pin);

  return lgfx::Panel_sdl::main(run);
}
