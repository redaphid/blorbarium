// The paint bench on the S3 itself, with the board's compiler and flags:
// every frame paint_shots_dump.cpp wrote (outputs/paint_bench_s3/shots.inc)
// drawn as the loop draws it, counted in instructions by phase, hashed against the
// host's hash of the same frames. Run under Espressif's QEMU by
// scripts/test/paint_bench_qemu.sh (env paintbench_s3): no board, no port.
// Under -icount it counts instructions, not stalls; a flash cache miss costs
// nothing there, so it is the work, not the wait. The first board numbers
// (d76c1bf: 25.9 ms a frame at 240 MHz, QEMU 6.4M instructions) put the
// board near one instruction a cycle, so instructions / 240 is about the board's us.
#include <Arduino.h>
#include <xtensa/hal.h>

#include <cstddef>
#include <cstring>

#include "paint/phases.h"
#include "paint/sprite_pack.h"
#include "paint_shots.h"
#include "shots.inc"

static_assert(sizeof(blorb::Appearance) == kShotBytes, "the S3 lays Appearance out as the host does");
static_assert(offsetof(blorb::Appearance, line) == kShotLineAt && offsetof(blorb::Appearance, hangCharge) == kShotHangAt,
              "the S3 lays Appearance out as the host does");

namespace {

paint::Canvas240 cv;

uint64_t fnv(uint64_t h) {
  const auto* b = reinterpret_cast<const uint8_t*>(cv.px);
  for (size_t i = 0; i < sizeof cv.px; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
  return h;
}

uint32_t cycles() { return xthal_get_ccount(); }

uint64_t phases[uint8_t(paint::Phase::Count)];
void takePhases() {
  for (int i = 0; i < int(paint::Phase::Count); ++i) phases[i] += paint::phaseClock.spent[i], paint::phaseClock.spent[i] = 0;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  constexpr size_t kCount = sizeof kShots / sizeof kShots[0];
  paint::phaseClock.now = cycles;
  uint64_t dishCycles = 0, hash = 0xCBF29CE484222325ull, dishUs = 0;
  // Each species' frames by stage (0 the egg, then baby, child, adult, elder): the heavy pet is the one to beat.
  uint64_t bySpecies[blorb::kSpeciesCount][5] = {}, worst[blorb::kSpeciesCount][5] = {};
  uint32_t count[blorb::kSpeciesCount][5] = {};
  for (size_t i = 0; i < kCount; ++i) {
    blorb::Appearance a;
    std::memcpy(static_cast<void*>(&a), kShots[i].bytes, sizeof a);
    const uint32_t now = kShots[i].now, t0 = cycles(), u0 = micros();
    paint::draw(a, paint::packFor(a.species), cv, now);
    paint::drawBootHint(cv, paint::rimHint(a.boot));
    paint::drawIdleBar(cv, uint16_t(now / 7 % 1001));
    const uint32_t spent = cycles() - t0;
    dishCycles += spent;
    dishUs += micros() - u0;
    const int sp = a.species.v % blorb::kSpeciesCount, st = a.kind == blorb::Appearance::Kind::Egg ? 0 : 1 + int(a.stage) % 4;
    bySpecies[sp][st] += spent, ++count[sp][st], worst[sp][st] = spent > worst[sp][st] ? spent : worst[sp][st];
    takePhases();
    hash = fnv(hash);
  }
  const bool dishOk = hash == kDishHash;
  const std::vector<paint::DuelScene> duels = paintbench::duelShots();
  uint64_t duelCycles = 0, duelPhases[uint8_t(paint::Phase::Count)] = {};
  hash = 0xCBF29CE484222325ull;
  for (const paint::DuelScene& sc : duels) {
    const uint32_t t0 = cycles();
    paint::drawDuel(sc, paint::packFor(sc.me.look.species), paint::packFor(sc.them.look.species), cv, 0);
    duelCycles += cycles() - t0;
    for (int i = 0; i < int(paint::Phase::Count); ++i) duelPhases[i] += paint::phaseClock.spent[i], paint::phaseClock.spent[i] = 0;
    hash = fnv(hash);
  }
  const bool duelOk = hash == kDuelHash;
  // Under -icount shift=0 a virtual nanosecond is an instruction, and the
  // cycle counter ticks at its own rate (40 MHz of virtual time): instructions
  // a frame from the clock, and the phases scaled by the counter's ratio to it.
  const double perCycle = double(dishUs) * 1000.0 / double(dishCycles);
  Serial.printf("[bench] dish_frames=%u kinstr_per_frame=%u duel_frames=%u kinstr_per_frame=%u dish_hash=%s duel_hash=%s\n",
                unsigned(kCount), unsigned(dishUs / kCount), unsigned(duels.size()),
                unsigned(double(duelCycles) * perCycle / duels.size() / 1000), dishOk ? "ok" : "MISMATCH",
                duelOk ? "ok" : "MISMATCH");
  const char* names[] = {"bg", "fig", "rim", "text", "rings", "mask", "hint"};
  Serial.printf("[bench] phases kinstr/frame:");
  for (int i = 0; i < int(paint::Phase::Count); ++i)
    Serial.printf(" %s=%u", names[i], unsigned(double(phases[i]) * perCycle / kCount / 1000));
  // The duel: bg is the arena, fig both pets and the props, hint the BOOT hint, mask the round mask.
  Serial.printf("\n[bench] duel phases kinstr/frame:");
  for (int i = 0; i < int(paint::Phase::Count); ++i)
    Serial.printf(" %s=%u", names[i], unsigned(double(duelPhases[i]) * perCycle / duels.size() / 1000));
  Serial.printf("\n[bench] kinstr a frame by species, mean/worst: egg baby child adult elder\n");
  for (int sp = 0; sp < blorb::kSpeciesCount; ++sp) {
    Serial.printf("[bench]   %-9s", blorb::SPECIES[sp].name);
    for (int st = 0; st < 5; ++st)
      if (count[sp][st])
        Serial.printf(" %5u/%-5u", unsigned(double(bySpecies[sp][st]) * perCycle / count[sp][st] / 1000),
                      unsigned(double(worst[sp][st]) * perCycle / 1000));
      else
        Serial.printf(" %11s", "-");
    Serial.printf("\n");
  }
  Serial.printf("[bench] done\n");
}

void loop() { delay(1000); }
