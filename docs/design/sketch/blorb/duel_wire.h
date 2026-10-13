#pragma once
// SKETCH (docs/design/BATTLES.md). ESP-NOW framing for duels. Internal to
// DuelSession: nothing above the session sees a packet (boundary
// discipline). decode() is the boundary parse; it rejects anything malformed
// and the session only ever sees domain types.
//
//   [magic 'b' 'D'][version u8][kind u8] body... [crc32 u32 LE over all before it]
//
// Three kinds, all state-based ("everything I have so far"), so any one
// packet that arrives is enough and none needs an ack of its own:
//   Beacon  broadcast in the lobby, every 300 ms: my card, whom I chose, my last knock, whether I am up for it
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

inline constexpr uint8_t kWireMagic0 = 'b', kWireMagic1 = 'D', kWireVersion = 1;

// Cheers sent with a double knock: pictures only, all kind.
enum class Emote : uint8_t { None, Heart, Star, Laugh, Wave };

struct BeaconPacket {
  uint16_t roster = 0;           // rosterHash(): a mismatch greys the friend out
  uint32_t nonce = 0;            // fresh per open(); half the battle seed
  PeerAddr wants{};              // whom my owner pointed at and knocked (none: nobody yet)
  uint8_t knockAgoTicks = 255;   // ticks since my last knock, 255 = none: the clink
  Decline decline = Decline::None;   // shown on the friend's screen as a picture; never matched while set
  BattleCard card{};
};

struct StatePacket {
  uint16_t roster = 0;
  BattleId battle{};
  uint32_t nonce = 0;
  BattleCard card{};
  uint8_t moveCount = 0;
  uint8_t moves[DuelRules::kMaxTurns]{};   // Move::pack() each
  uint8_t heard = 0;             // how many of your moves I hold: you may stop hurrying
  uint8_t emoteSeq = 0;          // bumps per cheer, so a doubled packet is one cheer
  Emote emote = Emote::None;
  bool leaving = false;          // lid, drop or wandered off: end as a draw now
  bool rematch = false;          // knocked on the friends screen
  bool hugged = false;           // cradled on the friends screen: yes to a friend egg
  uint16_t genomeBytes = 0;      // my genome's length, so the friend knows the chunk count
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

// Byte counts, arithmetic over the layout above (each field at its wire width).
inline constexpr size_t kLookWireBytes = kRegionCount * 3 + 1 + 1 + 1 + 4 * 2 + 4;
inline constexpr size_t kCardWireBytes = 1 + 1 + 1 + 1 + 4 + 1 + 1 + 8 + kLookWireBytes;
inline constexpr size_t kFrameOverhead = 4 + 4;   // header + crc
inline constexpr size_t kBattleIdBytes = 5;
inline constexpr size_t kBeaconWireBytes = kFrameOverhead + 2 + 4 + 6 + 1 + 1 + kCardWireBytes;
inline constexpr size_t kStateWireBytes =
    kFrameOverhead + 2 + kBattleIdBytes + 4 + kCardWireBytes + 1 + DuelRules::kMaxTurns + 1 + 2 + 1 + 2 + 4;
inline constexpr size_t kGenomeWireBytes = kFrameOverhead + kBattleIdBytes + 8 + 1 + 1 + kGenomeChunk;
static_assert(kBeaconWireBytes <= kDatagramMax && kStateWireBytes <= kDatagramMax && kGenomeWireBytes <= kDatagramMax,
              "a duel packet must fit one ESP-NOW v1 frame: never split a packet");

size_t encode(const Packet&, uint8_t (&out)[kDatagramMax]);
// Rejects: short, wrong magic, newer version, unknown kind, bad crc, a card
// version this firmware does not speak, moveCount past kMaxTurns, a chunk
// index or length past kMaxGenomeBytes. A stance byte out of range is folded
// by Move::unpack, so every byte is legal there.
std::optional<Packet> decode(const uint8_t*, size_t);

}  // namespace blorb
