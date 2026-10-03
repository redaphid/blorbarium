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
    while (!std::holds_alternative<Creature>(dish.occupant())) run(1000);
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
  std::optional<Snapshot> s = Keepsake::decode(blob.data(), blob.size());
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->hash(), r.dish.hash());
  EXPECT_EQ(Keepsake::encode(*s), blob);
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
      "#2b + phase=creature gen=0 stage=baby age=309 action=eat face=neutral asleep=0",
      "#2b + drives hunger=301 sleepiness=0 boredom=3 loneliness=1 fear=8 pain=1 discomfort=3 need_touch=1",
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
      "#3c + point=93",  "#3c + kind=palette",    "#3c + byte=5", "#3c + from=0",   "#3c + to=3",
      "#3c + point=100", "#3c + kind=size",       "#3c + byte=0", "#3c + from=128", "#3c + to=104",
      "#3c OK 5",
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
