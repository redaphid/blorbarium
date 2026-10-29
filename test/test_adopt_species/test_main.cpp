// The eight badges flashed with 0.4.0 hold grungos with no SpeciesGene. The
// release makes each one its badge's species and keeps the pet (founding.h
// adoptSpecies, Dish::adoptSpecies; decisions-2026-10-15.md D3). Every board
// is treated alike: board 237c's pet adopts grungo the same way.
#include <gtest/gtest.h>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include "blorb/founding.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/ble_name.h"
#include "blorb/dish.h"
#include "blorb/genes.h"
#include "founding_040.h"
#include "links.h"
#include "mem_storage.h"
#include "ota/held_storage.h"
#include "ota/trial.h"
#include "paint/sprite_pack.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint64_t kLineageId = 0xd40592771b3cull;

// The batch as flash_log.csv has it: each badge's MAC, its table species, and
// the genome hash its boot printed when 0.4.0 founded it.
struct FieldBadge { uint8_t mac[6]; const char* species; uint32_t genome040; };
constexpr FieldBadge kBatch[] = {
    {{0xd4, 0x05, 0x92, 0x77, 0x1b, 0x3c}, "mossby", 0x2ea98ea9},
    {{0xd4, 0x05, 0x92, 0x77, 0x1b, 0x44}, "puffkin", 0x4cc58619},
    {{0xd4, 0x05, 0x92, 0x77, 0x1b, 0x34}, "gilly", 0x8348e859},
    {{0xd4, 0x05, 0x92, 0x77, 0x1c, 0xb0}, "mothwick", 0xe28ed3e0},
    {{0xd4, 0x05, 0x92, 0x77, 0x1b, 0x30}, "mudge", 0xf9e011c0},
    {{0xd4, 0x05, 0x92, 0x77, 0x1b, 0x40}, "fizz", 0xfc33da63},
    {{0xd4, 0x05, 0x92, 0x77, 0x22, 0x58}, "hushwing", 0x261b57fb},
    {{0xd4, 0x05, 0x92, 0x77, 0x22, 0x40}, "tinker", 0x4e1d8f2b},
};
constexpr uint8_t k237cMac[6] = {0xd4, 0x05, 0x92, 0x77, 0x23, 0x7c};   // badges.def row 0, grungo

uint32_t seedOf(const uint8_t mac[6]) { return fnv1a(mac, 6); }
SpeciesId tableOf(const FieldBadge& b) { return *tableSpecies(b.mac); }

std::string lineName(SpeciesId s) {
  char n[16];
  speciesLineName(s, n);
  return n;
}

// A board that can be unplugged: the first `cutAfter` writes land (the last
// of them torn after `tearBytes` when set), and none after, as when the power
// goes mid-save.
struct Flash : MemStorage {
  std::optional<size_t> cutAfter;
  std::optional<size_t> tearBytes;
  size_t writes = 0;
  bool allowed() {
    if (cutAfter && writes >= *cutAfter) return false;
    if (tearBytes && cutAfter && writes + 1 == *cutAfter) tearAt(*tearBytes);
    ++writes;
    return true;
  }
  bool writeAtomic(const char* n, const uint8_t* d, size_t len) override {
    return allowed() && MemStorage::writeAtomic(n, d, len);
  }
  bool append(const char* n, const uint8_t* d, size_t len) override { return allowed() && MemStorage::append(n, d, len); }
  bool rename(const char* f, const char* t) override { return allowed() && MemStorage::rename(f, t); }
};

// A badge's life: founded by 0.4.0 this morning, hatched, a while lived, saved.
struct Badge {
  const FieldBadge& field;
  Flash flash;
  std::optional<Dish> dish;
  NullLink link;
  uint32_t ms = 0;

  explicit Badge(const FieldBadge& f) : field(f) {}
  uint32_t seed() const { return seedOf(field.mac); }
  // What both firmwares do at boot; only 0.4.0 founds from foundingGenome040.
  Dish& boot(Storage& store) {
    const Genome founder = foundingGenome040(seed());
    DishOptions o;
    o.founder = &founder;
    dish.emplace(store, seed(), kLineageId, o);
    ms = 0;
    dish->tick(ms, link);
    return *dish;
  }
  Dish& boot() { return boot(flash); }
  void run(uint32_t forMs) {
    for (const uint32_t end = ms + forMs; ms < end;) dish->tick(ms += 1000, link);
  }
  void hatch() {
    for (int s = 0; !std::holds_alternative<Creature>(dish->occupant()); ++s) {
      if (s == 3600) return ADD_FAILURE() << "no hatch within an hour";
      run(1000);
    }
  }
  // Founded and lived on 0.4.0: hatched, then about `minutes` of life, saved.
  void liveOn040(uint32_t minutes = 20) {
    boot();
    hatch();
    run(minutes * 60000);
    ASSERT_TRUE(dish->flush());
  }
  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  Dish::Adoption adopt() { return dish->adoptSpecies(tableOf(field), paint::bandsFor(tableOf(field))); }
};

std::map<uint16_t, std::vector<uint8_t>> genesByUid(const Genome& g) {
  std::map<uint16_t, std::vector<uint8_t>> out;
  g.forEach([&](const GeneView& v) {
    std::vector<uint8_t> bytes{v.header.type, v.header.len, v.header.flags, uint8_t(v.header.stage),
                               v.header.featGate, v.header.mutWeight};
    bytes.insert(bytes.end(), v.body, v.body + v.header.len);
    out[v.header.uid.v] = bytes;
  });
  return out;
}

// Gene for gene, an adopted pet is the founder his board founds today; only
// the order differs (his recipe first, then the species' genes). Three
// residues of 0.4.0 ride along: the retired marble genes at uids 63 and 77,
// which adoption leaves because nothing in him can fire them any more (no def
// row names stimulus 19, locus 16 or the chase); uid 79's action byte,
// written when hop_circles was action 10 (it is 9 today); and 0.4.0's own
// recipe numbers (founding_040.h kRecipe040), which are his and stay.
void expectHisFounder(const Genome& adopted, SpeciesId s, uint32_t seed, const char* who) {
  const Genome founder = foundingGenome(s, seed, paint::bandsFor(s));
  std::map<uint16_t, std::vector<uint8_t>> adoptedGenes = genesByUid(adopted), founderGenes = genesByUid(founder);
  adoptedGenes.erase(63);
  adoptedGenes.erase(77);
  if (auto hop = adoptedGenes.find(79); hop != adoptedGenes.end() && hop->second.size() > 9)
    hop->second[9] = 9;   // header (6) + action (body 3): hop_circles at today's id
  for (const Byte040& b : kRecipe040) {
    std::vector<uint8_t>& mine = adoptedGenes[b.uid];
    ASSERT_GT(mine.size(), size_t(6 + b.at)) << who;
    EXPECT_EQ(mine[6 + b.at], b.value) << who << ": 0.4.0's own byte stays";
    mine[6 + b.at] = founderGenes[b.uid][6 + b.at];
  }
  EXPECT_EQ(adoptedGenes, founderGenes) << who;
}

}  // namespace

// ---- the fixture is the field ---------------------------------------------------------------------

TEST(Field, TheFrozen040FounderIsWhatEachBadgePrintedThisMorning) {
  for (const FieldBadge& b : kBatch) {
    const Genome g = foundingGenome040(seedOf(b.mac));
    EXPECT_EQ(g.hash(), b.genome040) << b.species;
    EXPECT_TRUE(predatesSpecies(g)) << b.species;
    EXPECT_STREQ(speciesInfo(tableOf(b)).name, b.species);
  }
}

// ---- each badge becomes its species and stays himself ---------------------------------------------

TEST(Adopt, EachBadgeBecomesItsTableSpeciesAndKeepsAgeChemistryAndBrain) {
  for (const FieldBadge& f : kBatch) {
    Badge b(f);
    b.liveOn040();
    b.boot();   // the release's first boot: the 0.4.0 save resumes
    ASSERT_EQ(b.dish->boot(), Boot::Resumed) << f.species;
    const Creature before = b.creature();
    ASSERT_EQ(b.dish->appearance().species, kGrungo);
    ASSERT_EQ(std::string(b.dish->settings().name), "Grungo");

    ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted) << f.species;
    const Creature& after = b.creature();
    const SpeciesId s = tableOf(f);
    EXPECT_EQ(after.phenotype().species.body, s) << f.species;
    EXPECT_EQ(after.phenotype().species.power, speciesInfo(s).power) << f.species;
    EXPECT_EQ(after.phenotype().oracle.voice, speciesInfo(s).starter.voice) << f.species;
    EXPECT_EQ(b.dish->appearance().species, s) << f.species;
    EXPECT_EQ(after.generation(), before.generation()) << f.species;
    EXPECT_EQ(after.stats().ageTicks, before.stats().ageTicks) << f.species;
    EXPECT_EQ(after.stage(), before.stage()) << f.species;
    EXPECT_EQ(after.chemistry().hash(), before.chemistry().hash()) << f.species << ": chemicals and loci";
    EXPECT_EQ(after.brain().hash(), before.brain().hash()) << f.species << ": what he has learned";
    EXPECT_EQ(after.body().at.x.raw, before.body().at.x.raw) << f.species;
    EXPECT_EQ(after.body().pose.v, before.body().pose.v) << f.species;
    EXPECT_EQ(after.scars().count, before.scars().count) << f.species;
    EXPECT_NE(after.genome().hash(), before.genome().hash()) << f.species;
    EXPECT_EQ(std::string(b.dish->lineage().currentName()), lineName(s)) << "the default name follows the species";
    EXPECT_EQ(std::string(b.dish->settings().name), lineName(s));

    expectHisFounder(after.genome(), s, b.seed(), f.species);
    EXPECT_EQ(b.dish->lineage().genomeOf(after.generation())->bytes(), after.genome().bytes())
        << "one Checkpoint, so the lineage rebuilds him as he is now";

    // Saved at once: the next boot resumes him adopted.
    const std::vector<uint8_t> adopted = after.genome().bytes();
    b.boot();
    ASSERT_EQ(b.dish->boot(), Boot::Resumed);
    EXPECT_EQ(b.creature().genome().bytes(), adopted) << f.species;
    EXPECT_EQ(b.creature().stats().ageTicks, before.stats().ageTicks) << f.species;
    EXPECT_EQ(std::string(b.dish->settings().name), lineName(s));
  }
}

TEST(Adopt, AnOwnersNameStays) {
  Badge b(kBatch[2]);
  b.liveOn040();
  b.boot();
  b.dish->rename("Pickle");
  ASSERT_TRUE(b.dish->flush());
  b.boot();
  ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
  EXPECT_EQ(std::string(b.dish->lineage().currentName()), "Pickle");
  EXPECT_EQ(std::string(b.dish->settings().name), "Pickle");
  EXPECT_EQ(b.dish->appearance().species, species::gilly);
}

TEST(Adopt, AnOwnersEditOutsideTheSpeciesBytesStays) {
  Badge b(kBatch[0]);
  b.liveOn040();
  b.boot();
  // The motto: a NoteGene the owner may edit, which no species owns.
  GeneUid note{};
  b.creature().genome().forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<NoteGene>::value) note = v.header.uid;
  });
  ASSERT_TRUE(b.dish->editGene(note, 0, 'W'));
  ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
  EXPECT_EQ(b.creature().genome().find(note)->body[0], 'W');
}

TEST(Adopt, RunsOnceAndASecondAskChangesNothing) {
  Badge b(kBatch[3]);
  b.liveOn040();
  b.boot();
  ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
  const std::vector<uint8_t> genome = b.creature().genome().bytes();
  const auto files = b.flash.files;
  EXPECT_EQ(b.adopt(), Dish::Adoption::NotNeeded);
  EXPECT_EQ(b.creature().genome().bytes(), genome);
  EXPECT_EQ(b.flash.files, files) << "nothing written";
  EXPECT_FALSE(adoptSpecies(b.creature(), species::fizz, b.seed(), paint::bandsFor(species::fizz)))
      << "a pet with a species is never adopted again, even as another";
}

TEST(Adopt, APetWithASpeciesIsLeftAloneEvenIfHisLineLacksALaterGene) {
  // A species line founded before war paint: everything a founder carries but that one gene.
  const uint32_t seed = seedOf(kBatch[0].mac);
  const Genome fizz = *GenomeBuilder::from(foundingGenome(species::fizz, seed, paint::bandsFor(species::fizz)))
                           .erase(GeneUid{0x0F42})
                           .build();
  MemStorage store;
  DishOptions o;
  o.founder = &fizz;
  Dish d(store, seed, kLineageId, o);
  NullLink link;
  uint32_t ms = 0;
  while (!std::holds_alternative<Creature>(d.occupant()) && ms < 3600000) d.tick(ms += 1000, link);
  Creature& c = std::get<Creature>(d.occupant());
  const std::vector<uint8_t> before = c.genome().bytes();
  EXPECT_FALSE(adoptSpecies(c, species::mossby, seed, paint::bandsFor(species::mossby)));
  EXPECT_EQ(c.genome().bytes(), before);
  EXPECT_EQ(d.adoptSpecies(species::mossby, paint::bandsFor(species::mossby)), Dish::Adoption::NotNeeded);
  EXPECT_EQ(d.appearance().species, species::fizz);
}

// Every place the power can go during the adoption's writes (the Checkpoint,
// the Rename, the save), torn or whole: the next boot converges on the same
// pet, the same name and a lineage that rebuilds him.
TEST(Adopt, APowerCutAnywhereConvergesOnTheSamePet) {
  Badge ref(kBatch[6]);
  ref.liveOn040();
  ref.boot();
  const auto saved040 = ref.flash.files;
  const size_t w0 = ref.flash.writes;
  ASSERT_EQ(ref.adopt(), Dish::Adoption::Adopted);
  const size_t writes = ref.flash.writes - w0;
  ASSERT_GE(writes, 3u) << "a Checkpoint, a Rename and a save";
  const std::vector<uint8_t> want = ref.creature().genome().bytes();
  const std::string wantName = ref.dish->settings().name;

  for (size_t cut = 0; cut <= writes; ++cut)
    for (std::optional<size_t> tear : {std::optional<size_t>{}, std::optional<size_t>{7}}) {
      if (tear && cut == 0) continue;
      Badge b(kBatch[6]);
      b.flash.files = saved040;
      b.boot();
      b.flash.writes = 0;
      b.flash.cutAfter = cut;
      b.flash.tearBytes = tear;
      b.adopt();
      // The power comes back.
      b.flash.cutAfter.reset();
      b.flash.tearBytes.reset();
      b.boot();
      ASSERT_TRUE(std::holds_alternative<Creature>(b.dish->occupant())) << "cut " << cut;
      const Dish::Adoption again = b.adopt();
      EXPECT_NE(again, Dish::Adoption::NotYet) << "cut " << cut;
      EXPECT_EQ(b.creature().genome().bytes(), want) << "cut " << cut << (tear ? " torn" : "");
      EXPECT_EQ(std::string(b.dish->settings().name), wantName) << "cut " << cut << (tear ? " torn" : "");
      EXPECT_EQ(std::string(b.dish->lineage().currentName()), wantName) << "cut " << cut;
      EXPECT_EQ(b.dish->lineage().genomeOf(b.creature().generation())->bytes(), want) << "cut " << cut;
      b.boot();
      EXPECT_EQ(b.creature().genome().bytes(), want) << "cut " << cut << ": saved";
    }
}

// ---- when it does not run --------------------------------------------------------------------------

TEST(Adopt, WaitsWhileTheStorageIsHeldAndTheTrialStillSeesThe040Pet) {
  Badge b(kBatch[1]);
  b.liveOn040();
  b.dish.reset();
  // What 0.4.0 wrote in its handover record just before the restart.
  const auto resumed040 = [&] {
    Badge peek(kBatch[1]);
    peek.flash.files = b.flash.files;
    peek.boot();
    return ota::PetMark{peek.creature().generation(), peek.creature().genome().hash()};
  }();

  ota::HeldStorage held(b.flash);
  held.hold(true);
  const auto files = b.flash.files;
  b.boot(held);
  ASSERT_EQ(b.dish->boot(), Boot::Resumed);
  b.run(5000);
  EXPECT_EQ(b.adopt(), Dish::Adoption::NotYet);
  EXPECT_EQ(b.dish->appearance().species, kGrungo);
  EXPECT_EQ(b.flash.files, files) << "a held board writes nothing";

  // The trial's verdict over the pet it sees: the one the record names.
  const ota::PetMark now{b.creature().generation(), b.creature().genome().hash()};
  EXPECT_EQ(now, resumed040);
  ota::TrialFacts facts{true, true, true, true, resumed040, now, 31000, true};
  EXPECT_EQ(ota::judge(facts), ota::Verdict::Keep);

  // The trial keeps the image: the hold ends, and he is adopted.
  held.hold(false);
  EXPECT_EQ(b.adopt(), Dish::Adoption::Adopted);
  EXPECT_EQ(b.dish->appearance().species, species::puffkin);
  b.boot();
  EXPECT_EQ(b.dish->appearance().species, species::puffkin) << "saved through the released store";
}

TEST(Adopt, AnEggWaitsAndTheCreatureItHatchesIsAdopted) {
  Badge b(kBatch[4]);
  b.boot();
  ASSERT_TRUE(std::holds_alternative<Egg>(b.dish->occupant()));
  EXPECT_EQ(b.adopt(), Dish::Adoption::NotYet);
  b.hatch();
  EXPECT_EQ(b.adopt(), Dish::Adoption::Adopted);
  EXPECT_EQ(b.dish->appearance().species, species::mudge);
}

TEST(Adopt, ASealedBoardIsNeverTouched) {
  Badge b(kBatch[5]);
  b.liveOn040();
  const auto files = b.flash.files;
  DishOptions o;
  o.sealed = true;
  Dish sealed(b.flash, b.seed(), kLineageId, o);
  EXPECT_EQ(sealed.adoptSpecies(species::fizz, paint::bandsFor(species::fizz)), Dish::Adoption::NotNeeded);
  EXPECT_EQ(b.flash.files, files);
}

// Grungo is adopted like any species. Board 237c is an ordinary table row
// (grungo): a pet from before species on it becomes grungo by the same rule,
// gains grungo's SpeciesGene, PowerGene, KnackGene, duel genes and rolled
// look, and keeps his age, chemistry and brain.
TEST(Adopt, Board237csPetAdoptsGrungoLikeAnyBadgeAdoptsItsSpecies) {
  ASSERT_EQ(tableSpecies(k237cMac), std::optional<SpeciesId>{kGrungo});
  ASSERT_EQ(badgeSpecies(k237cMac), kGrungo);
  const FieldBadge board{{0xd4, 0x05, 0x92, 0x77, 0x23, 0x7c}, "grungo", 0};
  Badge b(board);
  b.liveOn040();
  b.boot();
  ASSERT_EQ(b.dish->boot(), Boot::Resumed);
  const Creature before = b.creature();
  ASSERT_TRUE(predatesSpecies(before.genome()));
  EXPECT_EQ(advertisedSpecies(&before.genome(), k237cMac), kGrungo);

  ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
  const Creature& after = b.creature();
  EXPECT_FALSE(predatesSpecies(after.genome())) << "he carries grungo's SpeciesGene now";
  EXPECT_EQ(after.phenotype().species.body, kGrungo);
  EXPECT_EQ(after.phenotype().species.power, power::foresee);
  EXPECT_EQ(after.stats().ageTicks, before.stats().ageTicks);
  EXPECT_EQ(after.chemistry().hash(), before.chemistry().hash());
  EXPECT_EQ(after.brain().hash(), before.brain().hash());
  EXPECT_NE(after.genome().hash(), before.genome().hash());
  expectHisFounder(after.genome(), kGrungo, b.seed(), "237c");
  EXPECT_EQ(std::string(b.dish->settings().name), "Grungo");
  EXPECT_EQ(b.dish->lineage().genomeOf(after.generation())->bytes(), after.genome().bytes());
  EXPECT_EQ(advertisedSpecies(&after.genome(), k237cMac), kGrungo);
  EXPECT_EQ(b.adopt(), Dish::Adoption::NotNeeded) << "once";
}

// The same rule, asked straight: toward grungo exactly as toward any species.
TEST(Adopt, APetFromBeforeSpeciesAdoptsTowardGrungoAsTowardAnySpecies) {
  const uint32_t seed = seedOf(kBatch[0].mac);
  for (const SpeciesInfo& row : SPECIES) {
    Badge b(kBatch[0]);
    b.liveOn040(1);
    b.boot();
    ASSERT_EQ(b.dish->adoptSpecies(row.id, paint::bandsFor(row.id)), Dish::Adoption::Adopted) << row.name;
    EXPECT_EQ(b.creature().phenotype().species.body, row.id) << row.name;
    EXPECT_EQ(b.creature().phenotype().species.power, row.power) << row.name;
    expectHisFounder(b.creature().genome(), row.id, seed, row.name);
  }
}

// A board off the table adopts the species its MAC gives (founding.h badgeSpecies).
TEST(Adopt, ABoardOffTheTableAdoptsTheSpeciesItsMacGives) {
  const FieldBadge stranger{{0x02, 0x00, 0x00, 0x00, 0x00, 0x09}, "", 0};
  const SpeciesId s = badgeSpecies(stranger.mac);
  ASSERT_FALSE(tableSpecies(stranger.mac));
  ASSERT_TRUE(inRegularCast(s));
  Badge b(stranger);
  b.liveOn040(1);
  b.boot();
  ASSERT_EQ(b.dish->adoptSpecies(s, paint::bandsFor(s)), Dish::Adoption::Adopted);
  EXPECT_EQ(b.dish->appearance().species, s);
}

// ---- his eggs --------------------------------------------------------------------------------------

TEST(Adopt, EggsLaidByAnAdoptedPetHatchAsHisSpeciesForGenerations) {
  for (const FieldBadge& f : kBatch) {
    Badge b(f);
    b.liveOn040(1);
    b.boot();
    ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
    Clutch k = b.creature().layClutch(3, Fx::zero(), 0);
    for (uint8_t i = 0; i < k.count; ++i) {
      Genome g = k.child(i).genome;
      for (int gen = 0; gen < 20; ++gen) {
        Phenotype p;
        expressStage(g, Stage::Baby, 0, p);
        ASSERT_EQ(p.species.body, tableOf(f)) << f.species << " egg " << int(i) << " generation " << gen + 1;
        ASSERT_EQ(p.species.power, speciesInfo(tableOf(f)).power);
        Clutch next{};
        next.parent = g;
        next.generation = uint16_t(gen + 2);
        next.count = 1;
        next.seeds[0] = 0x5eed0000u + uint32_t(gen) * 7919u + i;
        g = next.child(0).genome;
      }
    }
  }
}

// ---- the Bluetooth name ----------------------------------------------------------------------------

TEST(Advertised, ABadgeIsItsSpeciesBeforeAndAfterTheAdoption) {
  for (const FieldBadge& f : kBatch) {
    const Genome grungo = foundingGenome040(seedOf(f.mac));
    EXPECT_STREQ(speciesInfo(advertisedSpecies(&grungo, f.mac)).name, f.species) << "the table will adopt him";
    const SpeciesId s = tableOf(f);
    const Genome founder = foundingGenome(s, seedOf(f.mac), paint::bandsFor(s));
    EXPECT_STREQ(speciesInfo(advertisedSpecies(&founder, f.mac)).name, f.species);
    EXPECT_STREQ(speciesInfo(advertisedSpecies(nullptr, f.mac)).name, f.species) << "sealed: the table";
  }
}

TEST(Advertised, APetIsHisOwnSpeciesWhateverTheTableSays) {
  const Genome gilly = foundingGenome(species::gilly, 1, paint::bandsFor(species::gilly));
  EXPECT_EQ(advertisedSpecies(&gilly, kBatch[0].mac), species::gilly) << "a pinned founding on a mossby badge";
  const uint8_t stranger[6] = {0x02, 0, 0, 0, 0, 9};
  EXPECT_EQ(advertisedSpecies(nullptr, stranger), badgeSpecies(stranger)) << "sealed: the species its MAC gives";
  const Genome old = foundingGenome040(seedOf(stranger));
  EXPECT_EQ(advertisedSpecies(&old, stranger), badgeSpecies(stranger)) << "a 0.4.0 pet off the table: the board will adopt him";
  const Genome grungo = foundingGenome(kGrungo, 1, paint::bandsFor(kGrungo));
  EXPECT_EQ(advertisedSpecies(&grungo, stranger), kGrungo) << "a pet with a species is his own";
}

TEST(Advertised, TheBootLinesSpeciesIsWhatThePetIsNow) {
  for (const FieldBadge& f : kBatch) {
    EXPECT_EQ(speciesOf(foundingGenome040(seedOf(f.mac))), kGrungo) << "a 0.4.0 pet until adopted";
    const SpeciesId s = tableOf(f);
    EXPECT_EQ(speciesOf(foundingGenome(s, seedOf(f.mac), paint::bandsFor(s))), s);
  }
}

TEST(Advertised, EverySpeciesNameFitsTheScanResponse) {
  // The scan response's 31 bytes hold the name as one AD structure: 2 bytes of header.
  for (const SpeciesInfo& row : SPECIES) {
    EXPECT_LE(std::strlen(row.name) + 2, 31u) << row.name;
    for (const char* c = row.name; *c; ++c) EXPECT_TRUE(*c >= 'a' && *c <= 'z') << row.name << ": a lowercase slug";
  }
}

// ---- the name it advertises: always the pet's (blorb/ble_name.h) ---------------------------------

std::string advertised(Dish& d, const uint8_t mac[6]) {
  char out[blename::kMax + 1];
  return advertisedName(d, mac, out);
}

// A fresh board's egg already has a name, his founder's species: what the
// badge advertised before, byte for byte.
TEST(AdvertisedName, AFreshEggIsNamedForHisSpecies) {
  for (const FieldBadge& f : kBatch) {
    MemStorage store;
    const SpeciesId s = tableOf(f);
    const Genome founder = foundingGenome(s, seedOf(f.mac), paint::bandsFor(s));
    DishOptions o;
    o.founder = &founder;
    Dish d(store, seedOf(f.mac), kLineageId, o);
    ASSERT_TRUE(std::holds_alternative<Egg>(d.occupant()));
    EXPECT_EQ(advertised(d, f.mac), f.species);
  }
}

// The bug: an image on an OTA trial advertised blorb-xxxx, and a trial that
// kept its image never restarted, so the badge kept that name until a reboot.
// On a held store (a trial) the pet's name is read like any other day.
TEST(AdvertisedName, OnAnOtaTrialItIsStillThePetsName) {
  Badge b(kBatch[1]);
  b.liveOn040();
  b.dish.reset();
  ota::HeldStorage held(b.flash);
  held.hold(true);
  b.boot(held);
  EXPECT_EQ(advertised(*b.dish, b.field.mac), "grungo") << "his name until the trial keeps the image and he is adopted";
  held.hold(false);
  ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
  EXPECT_EQ(advertised(*b.dish, b.field.mac), "puffkin") << "the adoption renames him, and the name follows";
}

TEST(AdvertisedName, AnOwnersNameIsAdvertisedAndKept) {
  Badge b(kBatch[4]);
  b.liveOn040();
  ASSERT_EQ(b.adopt(), Dish::Adoption::Adopted);
  b.dish->rename("Zorp_V");
  EXPECT_EQ(advertised(*b.dish, b.field.mac), "zorp_v");
  ASSERT_TRUE(b.dish->flush());
  b.boot();
  EXPECT_EQ(advertised(*b.dish, b.field.mac), "zorp_v") << "after a restart";
}

// The one board with no pet name it can read: a sealed one. It advertises
// the species its board gives, never blorb-xxxx.
TEST(AdvertisedName, ASealedBoardIsItsBoardsSpecies) {
  Badge b(kBatch[5]);
  b.liveOn040();
  DishOptions o;
  o.sealed = true;
  Dish sealed(b.flash, b.seed(), kLineageId, o);
  EXPECT_EQ(advertised(sealed, b.field.mac), "fizz");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
