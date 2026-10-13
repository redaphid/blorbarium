#include "blorb/friend_egg.h"

#include <cstring>
#include <vector>
#include "blorb/seams.h"

namespace blorb {
namespace {

// The source parent switches about once in this many genes.
constexpr uint32_t kBlockGenes = 8;

// friend.egg: magic "BFEG", version u8, friendLineage u64, friendSpecies u8,
// battleSeed u32, friendGenomeHash u32, genome length u16, the genome, then a
// crc32 of everything before it. Little-endian, like the keepsake.
constexpr char kMagic[4] = {'B', 'F', 'E', 'G'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeadLen = 4 + 1 + 8 + 1 + 4 + 4 + 2;
constexpr size_t kCrcLen = 4;

void put(std::vector<uint8_t>& b, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
uint64_t get(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= uint64_t(p[i]) << (8 * i);
  return v;
}

}  // namespace

std::optional<Genome> acceptFriendGenome(const uint8_t* bytes, size_t len, uint32_t announcedHash,
                                         const Genome& mine) {
  std::optional<Genome> g = Genome::parse(bytes, len);
  if (!g || g->hash() != announcedHash) return std::nullopt;
  bool shares = false;
  mine.forEach([&](const GeneView& v) { shares = shares || g->find(v.header.uid).has_value(); });
  if (!shares) return std::nullopt;
  return g;
}

// One draw per gene of mine whatever it is, so the blocks fall the same way
// for every friend: the cross is a function of my genome, theirs and the seed.
MutationDiff crossDiff(const Genome& mine, const Genome& friends, Rng& rng) {
  MutationDiff d;
  bool fromFriend = (rng.next() & 1) != 0;
  mine.forEach([&](const GeneView& g) {
    if (rng.below(kBlockGenes) == 0) fromFriend = !fromFriend;
    if (!fromFriend || !crossable(g.header.type)) return;
    std::optional<GeneView> f = friends.find(g.header.uid);
    if (!f || f->header.type != g.header.type || f->header.len != g.header.len) return;
    for (uint8_t i = 0; i < g.header.len; ++i)
      if (g.body[i] != f->body[i]) d.ops.push_back(MutPoint{g.header.uid, i, g.body[i], f->body[i], false});
  });
  return d;
}

// Twice is no change: a second call only moves the seed.
void joinClutch(Clutch& k, uint32_t crossSeed) {
  if (k.friendSlot == 0 && k.count < kMaxClutch) ++k.count;
  k.friendSlot = k.count;
  k.friendSeed = crossSeed;
}

Offspring childOf(const Clutch& k, uint8_t i, const FriendEgg* f) {
  if (!f || k.friendSlot == 0 || i != k.friendSlot - 1 || f->record.battleSeed != k.friendSeed) return k.child(i);
  Rng crossRng = Rng::seeded(k.friendSeed);
  MutationDiff cross = crossDiff(k.parent, f->genome, crossRng);
  std::optional<Genome> crossed = apply(k.parent, cross);
  if (!crossed || !viability(*crossed).ok) return k.child(i);
  Rng rng = Rng::seeded(k.seeds[i]);
  MutationPolicy pol = policyOf(k.parent, k.wildBonus);
  pol.lookSlot = i;
  Offspring child = mutate(*crossed, pol, k.heirlooms, rng);
  if (!viability(child.genome).ok) return k.child(i);
  cross.ops.insert(cross.ops.end(), child.diff.ops.begin(), child.diff.ops.end());
  return Offspring{std::move(child.genome), std::move(cross)};
}

std::optional<FriendEgg> FriendEggStore::load(Storage& s) {
  std::vector<uint8_t> b(s.size(kFile));
  if (b.size() < kHeadLen + kCrcLen) return std::nullopt;
  std::optional<size_t> n = s.read(kFile, 0, b.data(), b.size());
  if (!n || *n != b.size() || std::memcmp(b.data(), kMagic, sizeof kMagic) != 0 || b[4] != kVersion)
    return std::nullopt;
  const size_t genomeLen = size_t(get(&b[kHeadLen - 2], 2));
  if (b.size() != kHeadLen + genomeLen + kCrcLen) return std::nullopt;
  if (uint32_t(get(&b[kHeadLen + genomeLen], 4)) != crc32(b.data(), kHeadLen + genomeLen)) return std::nullopt;
  FriendEggRecord r;
  r.friendLineage = get(&b[5], 8);
  r.friendSpecies = SpeciesId{b[13]};
  r.battleSeed = uint32_t(get(&b[14], 4));
  r.friendGenomeHash = uint32_t(get(&b[18], 4));
  std::optional<Genome> g = Genome::parse(&b[kHeadLen], genomeLen);
  if (!g) return std::nullopt;
  return FriendEgg{r, std::move(*g)};
}

bool FriendEggStore::save(Storage& s, const FriendEgg& egg) {
  const std::vector<uint8_t>& g = egg.genome.bytes();
  std::vector<uint8_t> b(kMagic, kMagic + sizeof kMagic);
  b.reserve(kHeadLen + g.size() + kCrcLen);
  put(b, kVersion, 1);
  put(b, egg.record.friendLineage, 8);
  put(b, egg.record.friendSpecies.v, 1);
  put(b, egg.record.battleSeed, 4);
  put(b, egg.genome.hash(), 4);
  put(b, g.size(), 2);
  b.insert(b.end(), g.begin(), g.end());
  put(b, crc32(b.data(), b.size()), 4);
  return s.writeAtomic(kFile, b.data(), b.size());
}

bool FriendEggStore::consume(Storage& s) { return s.rename(kFile, kUsed); }

}  // namespace blorb
