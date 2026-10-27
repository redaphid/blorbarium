// Does a family look like a family? (gene-looks) Renders, through the real
// egg path and the real renderer, for each species asked:
//
//   gen0   founder A and founder B (another board's founder of his species)
//   gen1   A's clutch of three (Creature::layClutch, Clutch::child) and the
//          friend egg crossed with B (friend_egg.h childOf)
//   gen2   egg 0's clutch of three
//   line   one egg picked at random each generation: generations 5, 10, 20
//
// each as a portrait at `stage` (Child or Adult), one 240x240 PPM per pet in
// <out>, and one CSV row per pet in <out>/gene_looks.csv: what the renderer is
// handed (region tints, scale, aspect, mark). tools/gene_looks_sheet.py turns
// the PPMs into PNGs and a contact sheet.
//
//   gene_looks <out> [species,...|all] [child|adult]
//
// Built by `pio run -e genelooks` (through ~/Worktrees/blorbarium/pio).
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/appearance.h"
#include "blorb/founding.h"
#include "blorb/friend_egg.h"
#include "blorb/genes.h"
#include "blorb/mutate.h"
#include "blorb/species.h"
#include "paint/sprite_pack.h"

using namespace blorb;

namespace {

uint32_t boardSeed(uint32_t n) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
  return fnv1a(mac, sizeof mac);
}
Genome founderOf(SpeciesId s, uint32_t n) { return foundingGenome(s, boardSeed(n), paint::bandsFor(s)); }

bool writePpm(const paint::Canvas240& cv, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; ++i) {
    const uint16_t p = cv.px[i];
    const uint8_t px[3] = {uint8_t(((p >> 11) & 31) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63),
                           uint8_t((p & 31) * 255 / 31)};
    std::fwrite(px, 1, 3, f);
  }
  return std::fclose(f) == 0;
}

// The real egg path: the parent hatched as a Creature, its clutch laid as at a
// death, each egg made by Clutch::child, the friend slot by childOf.
std::vector<Genome> clutchOf(const Genome& parent, uint16_t generation, const Genome* friendGenome) {
  Egg egg(Offspring{parent, {}}, generation, 0);
  auto c = std::make_unique<Creature>(egg, 0, 0);
  Clutch k = c->layClutch(3, Fx::zero(), 0);
  std::vector<Genome> out;
  for (uint8_t i = 0; i < k.count; ++i) out.push_back(k.child(i).genome);
  if (friendGenome) {
    FriendEgg f{FriendEggRecord{}, *friendGenome};
    f.record.battleSeed = 0xF00D + generation;
    joinClutch(k, f.record.battleSeed);
    out.push_back(childOf(k, uint8_t(k.friendSlot - 1), &f).genome);
  }
  return out;
}

struct Shot { std::string label; Genome g; uint16_t gen; };

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: gene_looks <out> [species,...|all] [child|adult]\n"); return 2; }
  const std::string dir = argv[1];
  const char* which = argc > 2 ? argv[2] : "grungo,hushwing,puffkin";
  const Stage stage = argc > 3 && !std::strcmp(argv[3], "child") ? Stage::Child : Stage::Adult;
  const char* stageName = stage == Stage::Child ? "child" : "adult";
  FILE* csv = std::fopen((dir + "/gene_looks.csv").c_str(), "a");
  if (!csv) { std::fprintf(stderr, "gene_looks: cannot write %s\n", dir.c_str()); return 1; }
  auto cv = std::make_unique<paint::Canvas240>();
  for (const SpeciesInfo& s : SPECIES) {
    if (std::strcmp(which, "all") && !std::strstr(which, s.name)) continue;
    std::vector<Shot> shots;
    const Genome a = founderOf(s.id, 0x771c00), b = founderOf(s.id, 0x22b0b);
    shots.push_back({"gen0_A", a, 0});
    shots.push_back({"gen0_B", b, 0});
    std::vector<Genome> kids = clutchOf(a, 1, &b);
    for (size_t i = 0; i < kids.size(); ++i)
      shots.push_back({i + 1 < kids.size() ? "gen1_" + std::to_string(i) : "gen1_AxB", kids[i], 1});
    std::vector<Genome> grand = clutchOf(kids[0], 2, nullptr);
    for (size_t i = 0; i < grand.size(); ++i) shots.push_back({"gen2_" + std::to_string(i), grand[i], 2});
    Genome line = grand[0];
    for (uint16_t gen = 3; gen <= 20; ++gen) {
      std::vector<Genome> next = clutchOf(line, gen, nullptr);
      line = next[(gen * 2654435761u >> 7) % next.size()];
      if (gen == 5 || gen == 10 || gen == 20) shots.push_back({"gen" + std::to_string(gen) + "_line", line, gen});
    }
    for (const Shot& sh : shots) {
      Appearance ap = portrait(sh.g, stage, sh.gen, 0);
      paint::draw(ap, paint::packFor(ap.species), *cv, 0);
      const std::string name = "gene_looks_" + std::string(s.name) + "_" + stageName + "_" + sh.label;
      if (!writePpm(*cv, dir + "/" + name + ".ppm")) return 1;
      std::fprintf(csv, "%s,%s,%s,%s,%u,scale=%u,size=%u,build=%u", name.c_str(), s.name, stageName, sh.label.c_str(),
                   unsigned(sh.gen), unsigned(ap.scalePct), unsigned(ap.bulk), unsigned(ap.build));
      for (const Tint& t : ap.regions) std::fprintf(csv, ",%d/%u/%u", t.hue, unsigned(t.sat), unsigned(t.val));
      for (int m = 0; m < ap.markCount; ++m)
        std::fprintf(csv, ",m%u:%u:%d", unsigned(ap.marks[m].layer), unsigned(ap.marks[m].variant), ap.marks[m].tint.hue);
      std::fprintf(csv, "\n");
    }
    std::fprintf(stderr, "%s %s: %zu pets\n", s.name, stageName, shots.size());
  }
  std::fclose(csv);
  return 0;
}
