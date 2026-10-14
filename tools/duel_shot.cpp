// The duel screen without a duel: draws test/support/duel_scenes.h's moments
// for any two species, or plays a whole scripted duel as frames for a film.
//
//   duel_shot --moment mercy --mine gilly --theirs fizz --out mercy.bmp [--ticks 12]
//   duel_shot --film outputs/duel-film --mine gilly --theirs fizz        frame_0000.bmp ... at 10 fps
//
// Built by `pio run -e duelshot` (tools/duel_film.sh does it in WSL). The same
// renderer as the badge, so a frame here is the frame on the glass.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include "blorb/appearance.h"
#include "duel_scenes.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"

namespace {

using paint::DuelAct;
using paint::DuelScene;
using paint::Side;

bool writeBmp(const paint::Canvas240& cv, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const int w = 240, h = 240, row = w * 3, size = 54 + row * h;
  uint8_t hdr[54] = {'B', 'M'};
  auto le32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[at + i] = uint8_t(v >> (8 * i)); };
  le32(2, uint32_t(size));
  le32(10, 54);
  le32(14, 40);
  le32(18, w);
  le32(22, h);
  hdr[26] = 1;
  hdr[28] = 24;
  le32(34, uint32_t(row * h));
  std::fwrite(hdr, 1, 54, f);
  for (int y = h - 1; y >= 0; --y)
    for (int x = 0; x < w; ++x) {
      const uint16_t p = cv.px[y * w + x];
      const uint8_t px[3] = {uint8_t((p & 31) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63),
                             uint8_t(((p >> 11) & 31) * 255 / 31)};
      std::fwrite(px, 1, 3, f);
    }
  return std::fclose(f) == 0;
}

bool species(const char* name, blorb::SpeciesId& out) {
  for (const blorb::SpeciesInfo& s : blorb::SPECIES)
    if (!std::strcmp(s.name, name)) { out = s.id; return true; }
  return false;
}

// A whole mortal duel, beat by beat, for a film: they meet, both kids take the
// skull, a guard that holds, a bonk of heads, my kid winds up a heavy special
// (the friend's own quick one telegraphed beside it), it lands with a WHAM, my pet stands over the friend's, the thumb goes down, he falls,
// and mine takes his token.
struct Beat { const char* moment; uint16_t frames; void (*tweak)(DuelScene&, int turn); };

void noTweak(DuelScene&, int) {}
void stakeHold(DuelScene& s, int t) {
  s.me.chose = s.them.chose = blorb::Stakes::Mortal;
  s.me.ceiling = blorb::Stakes::Mortal;
  s.me.hold = uint8_t(t * 255 / 30 < 255 ? t * 255 / 30 : 255);
  s.me.confirmed = t >= 30;
  s.them.confirmed = t >= 12;
  s.stakes = blorb::Stakes::Mortal;
}
void turn1Choose(DuelScene& s, int t) {
  s.them.hearts = 5;
  s.me.hearts = 5;
  s.ticksLeft = uint16_t(100 - t * 3);
  s.aim = int8_t(t < 8 ? -1 : int(t < 14 ? blorb::Stance::Pounce : blorb::Stance::Guard));
  s.me.locked = t >= 18;
  s.them.locked = t >= 10;
  s.me.armed = false;
}
void turn1Clash(DuelScene& s, int) {
  s.me.hearts = 5;
  s.them.hearts = 4;
  s.me.fired = false;
  s.super = false;
}
void turn2Clash(DuelScene& s, int) {
  s.me.hearts = 5;
  s.them.hearts = 4;
}
void mercyHold(DuelScene& s, int t) {
  s.finishHold = uint8_t(t < 10 ? 0 : (t - 10) * 255 / 30 < 255 ? (t - 10) * 255 / 30 : 255);
  s.mercyLeft = uint16_t(150 - t * 2);
  s.pleading = true;
}

constexpr Beat kFilm[] = {
    {"meeting", 24, noTweak},   {"staking_mortal", 36, stakeHold}, {"choosing", 30, turn1Choose},
    {"hit", 30, turn1Clash},    {"bonk", 30, turn2Clash},          {"windup_heavy", 30, noTweak},
    {"landing", 30, noTweak},
    {"mercy", 44, mercyHold},   {"fallen", 34, noTweak},           {"outcome", 30, noTweak},
};

int film(const std::string& dir, blorb::SpeciesId mine, blorb::SpeciesId theirs) {
  auto cv = std::make_unique<paint::Canvas240>();
  int n = 0;
  for (const Beat& b : kFilm)
    for (int t = 0; t < b.frames; ++t) {
      DuelScene s = blorbtest::duelScene(b.moment, mine, theirs);
      s.ticks = uint16_t(t);
      b.tweak(s, t);
      if (!std::strcmp(b.moment, "outcome")) {   // the film's ending is the mortal one
        s.stakes = blorb::Stakes::Mortal;
        s.verdict = blorb::Verdict::Finish;
        s.won = Side::Me;
        s.me.hearts = 5;
        s.them.hearts = 0;
      }
      paint::drawDuel(s, paint::packFor(mine), paint::packFor(theirs), *cv, 0);
      char name[32];
      std::snprintf(name, sizeof name, "/frame_%04d.bmp", n++);
      if (!writeBmp(*cv, dir + name)) { std::fprintf(stderr, "duel_shot: cannot write %s%s\n", dir.c_str(), name); return 1; }
    }
  std::printf("%d frames at 10 fps in %s\n", n, dir.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string moment = "meeting", out = "duel.bmp", filmDir;
  blorb::SpeciesId mine = blorb::kGrungo, theirs = blorb::kGrungo;
  int ticks = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
    if (!v) { std::fprintf(stderr, "duel_shot: %s needs a value\n", a.c_str()); return 2; }
    ++i;
    if (a == "--moment") moment = v;
    else if (a == "--out") out = v;
    else if (a == "--film") filmDir = v;
    else if (a == "--ticks") ticks = std::atoi(v);
    else if (a == "--mine" || a == "--theirs") {
      if (!species(v, a == "--mine" ? mine : theirs)) { std::fprintf(stderr, "duel_shot: %s is not a species\n", v); return 2; }
    } else { std::fprintf(stderr, "duel_shot: unknown %s\n", a.c_str()); return 2; }
  }
  if (!filmDir.empty()) return film(filmDir, mine, theirs);
  bool known = false;
  for (const blorbtest::DuelMoment& m : blorbtest::kDuelMoments) known |= moment == m.name;
  if (!known) { std::fprintf(stderr, "duel_shot: %s is not a moment\n", moment.c_str()); return 2; }
  DuelScene s = blorbtest::duelScene(moment.c_str(), mine, theirs);
  if (ticks >= 0) s.ticks = uint16_t(ticks);
  auto cv = std::make_unique<paint::Canvas240>();
  paint::drawDuel(s, paint::packFor(mine), paint::packFor(theirs), *cv, 0);
  return writeBmp(*cv, out) ? 0 : 1;
}
