#pragma once
// SKETCH (docs/design/BATTLES.md). One duel from "ready" to "friends", as a pure state
// machine the Dish ticks at 10 Hz. Its whole surface is open / tick / close /
// view. Behind it: the lobby, matching, the clink, the wire codec, resend
// pacing, merging the friend's move log, timers, the pet's own choice when a
// kid lets the clock run out, the shake prophecy and its judging, emotes,
// the rematch, the hug and the friend egg's genome swap. The Dish sees domain types only, never a packet.
//
// Lockstep by state, not by messages: each device writes only its own
// MoveLog, sends the whole of it, and adopts the longest consistent prefix it
// hears. A turn resolves when both logs reach it. Loss, duplication, reorder
// and delay change only WHEN a device learns a move, never WHAT the battle is.
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include "blorb/air.h"
#include "blorb/duel.h"
#include "blorb/duel_card.h"
#include "blorb/duel_book.h"
#include "blorb/duel_wire.h"
#include "blorb/friend_egg.h"
#include "blorb/senses.h"

namespace blorb {

class DishDuel;          // dish_duel.h
struct DuelTestAccess;   // test/support only

// Proof that this device's own owner asked for a duel. Only DishDuel can make
// one, and it does so in exactly one place: routing a body-sourced BOOT
// double press (or v2's wheel row) while a creature lives. The Dish exposes
// DishDuel to nobody but its own tick, so no protocol verb, twist or packet
// can reach a line that makes one.
class LocalConsent {
  friend class DishDuel;
  friend struct DuelTestAccess;
  LocalConsent() = default;
};

// What the body did this tick, parsed once at the Dish from the raw stimuli
// and the tilt loci. The aim is a rim sector: tilt left = Pounce (orange paw),
// right = Guard (blue leaf shield), toward you = Trick (purple swirl). Level,
// or tilted away, aims at nothing.
struct DuelInput {
  int8_t aim = -1;   // Stance index, or -1
  bool knock = false, doubleKnock = false, press = false, shake = false, lid = false, dropped = false;
  static DuelInput from(const SenseOut& raw, Fx tiltX, Fx tiltY);
};

// Stimuli the duel takes for itself while active, so his body does not
// flinch at every lock-in knock. Shake, BOOT hold, drop and lid are never
// taken: shaking always tells the future (ask 25), a hold always feeds, and
// a drop or the lid always reaches him (they also end the duel).
constexpr uint64_t duelStimBit(StimId s) { return uint64_t(1) << s.v; }
inline constexpr uint64_t kDuelCaptures =
    duelStimBit(stim::knock) | duelStimBit(stim::double_knock) | duelStimBit(stim::button);
static_assert((kDuelCaptures & (duelStimBit(stim::shake) | duelStimBit(stim::button_hold) |
                                duelStimBit(stim::dropped) | duelStimBit(stim::lid_down))) == 0,
              "a duel may not swallow a shake, a hold, a drop or the lid");

enum class DuelPhase : uint8_t {
  Closed,     // radio off
  Lobby,      // ready: beaconing; nearby ready friends' faces sit on the rim
  Greeting,   // matched: VS splash until the friend's first State proves they are in too
  Staking,    // both kids pick a tier on the rim (tilt), see both forecasts (skulls), and confirm
              // with the tier's gesture: knock (Spar), BOOT press (Scrap), BOOT held 3 s (Mortal)
  Choosing,   // tilt to aim, knock to lock, BOOT to power up, shake for a prophecy
  Waiting,    // locked; the friend has not ("..." over their head; a tick mark once they lock)
  Revealing,  // both known: the stances pop, the bonk plays, hearts fly (3 s)
  Fainting,   // the loser swoons, spiral eyes; the winner hops in circles (Spar, Scrap)
  Mercy,      // Mortal: the loser is down. The winner's kid holds BOOT 3 s to finish, or knocks
              // (or waits 15 s) to spare. The loser's kid sees the skull fill and can only plead (a cheer).
  Fallen,     // Mortal, finished: the fallen pet's last scene; the victor takes the trophy
  Friends,    // both pets side by side under a heart; knock for a rematch, hug (cradle 3 s) for a friend egg
};
enum class EndReason : uint8_t { None, Finished, LidDown, Dropped, WanderedOff, Conflict, LobbyTimeout, StakesRefused, Fled };
// Fled: a side ended a Scrap or Mortal duel (lid, drop, silence) while losing. It forfeits:
// the other side wins, and the one who fled still takes the wound of the hearts it lost.
// A pet downed in a Mortal duel that flees before the verdict takes the downed wound but
// lives: yanking the cable is not policed, and the book remembers it as "fled".

inline constexpr uint8_t kMaxLobbyPeers = 4;

struct DuelTimings {
  static constexpr uint16_t kLobbyTicks = 600;      // 60 s ready with no match: radio off
  static constexpr uint16_t kGreetTicks = 20;       // the VS splash
  static constexpr uint16_t kChooseTicks = 100;     // 10 s, then the pet picks
  static constexpr uint16_t kRevealTicks = 30;
  static constexpr uint16_t kFaintTicks = 40;
  static constexpr uint16_t kFriendsTicks = 120;
  static constexpr uint16_t kStakingTicks = 200;    // 20 s to agree on stakes, else back to the lobby
  static constexpr uint16_t kSilentTicks = 200;     // 20 s with no packet: they wandered off
  static constexpr uint8_t kClinkWindowTicks = 3;   // two knocks within 300 ms are a clink
  static constexpr uint8_t kBeaconEvery = 3, kHurryEvery = 2, kIdleEvery = 10;
  static constexpr uint16_t kGenomeTicks = 150;     // 15 s to swap genomes after both hugs, else the egg fizzles;
                                                    // Friends stays open while a swap runs
  static constexpr uint8_t kChunksPerSend = 4;      // Genome packets per send at 5 Hz: 41 chunks in about 2 s without loss
  // Graft from candidate C: a friend must be close. Below this a beacon is
  // ignored (not drawn, never matched). An estimate for ESP-NOW at 10 dBm, about
  // arm's length; unit D12 sets it on a desk at 30 cm and 2 m.
  static constexpr int8_t kNearRssi = -62;
};

// Plain data for the renderer (lib/paint/duel_scene.cpp) and the goldens.
struct DuelView {
  DuelPhase phase = DuelPhase::Closed;
  EndReason ended = EndReason::None;
  uint16_t phaseTicks = 0;              // animation clock for this phase

  // Lobby: up to four ready friends round the rim, the aimed one bobbing.
  struct Peer { SpeciesId species; CardLook look; Decline decline; bool wantsMe, chosen, compatible; };
  Peer peers[kMaxLobbyPeers]{};
  uint8_t peerCount = 0;
  int8_t aimedPeer = -1;

  // Battle: me at the bottom, the friend at the top, both screens alike.
  BattleCard me{}, them{};
  // Staking: the rim shows three tiers; the ones a pet or the fairness rule
  // forbids are greyed with the reason (a cradle for "too young", a balance
  // for "not a fair fight", a skull for "this could kill him").
  Stakes ceiling[2]{};                  // [me, them]
  Stakes chosen = Stakes::Spar, theyChose = Stakes::Spar, agreed = Stakes::Spar;
  bool confirmed = false, theyConfirmed = false;
  uint8_t skull[2]{};                   // [me, them]: each pet's forecast at the agreed tier
  uint8_t confirmHold = 0;               // 0..255 while BOOT is held for Mortal
  // Mercy and the end.
  bool iAmDown = false, theyAreDown = false;
  uint16_t mercyTicksLeft = 0;
  uint8_t finishHold = 0;                // 0..255: the skull filling while the winner holds BOOT
  Verdict verdict = Verdict::None;
  uint8_t newScars = 0;                  // drawn appearing on my pet at the end
  SideState mine{}, theirs{};
  int8_t aim = -1;                      // stance under the tilt now
  Lean lean{};                          // what my pet would pick: he leans toward it, glowing brighter with the assist
  uint8_t assistHearts[2]{};            // [me, them]: drawn as the pet cheering, never as a label
  bool locked = false, theyLocked = false, powerArmed = false;
  uint16_t chooseTicksLeft = 0;         // the shrinking rim ring
  bool hasLast = false;
  TurnReport last{};                    // mirrored so index 0 is always me
  Ending ending = Ending::Ongoing;
  bool iWon = false, draw = false;

  bool prophesied = false;              // shake: the bubble over the friend
  Foretelling prophecy{};
  bool prophecyKept = false, prophecyJudged = false;

  Emote emoteIn = Emote::None;          // a cheer from the friend, floating up
  uint16_t emoteTicks = 0;
  bool rematchAsked = false, theyAskedRematch = false;
  bool iHugged = false, theyHugged = false;   // two hearts fill on the friends screen
  uint8_t eggProgress = 0;              // 0..255 while the genomes swap; the speckled egg rolls in at 255
  bool eggFizzled = false;
};

// What the Dish does with one duel tick.
struct DuelEffects {
  StimId stimuli[4]{};                  // Duel-sourced; the creature applies them through duelFilter()
  uint8_t stimCount = 0;
  uint64_t captured = 0;                // which of this tick's raw stimuli were the duel's (within kDuelCaptures)
  std::optional<Meeting> met;           // the battle ended: DuelBook::record
  std::optional<Wound> wound;           // Scrap and Mortal: my pet's injury, scars and whether he is down
  bool fall = false;                    // Mortal, finished, and I am the one downed: Creature::fall()
  std::optional<FriendEggRecord> egg;   // both hugged and the genome arrived: FriendEggStore::save(record, friendGenome)
  const Genome* friendGenome = nullptr; // valid with `egg`, until the next tick
  bool radioClosed = false;
};

class DuelSession {
 public:
  // The only way in. The Dish has already checked he can duel (a creature,
  // awake, sleepiness under 0.8). False when Air::open() is refused (OTA).
  bool open(LocalConsent, Air&, const BattleCard& mine, const DuelSkill&, const Genome& myGenome, Stakes ceiling,
            uint8_t skullByTier[3], uint32_t nonce, uint32_t tick);
  // Drain the air, apply the body, advance timers, send what is due.
  DuelEffects tick(Air&, const DuelInput&, uint32_t tick);
  void close(Air&, EndReason);          // idempotent; sends `leaving` once if in a battle
  bool active() const { return !std::holds_alternative<Closed>(state_); }
  DuelView view(uint32_t tick) const;

 private:
  struct Closed {};
  struct Lobby {
    struct Seen { PeerAddr addr; BeaconPacket beacon; uint32_t heardTick; };
    Seen seen[kMaxLobbyPeers]{};
    uint8_t count = 0;
    int8_t aimed = -1;
    PeerAddr wants{};
    uint32_t openedTick = 0, myKnockTick = 0;
    bool knocked = false;
  };
  struct Battle {
    PeerAddr peer{};
    BattleCard theirs{};
    BattleId battle{};
    Side me = Side::A;                  // A is the lower address: display order only
    MoveLog mine, heard;                // per actor: I append to mine, I adopt into heard
    uint8_t theyHold = 0;               // their `heard`: how many of my moves they have
    DuelPhase phase = DuelPhase::Greeting;
    uint32_t phaseTick = 0, lastHeardTick = 0, lastSentTick = 0;
    bool powerArmed = false, greeted = false;
    std::optional<Foretelling> prophecy;
    uint8_t prophecyTurn = 0;
    Emote emoteOut = Emote::None, emoteIn = Emote::None;
    uint8_t emoteSeqOut = 0, emoteSeqIn = 0;
    uint32_t emoteInTick = 0;
    DuelSkill skill{};                  // learns turn by turn; handed back in the Meeting
    Outcome outcome{};                  // derived: resolve() again whenever a log grows
  };
  struct Farewell {
    Battle last;
    uint32_t since = 0;
    bool iAsked = false, theyAsked = false;   // both knock: a rematch, BattleId{duelSeed(nonces, n + 1), n + 1}
    bool iHugged = false, theyHugged = false;
    uint64_t haveTheirs = 0, theyHaveMine = 0;  // chunk bitmaps
    std::vector<uint8_t> theirGenome;           // up to kMaxGenomeBytes, only while swapping (PSRAM on the board)
  };
  std::variant<Closed, Lobby, Battle, Farewell> state_;
  BattleCard mine_{};
  DuelSkill skill_{};
  const Genome* myGenome_ = nullptr;   // the Dish's creature's; sent only after both hugs
  uint32_t nonce_ = 0;

  // Match rule, computed by each side alone, so no "accept" message exists:
  // I take friend P when we chose each other (P.wants == me && my wants == P),
  // or when our knocks landed within kClinkWindowTicks and P is the only
  // ready friend whose knock did. A peer under kNearRssi, or one that declines,
  // is never matched. Greeting keeps beaconing with `wants` set, so a friend
  // whose first State was lost still sees the match.
  //
  // Rematch liveness: a State from my matched peer naming BattleId{duelSeed(
  // nonces, n + 1), n + 1} counts as their rematch ask, so if their kid knocked
  // and they moved on before my `rematch` flag reached them, I follow instead
  // of waiting in Friends. Greeting ends only on P's State for the
  // same battle id, so a one-sided match times back to the lobby in 2 s.
  static std::optional<uint8_t> match(const Lobby&, PeerAddr self, uint32_t tick);
};

}  // namespace blorb
