#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "links.h"
#include "mem_storage.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

using Lines = std::vector<std::string>;

// The starter with a one-minute egg, so a creature is a minute away.
Genome quickEgg() {
  Genome g = starterGenome(7);
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<EggGene>::value) b.setByte(v.header.uid, 0, 1);
  });
  return *b.build();
}

struct Rig {
  MemStorage store;
  ScriptedLink link;
  Dish dish;
  uint32_t ms = 0, lastCall = 0;

  explicit Rig(const Genome& founder = quickEgg()) : dish(store, 7, 0x9f31c2d04a7bull, DishOptions{nullptr, &founder}) {}
  void run(uint32_t forMs) {
    runFor(dish, link, ms, forMs);
    lastCall = ms - kSampleMs;
  }
  void hatch() {
    for (int s = 0; !std::holds_alternative<Creature>(dish.occupant()); ++s) {
      if (s == 24 * 3600) return ADD_FAILURE() << "no hatch within a pet day";
      run(1000);
    }
  }
  Lines send(const std::string& line) {
    link.sent.clear();
    link.inbound.push_back(line);
    dish.tick(lastCall, link);   // no time passes: the line lands between ticks
    return link.sent;
  }
};

uint16_t geneUidOf(const Genome& g, uint8_t type, uint8_t firstByte) {
  uint16_t uid = 0;
  g.forEach([&](const GeneView& v) {
    if (v.header.type == type && v.body[0] == firstByte && !uid) uid = v.header.uid.v;
  });
  return uid;
}

std::vector<uint8_t> unbase64(const std::string& s) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  std::vector<uint8_t> out;
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    int v = value(c);
    if (v < 0) continue;
    acc = acc << 6 | uint32_t(v);
    bits += 6;
    if (bits >= 8) bits -= 8, out.push_back(uint8_t(acc >> bits));
  }
  return out;
}

// Reassembles "+ <offset> <b64>" lines, checking offsets, length and CRC from "OK <len> <crc>".
std::vector<uint8_t> unchunk(const Lines& lines, const std::string& id) {
  std::vector<uint8_t> blob;
  for (const std::string& l : lines) {
    if (l.rfind(id + " + ", 0) == 0) {
      size_t at = l.find(' ', id.size() + 3);
      EXPECT_EQ(std::stoul(l.substr(id.size() + 3, at - id.size() - 3)), blob.size());
      std::vector<uint8_t> part = unbase64(l.substr(at + 1));
      blob.insert(blob.end(), part.begin(), part.end());
    } else {
      unsigned len = 0, crc = 0;
      EXPECT_EQ(std::sscanf(l.c_str() + id.size(), " OK %u %x", &len, &crc), 2) << l;
      EXPECT_EQ(len, blob.size());
      EXPECT_EQ(crc, crc32(blob.data(), blob.size()));
    }
  }
  return blob;
}

bool isFinal(const std::string& l) { return l.find(" OK") != std::string::npos || l.find(" ERR ") != std::string::npos; }

constexpr uint64_t kLineageId = 0x9f31c2d04a7bull;

Lines ask(Dish& dish, ScriptedLink& link, const std::string& line) {
  link.sent.clear();
  link.inbound.push_back(line);
  dish.tick(0, link);
  return link.sent;
}

Creature deadOf(const Genome& g, uint16_t generation) {
  Creature c(Egg(Offspring{g, {}}, generation, 0), 0, 0);
  Habitat h;
  Behaviours b;
  c.inject(chem::life, Fx::ratio(5, 100));
  c.tick(SenseOut{}, h, b, 1);
  return c;
}

// Nine lives on the log: each dies a baby and its only egg is picked, and the
// last is renamed. Generation 8 carries a checkpoint.
struct Family {
  MemStorage store;
  std::vector<Genome> genomes;
  Family() {
    Genome g = starterGenome(7);
    genomes.push_back(g);
    Lineage l = Lineage::open(store, g, kLineageId, 0);
    for (uint16_t gen = 1; gen <= 9; ++gen) {
      Creature dead = deadOf(g, uint16_t(gen - 1));
      EXPECT_TRUE(dead.dead());
      l.recordDeath(dead, "Grungo", gen * 100u);
      Clutch k;
      k.parent = g;
      k.generation = gen;
      k.seeds[0] = 1000 + gen;
      Egg egg(k.child(0), gen, gen * 100u + 1);
      l.recordBirth(egg, k, 0, gen * 100u + 1);
      g = egg.genome();
      genomes.push_back(g);
    }
    l.rename("Blorbo");
  }
};

// The rig grown to an elder and dead, so the clutch holds three eggs.
void elderDies(Rig& r) {
  const Fx life[] = {Fx::ratio(85, 100), Fx::ratio(60, 100), Fx::ratio(15, 100), Fx::ratio(5, 100)};
  for (Fx l : life) {
    std::get<Creature>(r.dish.occupant()).inject(chem::life, l);
    r.run(500);
  }
}

}  // namespace

TEST(Framing, FuzzedLinesNeverCrashAndAlwaysEndInOneFinalLine) {
  Rig r;
  r.hatch();
  Rng rng = Rng::seeded(99);
  const std::string seeds[] = {"#1 HELLO", "#2 TWIST stimulus shake", "#3 GENE 12", "#4 SUB state", "#5 TIME 1790000000",
                               "#6 TWIST prophecy held rest fear -300", "#7 BRAIN 3", "#8 DIFF 1", "#a TWIST gene_edit 9 1 7"};
  for (int i = 0; i < 3000; ++i) {
    std::string line;
    if (i % 3 == 0) {
      for (uint32_t n = rng.below(220); n > 0; --n) line.push_back(char(rng.below(256)));
    } else {
      line = seeds[rng.below(std::size(seeds))];
      for (uint32_t n = rng.below(4); n > 0; --n) {
        uint32_t at = rng.below(uint32_t(line.size()) + 1);
        if (rng.below(2)) line.insert(line.begin() + at, char(rng.below(128)));
        else if (at < line.size()) line.erase(line.begin() + at);
      }
    }
    Lines out = r.send(line);
    int finals = 0;
    for (const std::string& l : out) {
      EXPECT_LE(l.size(), 200u);
      EXPECT_TRUE(l[0] == '#' || l[0] == '!') << l;
      finals += l[0] == '#' && isFinal(l);
    }
    EXPECT_EQ(finals, 1) << "line " << i;
    if (i % 100 == 0) r.run(500);
  }
}

TEST(Framing, A201ByteLineIsErr413AndA200ByteLineIsNot) {
  Rig r;
  std::string body = "#1a HELLO ";
  EXPECT_EQ(r.send(body + std::string(201 - body.size(), 'x')), (Lines{"#1a ERR 413 TOO_LONG"}));
  Lines ok = r.send(body + std::string(200 - body.size(), 'x'));
  ASSERT_EQ(ok.size(), 1u);
  EXPECT_EQ(ok[0].rfind("#1a OK fw=", 0), 0u) << "HELLO ignores trailing words";
  EXPECT_EQ(r.send(std::string(300, 'y')), (Lines{"#0 ERR 413 TOO_LONG"}));
  EXPECT_EQ(r.send("#1234567 HELLO"), (Lines{"#0 ERR 400 BAD_ID"}));
  EXPECT_EQ(r.send("#zz HELLO"), (Lines{"#0 ERR 400 BAD_ID"}));
  EXPECT_EQ(r.send("#5 FEED"), (Lines{"#5 ERR 404 UNKNOWN_VERB"}));
}

TEST(Snapshot, TheBlobDecodesToTheLiveState) {
  Rig r;
  r.hatch();
  r.run(20000);
  Lines out = r.send("#9 SNAPSHOT");
  std::vector<uint8_t> blob = unchunk(out, "#9");
  Snapshot s;
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), s));
  EXPECT_EQ(s.hash(), r.dish.hash());
  EXPECT_EQ(Keepsake::encode(s), blob);
}

TEST(Twist, AStimulusTwistChangesTheLiveStateWhileTicksRun) {
  Rig a, b;
  a.hatch();
  b.hatch();
  ASSERT_EQ(a.dish.hash(), b.dish.hash());
  EXPECT_EQ(a.send("#1 TWIST stimulus shake"), (Lines{"#1 OK stimulus"}));
  a.run(3000);
  b.run(3000);
  EXPECT_NE(a.dish.hash(), b.dish.hash());
  EXPECT_EQ(std::get<Creature>(a.dish.occupant()).stats().shaken, 1u);
  EXPECT_EQ(std::get<Creature>(b.dish.occupant()).stats().shaken, 0u);
}

// Ask 15: basic food is body-only. Neither the pellet button nor a meal's own
// stimulus can come from the phone, by name or by number.
TEST(Twist, ThePhoneCannotDeliverPlainFood) {
  Rig a, b;
  a.hatch();
  b.hatch();
  for (const char* stim : {"button", "fed", "8", "17"})
    EXPECT_EQ(a.send(std::string("#1 TWIST stimulus ") + stim), (Lines{"#1 ERR 403 BODY_ONLY"})) << stim;
  a.run(3000);
  b.run(3000);
  EXPECT_EQ(a.dish.hash(), b.dish.hash()) << "a refused feed changed nothing";
}

// A special treat is what body feeding cannot give: this one is the phone's
// foresight, so he glows as if foreseeing, and it is no meal.
TEST(Twist, AProphecyTreatMakesHimGlowAndDoesNotFeedHim) {
  Rig a, b;
  a.hatch();
  b.hatch();
  EXPECT_EQ(a.send("#1 TWIST prophecy_treat"), (Lines{"#1 OK prophecy_treat"}));
  a.run(1000);
  b.run(1000);
  const Creature& fed = std::get<Creature>(a.dish.occupant());
  const Creature& plain = std::get<Creature>(b.dish.occupant());
  EXPECT_GT(a.dish.appearance().glow, b.dish.appearance().glow);
  EXPECT_EQ(fed.chemistry().chem[chem::food.v], plain.chemistry().chem[chem::food.v]) << "no food chemical";
  EXPECT_GE(fed.chemistry().drive(drive::hunger), plain.chemistry().drive(drive::hunger)) << "no satiety";
  EXPECT_EQ(fed.stats().fed, plain.stats().fed);
  Rig egg;
  EXPECT_EQ(egg.send("#2 TWIST prophecy_treat"), (Lines{"#2 ERR 409 NOT_NOW"}));
  EXPECT_EQ(egg.send("#3 TWIST prophecy_treat 5"), (Lines{"#3 ERR 400 BAD_ARGS"}));
}

// Prework 3: an owner edit is a Checkpoint, so the lineage rebuilds the edited genome.
TEST(Twist, AGeneEditChangesTheGenomeAndTheLineageRecordsIt) {
  Rig r;
  r.hatch();
  const Creature& c = std::get<Creature>(r.dish.occupant());
  uint16_t cloak = geneUidOf(c.genome(), GeneKindOf<PaletteGene>::value, region::cloak.v);
  ASSERT_NE(cloak, 0);
  std::string op = "#2 TWIST gene_edit " + std::to_string(cloak) + " 1 200";
  EXPECT_EQ(r.send(op), (Lines{"#2 OK gene_edit"}));
  EXPECT_EQ(c.genome().find(GeneUid{cloak})->body[1], 200);
  r.run(2000);
  std::optional<Genome> recorded = r.dish.lineage().genomeOf(c.generation());
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(recorded->bytes(), c.genome().bytes());
  bool tinted = false;
  for (const Phenotype::Paint& p : c.phenotype().palette) tinted = tinted || (p.region == region::cloak && p.tint.hue == int8_t(200 - 128));
  EXPECT_TRUE(tinted) << "the phenotype was re-expressed from the edit";
}

TEST(Twist, RenameProphecyAndPickApply) {
  Rig r;
  r.hatch();
  EXPECT_EQ(r.send("#3 TWIST rename Grungo_V"), (Lines{"#3 OK rename"}));
  EXPECT_STREQ(r.dish.settings().name, "Grungo_V");
  EXPECT_STREQ(r.dish.lineage().currentName(), "Grungo_V");
  EXPECT_EQ(r.send("#4 TWIST prophecy recent_shake foresee fear -500"), (Lines{"#4 OK prophecy"}));
  EXPECT_EQ(std::get<Creature>(r.dish.occupant()).brain().predict(locus::recent(stim::shake), action::foresee, drive::fear),
            fromQ15(toQ15(-Fx::ratio(1, 2))));
  EXPECT_EQ(r.send("#5 TWIST pick 0"), (Lines{"#5 ERR 409 NOT_NOW"}));
  std::get<Creature>(r.dish.occupant()).inject(chem::life, Fx::ratio(5, 100));
  r.run(1000);
  ASSERT_TRUE(std::holds_alternative<Clutch>(r.dish.occupant()));
  EXPECT_EQ(r.send("#6 TWIST pick 7"), (Lines{"#6 ERR 416 OUT_OF_RANGE"}));
  EXPECT_EQ(r.send("#7 TWIST pick 0"), (Lines{"#7 OK pick"}));
  EXPECT_TRUE(std::holds_alternative<Egg>(r.dish.occupant()));
  EXPECT_EQ(r.dish.lineage().currentGeneration(), 1);
}

TEST(Twist, MalformedOpsAreRefusedAndTheStateIsUntouched) {
  Rig a, b;
  a.hatch();
  b.hatch();
  const Creature& c = std::get<Creature>(a.dish.occupant());
  std::string palette = std::to_string(geneUidOf(c.genome(), GeneKindOf<PaletteGene>::value, region::cloak.v));
  const std::pair<std::string, std::string> bad[] = {
      {"TWIST", "400 NO_TWIST"},
      {"TWIST weather rain", "404 UNKNOWN_TWIST"},
      {"TWIST stimulus", "400 BAD_ARGS"},
      {"TWIST stimulus shake knock", "400 BAD_ARGS"},
      {"TWIST stimulus 63", "416 OUT_OF_RANGE"},
      {"TWIST stimulus tickle", "416 OUT_OF_RANGE"},
      {"TWIST gene_edit 9 1", "400 BAD_ARGS"},
      {"TWIST gene_edit 9 1 256", "400 BAD_ARGS"},
      {"TWIST gene_edit 70000 1 1", "400 BAD_ARGS"},
      {"TWIST gene_edit -9 1 1", "400 BAD_ARGS"},
      {"TWIST gene_edit 60000 1 1", "416 OUT_OF_RANGE"},
      {"TWIST gene_edit " + palette + " 6 1", "416 OUT_OF_RANGE"},
      {"TWIST rename", "400 BAD_ARGS"},
      {"TWIST rename Grungo_the_sixteenth", "400 BAD_ARGS"},
      {"TWIST rename bad!name", "400 BAD_ARGS"},
      {"TWIST pick", "400 BAD_ARGS"},
      {"TWIST pick 0", "409 NOT_NOW"},
      {"TWIST prophecy held rest fear", "400 BAD_ARGS"},
      {"TWIST prophecy held rest fear 1001", "400 BAD_ARGS"},
      {"TWIST prophecy tilt_x rest fear 10", "416 OUT_OF_RANGE"},
      {"TWIST prophecy held fly fear 10", "416 OUT_OF_RANGE"},
      {"TWIST prophecy held rest joy 10", "416 OUT_OF_RANGE"},
  };
  uint32_t before = a.dish.hash();
  for (const auto& [op, err] : bad) {
    EXPECT_EQ(a.send("#e " + op), (Lines{"#e ERR " + err})) << op;
    EXPECT_EQ(a.dish.hash(), before) << op;
  }
  a.run(5000);
  b.run(5000);
  EXPECT_EQ(a.dish.hash(), b.dish.hash()) << "nothing a refused op touched shows up later either";
}

TEST(Link, AConnectedLinkThatSendsNothingReceivesNothing) {
  Rig r;
  r.run(120000);
  r.hatch();
  r.run(60000);
  EXPECT_TRUE(r.link.sent.empty());
}

TEST(Events, SubPushesStateEveryTwoSecondsAndDisconnectStops) {
  Rig r;
  r.hatch();
  EXPECT_EQ(r.send("#1 SUB state died").front(), "#1 OK events=0x05");
  r.link.sent.clear();
  r.run(10000);
  int states = 0;
  for (const std::string& l : r.link.sent) states += l.rfind("! STATE phase=creature", 0) == 0;
  EXPECT_GE(states, 4);
  EXPECT_LE(states, 6);
  std::get<Creature>(r.dish.occupant()).inject(chem::life, Fx::ratio(5, 100));
  r.link.sent.clear();
  r.run(1000);
  bool died = false;
  for (const std::string& l : r.link.sent) died = died || l == "! DIED gen=0 cause=old_age";
  EXPECT_TRUE(died);
  NullLink gone;
  r.dish.tick(r.ms, gone);
  r.link.sent.clear();
  r.run(10000);
  EXPECT_TRUE(r.link.sent.empty()) << "a new connection starts unsubscribed";
}

TEST(Schema, ListsEveryRegistryRowIncludingTwistKinds) {
  Rig r;
  Lines out = r.send("#5 SCHEMA");
  ASSERT_FALSE(out.empty());
  size_t senses = 0;
#define BLORB_SENSE(member, Type) ++senses;
#include "blorb/defs/senses.def"
#undef BLORB_SENSE
  size_t want = std::size(CHEMICALS) + std::size(DRIVES) + std::size(LOCI) + std::size(STIMULI) + std::size(ACTIONS) +
                std::size(POSES) + std::size(EXPRESSIONS) + std::size(REGIONS) + std::size(REFLEXES) +
                std::size(CARES) + std::size(FEATS) + std::size(GENE_TYPES) + senses + std::size(COMMANDS) +
                std::size(TWISTS);
  EXPECT_EQ(out.size(), want + 1);
  EXPECT_EQ(out.back(), "#5 OK " + std::to_string(want));
  for (const TwistInfo& t : TWISTS) {
    bool listed = false;
    for (const std::string& l : out) listed = listed || l.rfind(std::string("#5 + twist ") + t.name + " ", 0) == 0;
    EXPECT_TRUE(listed) << t.name;
  }
  for (const char* row : {"#5 + stim 2 shake body 1", "#5 + gene 32 palette 6 look XNNNAN", "#5 + cmd TWIST 1",
                          "#5 + locus 133 startle act 0", "#5 + sense tilt"}) {
    bool listed = false;
    for (const std::string& l : out) listed = listed || l == row;
    EXPECT_TRUE(listed) << row;
  }
}

// ---- golden transcripts: what the website parses ------------------------------------

TEST(Golden, Hello) {
  Rig r;
  EXPECT_EQ(r.send("#1a HELLO"), (Lines{"#1a OK fw=0.3.0 fmt=1 lineage=9f31c2d04a7b gen=0 name=Grungo phase=egg feats=0x00"}));
}

TEST(Golden, State) {
  Rig r;
  r.hatch();
  r.run(30000);
  Lines got = r.send("#2b STATE");
  Lines want = {
      "#2b + phase=creature gen=0 stage=baby age=309 action=chase face=neutral asleep=0",
      "#2b + drives hunger=301 sleepiness=0 boredom=2 loneliness=1 fear=8 pain=1 discomfort=3 need_touch=1",
      "#2b + body life=1000 injury=0 glow=0 dreaming=0",
      "#2b + dish pantry=4 pellets=0 night=1 time=unknown",
      "#2b OK",
  };
  EXPECT_EQ(got, want);
}

TEST(Golden, Diff) {
  MemStorage store;
  Genome starter = starterGenome(7);
  {
    Lineage l = Lineage::open(store, starter, 0x9f31c2d04a7bull, 0);
    Clutch k;
    k.parent = starter;
    k.generation = 1;
    k.seeds[0] = 42;
    Egg egg(k.child(0), 1, 100);
    l.recordBirth(egg, k, 0, 100);
  }
  ScriptedLink link;
  Dish dish(store, 7, 0x9f31c2d04a7bull);
  EXPECT_EQ(dish.boot(), Boot::FromLineage);
  link.inbound.push_back("#3c DIFF 1");
  dish.tick(0, link);
  Lines want = {
      "#3c + point=38",  "#3c + kind=receptor",   "#3c + byte=4", "#3c + from=128", "#3c + to=132",
      "#3c + point=56",  "#3c + kind=stimulus",   "#3c + byte=6", "#3c + from=115", "#3c + to=123",
      "#3c + point=90",  "#3c + kind=expression", "#3c + byte=1", "#3c + from=0",   "#3c + to=6",
      "#3c + point=91",  "#3c + kind=palette",    "#3c + byte=1", "#3c + from=139", "#3c + to=168",
      "#3c OK 4",
  };
  EXPECT_EQ(link.sent, want);
}

TEST(Golden, Twist) {
  Rig r;
  r.hatch();
  Lines got;
  for (const char* op : {"#4d TWIST stimulus petted", "#4e TWIST rename Grungo_V", "#4f TWIST gene_edit 1 2 250",
                         "#50 TWIST pick 0", "#51 TWIST stimulus 99", "#52 TWIST prophecy held rest need_touch -400"}) {
    Lines out = r.send(op);
    got.insert(got.end(), out.begin(), out.end());
  }
  Lines want = {
      "#4d OK stimulus",     "#4e OK rename",            "#4f OK gene_edit",
      "#50 ERR 409 NOT_NOW", "#51 ERR 416 OUT_OF_RANGE", "#52 OK prophecy",
  };
  EXPECT_EQ(got, want);
}

// One op of every kind but a point, so the phone's text for each is pinned.
TEST(Golden, DiffOfEveryOpKind) {
  const Genome starter = starterGenome(7);
  uint16_t cloak = 0, bluerCloak = 0, eye = 0, stalks = 0, lastInstinct = 0;
  uint8_t weight = 0;
  starter.forEach([&](const GeneView& v) {
    const bool palette = v.header.type == GeneKindOf<PaletteGene>::value;
    if (palette && v.body[0] == region::cloak.v) (v.header.flags & GeneFlags::Dormant ? bluerCloak : cloak) = v.header.uid.v;
    if (palette && v.body[0] == region::eye.v) eye = v.header.uid.v;
    if (v.header.type == GeneKindOf<MarkGene>::value && v.body[0] == 1) stalks = v.header.uid.v;
    if (v.header.type == GeneKindOf<InstinctGene>::value) lastInstinct = v.header.uid.v, weight = v.header.mutWeight;
  });
  const GeneUid copy = starter.nextUid(), learned{uint16_t(copy.v + 1)};
  const InstinctGene instinct{{locus::cradled.v, 255, 255}, action::curl.v, drive::fear.v, 70, 200};
  std::vector<uint8_t> gene = {GeneKindOf<InstinctGene>::value, uint8_t(sizeof instinct),
                               uint8_t(GeneFlags::Mutable | GeneFlags::Heirloom), uint8_t(Stage::Baby), 0, weight,
                               uint8_t(learned.v), uint8_t(learned.v >> 8)};
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&instinct);
  gene.insert(gene.end(), raw, raw + sizeof instinct);
  const MutationDiff diff{{MutDup{GeneUid{cloak}, copy}, MutDel{GeneUid{stalks}}, MutWake{GeneUid{bluerCloak}},
                           MutSleep{GeneUid{eye}}, MutHeirloom{GeneUid{lastInstinct}, gene}}};
  std::optional<Genome> child = apply(starter, diff);
  ASSERT_TRUE(child.has_value());
  MemStorage store;
  {
    Lineage l = Lineage::open(store, starter, kLineageId, 0);
    Clutch k;
    k.parent = starter;
    k.generation = 1;
    l.recordBirth(Egg(Offspring{*child, diff}, 1, 100), k, 0, 100);
  }
  ScriptedLink link;
  Dish dish(store, 7, kLineageId);
  ASSERT_EQ(dish.boot(), Boot::FromLineage);
  Lines want = {
      "#5f + dup=93",        "#5f + kind=palette", "#5f + del=99",        "#5f + kind=mark",
      "#5f + wake=96",       "#5f + kind=palette", "#5f + sleep=94",      "#5f + kind=palette",
      "#5f + heirloom=80",   "#5f + kind=instinct", "#5f + cue0=cradled", "#5f + cue1=255",
      "#5f + cue2=255",      "#5f + action=curl",  "#5f + drive=fear",    "#5f + level=70",
      "#5f + strength=200",  "#5f + learned=0",    "#5f OK 5",
  };
  EXPECT_EQ(ask(dish, link, "#5f DIFF 1"), want);
}

TEST(Golden, Genome) {
  Rig r;
  r.hatch();
  Lines out = r.send("#60 GENOME");
  EXPECT_EQ(unchunk(out, "#60"), std::get<Creature>(r.dish.occupant()).genome().bytes());
  EXPECT_EQ(out.size(), 14u);
  EXPECT_EQ(out.back(), "#60 OK 1528 c2e84bf2") << "length and CRC of the quick-egg starter";
}

TEST(Golden, Chem) {
  Rig egg;
  EXPECT_EQ(egg.send("#61 CHEM"), (Lines{"#61 ERR 409 NO_CREATURE"}));
  Rig r;
  r.hatch();
  r.run(30000);
  Lines want = {
      "#62 + chem=1 name=hunger level=301",      "#62 + chem=2 name=sleepiness level=0",
      "#62 + chem=3 name=boredom level=2",       "#62 + chem=4 name=loneliness level=1",
      "#62 + chem=5 name=fear level=8",          "#62 + chem=6 name=pain level=1",
      "#62 + chem=7 name=discomfort level=3",    "#62 + chem=8 name=need_touch level=1",
      "#62 + chem=16 name=life level=1000",      "#62 + chem=18 name=energy level=800",
      "#62 + chem=23 name=melatonin level=3",    "#62 + chem=24 name=vision level=29",
      "#62 + locus=0 name=always level=1000",    "#62 + locus=1 name=tilt_x level=500",
      "#62 + locus=2 name=tilt_y level=500",     "#62 + locus=7 name=day_sin level=503",
      "#62 + locus=8 name=day_cos level=1000",   "#62 + locus=9 name=owner_near level=1000",
      "#62 + locus=16 name=marble_near level=413", "#62 + locus=18 name=pantry level=1000",
      "#62 OK 20",
  };
  EXPECT_EQ(r.send("#62 CHEM"), want);
}

TEST(Golden, LineageOverNineGenerations) {
  Family f;
  ScriptedLink link;
  Dish dish(f.store, 7, kLineageId);
  ASSERT_EQ(dish.boot(), Boot::FromLineage);
  Lines want = {
      "#63 + founding id=9f31c2d04a7b species=Grungo",
      "#63 + death gen=0 cause=old_age age=0 feats=0x00 name=Grungo",
      "#63 + birth gen=1 chosen=0 of=1 ops=6",
      "#63 + death gen=1 cause=old_age age=0 feats=0x00 name=Grungo",
      "#63 + birth gen=2 chosen=0 of=1 ops=5",
      "#63 + death gen=2 cause=old_age age=0 feats=0x00 name=Grungo",
      "#63 + birth gen=3 chosen=0 of=1 ops=7",
      "#63 + death gen=3 cause=old_age age=0 feats=0x00 name=Grungo",
      "#63 + birth gen=4 chosen=0 of=1 ops=8",
      "#63 + death gen=4 cause=old_age age=0 feats=0x00 name=Grungo",
      "#63 + birth gen=5 chosen=0 of=1 ops=7",
      "#63 + death gen=5 cause=old_age age=0 feats=0x20 name=Grungo",
      "#63 + birth gen=6 chosen=0 of=1 ops=8",
      "#63 + death gen=6 cause=old_age age=0 feats=0x20 name=Grungo",
      "#63 + birth gen=7 chosen=0 of=1 ops=7",
      "#63 + death gen=7 cause=old_age age=0 feats=0x20 name=Grungo",
      "#63 + birth gen=8 chosen=0 of=1 ops=9",
      "#63 + checkpoint gen=8",
      "#63 + death gen=8 cause=old_age age=0 feats=0x20 name=Grungo",
      "#63 + birth gen=9 chosen=0 of=1 ops=4",
      "#63 + rename gen=9 name=Blorbo",
      "#63 OK 21",
  };
  EXPECT_EQ(ask(dish, link, "#63 LINEAGE"), want);
}

TEST(Golden, AncestorRebuildsEveryGenerationOfTheLog) {
  Family f;
  ScriptedLink link;
  Dish dish(f.store, 7, kLineageId);
  ASSERT_EQ(dish.boot(), Boot::FromLineage);
  for (uint16_t gen = 0; gen <= 9; ++gen)
    EXPECT_EQ(unchunk(ask(dish, link, "#64 ANCESTOR " + std::to_string(gen)), "#64"), f.genomes[gen].bytes())
        << "generation " << gen;
  EXPECT_NE(f.genomes[9].bytes(), f.genomes[0].bytes());
  EXPECT_EQ(ask(dish, link, "#65 ANCESTOR 10"), (Lines{"#65 ERR 404 NO_GENOME"}));
  EXPECT_EQ(ask(dish, link, "#66 ANCESTOR"), (Lines{"#66 ERR 400 BAD_ARGS"}));
}

TEST(Golden, Portrait) {
  Family f;
  ScriptedLink link;
  Dish dish(f.store, 7, kLineageId);
  ASSERT_EQ(dish.boot(), Boot::FromLineage);
  Lines got = ask(dish, link, "#67 PORTRAIT 0 0");
  Lines nine = ask(dish, link, "#68 PORTRAIT 9");
  got.insert(got.end(), nine.begin(), nine.end());
  Lines want = {
      "#67 + stage=baby scale=55 seed=d414320c marks=1",
      "#67 + region=skin hue=11 sat=128 val=128",
      "#67 + region=belly hue=0 sat=128 val=137",
      "#67 + region=cloak hue=0 sat=128 val=118",
      "#67 + region=eye hue=0 sat=128 val=128",
      "#67 + region=mouth hue=0 sat=128 val=128",
      "#67 + region=glow hue=0 sat=140 val=160",
      "#67 + region=shell hue=11 sat=128 val=128",
      "#67 + mark layer=0 variant=4 hue=0",
      "#67 OK",
      "#68 + stage=adult scale=103 seed=e0415e2f marks=1",
      "#68 + region=skin hue=13 sat=128 val=158",
      "#68 + region=belly hue=0 sat=128 val=137",
      "#68 + region=cloak hue=-90 sat=110 val=120",
      "#68 + region=eye hue=0 sat=128 val=128",
      "#68 + region=mouth hue=0 sat=128 val=128",
      "#68 + region=glow hue=0 sat=140 val=160",
      "#68 + region=shell hue=13 sat=128 val=158",
      "#68 + mark layer=0 variant=4 hue=0",
      "#68 OK",
  };
  EXPECT_EQ(got, want);
  EXPECT_EQ(ask(dish, link, "#69 PORTRAIT 10"), (Lines{"#69 ERR 404 NO_GENOME"}));
  EXPECT_EQ(ask(dish, link, "#6a PORTRAIT 9 4"), (Lines{"#6a ERR 400 BAD_ARGS"}));
}

TEST(Golden, Clutch) {
  Rig r;
  r.hatch();
  EXPECT_EQ(r.send("#6b CLUTCH"), (Lines{"#6b ERR 409 NO_CLUTCH"}));
  const std::vector<uint8_t> parent = std::get<Creature>(r.dish.occupant()).genome().bytes();
  elderDies(r);
  ASSERT_TRUE(std::holds_alternative<Clutch>(r.dish.occupant()));
  r.run(Clutch::kVigilTicks * kTickMs);
  EXPECT_EQ(r.send("#6c TWIST stimulus knock"), (Lines{"#6c OK stimulus"}));
  r.run(200);
  Lines want = {
      "#6d + egg=0 skin=11,128,98 cloak=-90,110,120 shell=11,128,98 look=4 mind=5",
      "#6d + egg=1 skin=-15,128,128 cloak=0,128,118 shell=-15,128,128 look=4 mind=8",
      "#6d + egg=2 skin=11,128,160 cloak=0,128,118 shell=11,128,160 look=2 mind=4",
      "#6d OK eggs=3 cursor=1 previewed=3",
  };
  EXPECT_EQ(r.send("#6d CLUTCH"), want);
  EXPECT_EQ(unchunk(r.send("#6e GENOME"), "#6e"), parent) << "a clutch answers with its parent's genome";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
