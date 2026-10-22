#pragma once
// ESP-NOW framing for duels (docs/design/BATTLES.md, "Radio"). Internal to
// DuelSession: nothing above the session sees a packet. decode() is the
// boundary parse; it refuses anything malformed, so the session only ever
// handles domain types.
//
//   [magic 'b' 'D'][version u8][kind u8] body... [crc32 u32 LE over all before it]
//
// Three kinds, all state-based ("everything I have so far"), so any one
// packet that arrives is enough and none needs an ack of its own:
//   Beacon  broadcast in fight mode, every 300 ms: my card, whom I claim, a sequence number, whether I am up for it
//   State   unicast once matched, 5 Hz while the friend lacks something, else 1 Hz:
//           my card, my whole move log, how many of THEIR moves I hold, my cheer, my hug
//   Genome  unicast after both hugs (friend egg): which of THEIR chunks I hold, and one of MINE they lack
// No packet carries a name or any free text, ever.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include "blorb/air.h"
#include "blorb/duel.h"
#include "blorb/duel_card.h"
#include "blorb/genome.h"

namespace blorb {

inline constexpr uint8_t kWireMagic0 = 'b', kWireMagic1 = 'D', kWireVersion = 2;   // 2: fight mode's claims (0.7.4); a 0.7.3 badge refuses it, and it them

// Cheers: pictures only, all kind. A double knock sends one of the first
// four; Plead is the downed kid's only voice in Mercy.
enum class Emote : uint8_t { None, Heart, Star, Laugh, Wave, Plead };

struct BeaconPacket {
  uint16_t roster = 0;           // duelRulesHash() (duel.h): the roster and the rules; a mismatch greys the friend out
  uint32_t nonce = 0;            // fresh per open(); half the battle seed
  PeerAddr wants{};              // whom I claim (none: nobody); a claim on someone else says "already paired"
  uint16_t seq = 0;              // bumps per beacon from this session, so a late one never overrides a newer
  Decline decline = Decline::None;   // shown on the friend's screen as a picture; never matched while set
  Stakes ceiling = Stakes::Spar;     // the highest tier this pet may enter on this device
  BattleCard card{};             // encode() writes lineageId as 0 in a beacon: no permanent id is broadcast;
                                 // the matched friend learns it from the unicast State
};

struct StatePacket {
  uint16_t roster = 0;
  BattleId battle{};
  uint32_t nonce = 0;
  // Staking. `chosen` is frozen once `confirmed`. The duel starts at
  // agreedStakes(mine, theirs) once both are; a State already carrying a
  // tier's BattleId is also its sender's confirm, so a lost confirm cannot
  // strand Staking.
  Stakes chosen = Stakes::Spar;
  bool confirmed = false;
  Stakes ceiling = Stakes::Spar;   // the highest tier I may enter against YOU (my pet, my device, my book)
  Decline why[3]{};                // by tier: the picture for why I may not enter it
  uint8_t skull[3]{};              // by tier: my pet's forecast, drawn in my corner of your screen too
  uint8_t hold = 0;                // 0..255: my Mortal confirm hold (Staking) or my Finish hold (Mercy)
  BattleCard card{};
  uint8_t moveCount = 0;
  uint8_t moves[DuelRules::kLogSlots]{};   // Move::pack() each
  uint8_t heard = 0;             // how many of your moves I hold: you may stop hurrying, and it acks a verdict
  uint8_t emoteSeq = 0;          // bumps per cheer, so a doubled packet is one cheer
  Emote emote = Emote::None;
  bool leaving = false;          // lid, drop, face down or a double press goodbye: ends it now
  bool rematch = false;          // asked on the friends screen
  bool hugged = false;           // hugged on the friends screen: yes to a friend egg
  bool greeted = false;          // I have heard your State in this bout: the start's third step
  uint16_t genomeBytes = 0;      // my genome's length once I hugged, so the friend knows the chunk count
  uint32_t genomeHash = 0;
};

// The friend egg's transfer (friend_egg.h). Both directions at once, state-
// based: `have` is a bitmap of the friend's chunks I already hold, and the
// chunk is one of mine the friend's last `have` lacked (round-robin). A lost
// or doubled packet is harmless; the transfer ends when both bitmaps fill.
inline constexpr size_t kGenomeChunk = 200;
inline constexpr size_t kMaxGenomeChunks = (kMaxGenomeBytes + kGenomeChunk - 1) / kGenomeChunk;
static_assert(kMaxGenomeChunks <= 64, "a chunk bitmap is one u64");
struct GenomePacket {
  BattleId battle{};
  uint64_t have = 0;
  uint8_t index = 0;             // which of MY chunks this is
  uint8_t len = 0;
  uint8_t bytes[kGenomeChunk]{};
};

using Packet = std::variant<BeaconPacket, StatePacket, GenomePacket>;

// Byte counts, each field at its wire width (duel_wire.cpp writes exactly
// these; test_duel_wire holds encode() to them).
inline constexpr size_t kLookWireBytes = kRegionCount * 3 + 1 + 1 + 1 + 4 * 2 + 4;
inline constexpr size_t kCardWireBytes = 1 + 1 + 1 + 1 + 4 + 1 + 1 + 8 + kLookWireBytes;
inline constexpr size_t kFrameOverhead = 4 + 4;   // header + crc
inline constexpr size_t kBattleIdBytes = 6;
inline constexpr size_t kBeaconWireBytes = kFrameOverhead + 2 + 4 + 6 + 2 + 1 + 1 + kCardWireBytes;
inline constexpr size_t kStateWireBytes = kFrameOverhead + 2 + kBattleIdBytes + 4 + 1 + 1 + 1 + 3 + 3 + 1 +
                                          kCardWireBytes + 1 + DuelRules::kLogSlots + 1 + 1 + 1 + 1 + 2 + 4;
inline constexpr size_t kGenomeWireBytes = kFrameOverhead + kBattleIdBytes + 8 + 1 + 1 + kGenomeChunk;
static_assert(kBeaconWireBytes <= kDatagramMax && kStateWireBytes <= kDatagramMax && kGenomeWireBytes <= kDatagramMax,
              "a duel packet must fit one ESP-NOW v1 frame: never split a packet");

// A Genome packet's `len` is its chunk's real length; the frame still
// carries all kGenomeChunk bytes, so every frame of a kind is one size.
size_t encode(const Packet&, uint8_t (&out)[kDatagramMax]);
// Refuses: a length other than its kind's, wrong magic, a newer version, an
// unknown kind, a bad crc, a card version this firmware does not speak, an
// enum byte past its last value, moveCount or heard past kLogSlots, a chunk
// index or length past kMaxGenomeBytes. A move byte is folded by
// Move::unpack, so every move byte is legal.
std::optional<Packet> decode(const uint8_t*, size_t);

// One frame in a few words, for a log or the sim film's ticker: "beacon",
// "beacon wants", "state t5 h3 Scrap", "genome 4 have 3", or why decode()
// refuses it ("short", "magic", "version", "kind", "bad crc", "bad body").
// t is how many moves the sender has locked, h how many of the friend's it holds.
struct WireText { char s[40]; };
WireText describe(const uint8_t*, size_t);

}  // namespace blorb
