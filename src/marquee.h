#pragma once
// The full-screen marquee: any line, in enormous letters marching across the
// whole glass over everything (paint/banner.h, cyber-puck's banner). Included
// by main.cpp once `dish`, `canvas`, `powerPolicy` and `host` exist.
//
// Three ways up, all into showMarquee():
//   the cable:   DEBUG marquee <text>
//   a phone:     MARQUEE <text>            (#id MARQUEE <text>; OK shown=1|queued=1 len=<n>)
//   the Wi-Fi check-in's remote commands call showMarquee() themselves.
// The text is clamped by paint::clampBannerText: upper case, printable ASCII,
// at most kBannerMaxText (120, remote debug's rdebug::kMarqueeMax) characters.
//
// It wakes a dark screen and holds it awake while it runs. It goes round
// paint::bannerPasses times (two; more for a short word, to 8 s; once for one
// too wide for two in a minute), then
// gives the screen back to the pet. A press of the button takes it down, and the
// pet never feels that press. A duel is never interrupted: a marquee asked for
// during one (or while fight mode is searching) waits until it is over, and one
// running when a duel starts gives way and comes back after it. Only the newest
// waits: a second line replaces one not yet shown.
//
// All of its state, the link wrapper too, is one block in PSRAM made at the first tick:
// internal RAM is the scarce kind (a duel's radio takes about 37 KB of it).
// Call it from the loop task.
#include <cctype>
#include <cstring>
#include "paint/banner.h"

namespace marqueefw {

static bool post(const char* text, size_t* len = nullptr);
static bool fightOwns();

// `MARQUEE <text>` from a phone, answered here before the Dish sees the line;
// every other line passes through untouched.
class Link : public blorb::Link {
 public:
  explicit Link(blorb::Link& inner) : inner_(inner) {}
  bool connected() override { return inner_.connected(); }
  void writeLine(std::string_view s) override { inner_.writeLine(s); }
  std::optional<std::string_view> readLine() override {
    while (std::optional<std::string_view> line = inner_.readLine()) {
      if (!answer(*line)) return line;
    }
    return std::nullopt;
  }

 private:
  blorb::Link& inner_;

  bool answer(std::string_view line) {
    std::string_view rest = line;
    unsigned id = 0;
    if (rest.size() > 1 && rest[0] == '#') {
      size_t i = 1;
      for (; i < rest.size() && std::isxdigit(uint8_t(rest[i])); ++i)
        id = id * 16 + unsigned(std::isdigit(uint8_t(rest[i])) ? rest[i] - '0' : (std::tolower(rest[i]) - 'a' + 10));
      if (i == 1 || i >= rest.size() || rest[i] != ' ') return false;
      rest.remove_prefix(i + 1);
    }
    constexpr std::string_view kVerb = "MARQUEE";
    if (rest.substr(0, kVerb.size()) != kVerb || (rest.size() > kVerb.size() && rest[kVerb.size()] != ' ')) return false;
    rest.remove_prefix(std::min(rest.size(), kVerb.size() + 1));
    char text[paint::kBannerMaxText * 2];   // room for spaces the clamp will fold
    const size_t k = std::min(rest.size(), sizeof text - 1);
    std::memcpy(text, rest.data(), k);
    text[k] = '\0';
    size_t len = 0;
    char reply[64];
    const int n = post(text, &len)
                      ? std::snprintf(reply, sizeof reply, "#%x OK %s=1 len=%u", id, fightOwns() ? "queued" : "shown", unsigned(len))
                      : std::snprintf(reply, sizeof reply, "#%x ERR 400 BAD_ARGS", id);
    inner_.writeLine(std::string_view(reply, n > 0 ? size_t(n) : 0));
    return true;
  }
};

struct State {
  explicit State(blorb::Link& inner) : wire(inner) {}
  Link wire;                                     // the Dish's link, MARQUEE answered on the way
  char text[paint::kBannerMaxText + 1] = "";     // what is going round now
  char waiting[paint::kBannerMaxText + 1] = "";  // the next, until the screen is free for it
  uint32_t since = 0, ms = 0;
  bool on = false, owned = false;                // owned: the press that took it down, until let go
};
static State* st = nullptr;   // made at the first tick; the only internal RAM it takes is this pointer

static State* state() {
  if (st) return st;
  void* m = nullptr;
#if !defined(BADGE_BOARD_SIM)
  m = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  st = m ? new (m) State(host) : new State(host);
  return st;
}
// What the Dish reads the phone and the cable through.
static blorb::Link& link() { return state()->wire; }

// A duel, or fight mode looking for one: the screen and the button are its.
static bool fightOwns() { return dish && (dish->duelActive() || std::strcmp(dish->fightState(), "off") != 0); }

static bool on() { return st && st->on; }
static bool pending() { return st && st->waiting[0]; }
// Needs the screen awake: running, or due to run the moment the loop sees it.
static bool holds() { return on() || (pending() && !fightOwns()); }

// The clamped line, queued for the loop to put up. False when nothing of it is showable.
static bool post(const char* text, size_t* len) {
  char clean[paint::kBannerMaxText + 1];
  const size_t n = paint::clampBannerText(clean, text);
  if (len) *len = n;
  if (!n) return false;
  State* s = state();
  if (!s) return false;
  std::memcpy(s->waiting, clean, sizeof clean);
  Serial.printf("[marquee] queued \"%s\"%s\n", clean, fightOwns() ? " (after the duel)" : "");
  return true;
}

// Every loop, before the power policy: starts, yields and ends it.
static void tick(uint32_t now) {
  if (!st) return;
  if (st->on && fightOwns()) {   // a duel began: it gives way, and comes back after
    if (!st->waiting[0]) std::memcpy(st->waiting, st->text, sizeof st->text);
    st->on = false;
    Serial.printf("[marquee] waits for the duel\n");
  }
  if (st->on && now - st->since >= st->ms) {
    st->on = false;
    Serial.printf("[marquee] done\n");
  }
  if (st->waiting[0] && !fightOwns()) {
    std::memcpy(st->text, st->waiting, sizeof st->text);
    st->waiting[0] = '\0';
    st->since = now;
    st->ms = paint::bannerShowMs(paint::kMarqueeBanner, st->text);
    st->on = true;
#if !defined(BADGE_BOARD_SIM)
    powerPolicy.wakeNow(now);   // a dark screen lights for it
#endif
    Serial.printf("[marquee] up \"%s\" for %u ms\n", st->text, unsigned(st->ms));
  }
}

// The button, every sample: a press while it runs takes it down, and the pet
// feels none of that press. Returns what the pet should feel.
static bool takesButton(bool down) {
  if (!st) return down;
  if (down && st->on && !st->owned) {
    st->on = false;
    st->owned = true;
    Serial.printf("[marquee] dismissed\n");
  }
  if (!down) st->owned = false;
  return down && !st->owned;
}

static void draw(paint::Canvas240& cv, uint32_t now) { paint::drawBanner(cv, paint::kMarqueeBanner, st->text, now - st->since); }

}  // namespace marqueefw

// Put a line up on the whole screen (see the top of this file). For any caller
// on the loop task: the cable, the phone, the Wi-Fi check-in's remote commands.
// remote-debug's signature (src/remote_debug.h); nothing showable is logged and ignored.
static void showMarquee(const char* text) {
  if (!marqueefw::post(text)) Serial.printf("[marquee] nothing to show in \"%s\"\n", text ? text : "");
}
