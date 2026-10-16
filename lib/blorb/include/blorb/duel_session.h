#pragma once
// One duel from "ready" to "friends" (docs/design/BATTLES.md), as a state
// machine the Dish ticks at 10 Hz. Its whole surface is open / tick / close /
// view. Behind it: the lobby, matching, the clink, staking, the wire codec,
// resend pacing, merging the friend's move log, timers, the pet's own choice
// when a kid lets the clock run out, the shake prophecy, cheers, mercy, the
// rematch, the hug and the friend egg's genome swap. The Dish sees domain
// types only, never a packet.
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
#include "blorb/duel_book.h"
#include "blorb/duel_card.h"
#include "blorb/duel_wire.h"
#include "blorb/friend_egg.h"
#include "blorb/registry.h"
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

// What the body did this tick, parsed once at the Dish from the raw stimuli,
// the tilt loci and the button's level. The aim is a rim sector: tilt left =
// Pounce, right = Guard, toward you = Trick, away = the lobby's fourth friend.
// In Staking the same sectors are Spar, Scrap and Mortal.
struct DuelInput {
  int8_t aim = -1;   // 0..3, or -1 level
  bool knock = false, doubleKnock = false, press = false, hold = false, doublePress = false;
  bool shake = false, cradle = false;
  bool lid = false, dropped = false;
  bool bootDown = false;   // the button's level: the Mortal confirm, the Finish and the hug are 3 s holds
  // `remote` is the stimuli a phone fired this tick (SenseOut::remote, unit D9):
  // none of them is a gesture here, so no verb or twist can lock a move.
  static DuelInput from(const SenseOut& raw, uint64_t remote, Fx tiltX, Fx tiltY, bool bootDown);
};

// Stimuli the duel takes for itself while it is open, so his body does not
// flinch at every lock-in knock. BOOT is the duel's in every phase (the
// special, the confirm, the Finish, the hug), so a heavy special never also
// tucks him in. A shake always tells the future, and a drop and the lid always
// reach him (they also end the duel): those are never taken.
constexpr uint64_t duelStimBit(StimId s) { return uint64_t(1) << s.v; }
inline constexpr uint64_t kDuelCaptures = duelStimBit(stim::knock) | duelStimBit(stim::double_knock) |
                                          duelStimBit(stim::button) | duelStimBit(stim::button_double);
inline constexpr uint64_t kHoldPhaseCaptures = kDuelCaptures | duelStimBit(stim::button_hold);
static_assert((kHoldPhaseCaptures & (duelStimBit(stim::shake) | duelStimBit(stim::dropped) |
                                     duelStimBit(stim::lid_down) | duelStimBit(stim::cradle))) == 0,
              "BOOT is the duel's; a shake, a drop, the lid and a cuddle are always the pet's");

enum class DuelPhase : uint8_t {
  Closed,     // radio off
  Lobby,      // ready: beaconing; nearby ready friends' faces sit on the rim
  Greeting,   // matched: VS splash until the friend's first State proves they are in too
  Staking,    // both kids pick a tier and confirm it with its gesture: knock (Spar), BOOT press
              // (Scrap), BOOT held 3 s (Mortal); both pets' skulls show
  Choosing,   // tilt to aim, knock to lock, BOOT for a special, shake for a prophecy
  Waiting,    // locked (or winding up); the friend has not
  Revealing,  // both known: the stances pop, the bonk plays, hearts fly (3 s)
  Fainting,   // a side at 0 hearts swoons (Spar, Scrap, or spared)
  Mercy,      // Mortal: the loser is down; the winner's kid spares or finishes
  Fallen,     // Mortal, finished: the fallen pet's last scene
  Friends,    // both pets under a heart; rematch, hug for a friend egg, or leave
};
// Fled: a side ended a Scrap or Mortal duel (lid, a drop, silence) while
// behind on hearts. It forfeits and keeps every wound already applied. A
// downed pet that leaves before the verdict lives and is fled. The winner's
// side leaving during Mercy is "the victor left": the downed pet lives and
// nobody fled.
enum class EndReason : uint8_t { None, Finished, LidDown, Dropped, WanderedOff, Conflict, LobbyTimeout, Left, Fled };

inline constexpr uint8_t kMaxLobbyPeers = 4;

struct DuelTimings {
  static constexpr uint16_t kLobbyTicks = 600;      // 60 s ready with no match: radio off
  static constexpr uint16_t kGreetTicks = 20;       // a one-sided match falls back to the lobby
  static constexpr uint16_t kChooseTicks = 100;     // 10 s, then the pet picks
  static constexpr uint16_t kRevealTicks = 30;
  static constexpr uint16_t kFaintTicks = 40;
  static constexpr uint16_t kFallenTicks = 50;      // the fallen side keeps sending, so the victor hears the ack
  static constexpr uint16_t kFriendsTicks = 120;
  static constexpr uint16_t kStakingTicks = 200;    // 20 s to agree on stakes, else back to the lobby
  static constexpr uint16_t kSilentTicks = 200;     // 20 s with no packet: they wandered off
  static constexpr uint16_t kLobbyForgetTicks = 30; // a friend unheard for 3 s leaves the rim
  static constexpr uint16_t kHugHoldTicks = 30;     // BOOT held 3 s on the friends screen
  static constexpr uint8_t kClinkWindowTicks = 6;   // two knocks within 600 ms are a clink (300 ms was too tight for kids)
  static constexpr uint8_t kBeaconEvery = 3, kHurryEvery = 2, kIdleEvery = 10;
  static constexpr uint8_t kLeavingBurst = 3;       // `leaving` goes out this many times on the way out
  static constexpr uint16_t kGenomeTicks = 150;     // 15 s to swap genomes after both hugs, else the egg fizzles
  static constexpr uint8_t kChunksPerSend = 4;
  // Graft from candidate C: a friend must be close. Below this a beacon is
  // ignored (not drawn, never matched). An estimate for ESP-NOW at 10 dBm:
  // -62 (arm's length) was never measured and missed kids standing apart, so
  // -75, a few steps across a room. Unit D12 still owes the 30 cm / 2 m reading.
  static constexpr int8_t kNearRssi = -75;
};

// The fairness rules that need both cards and this device's book: the
// assist cap, the kid's own picked turns, the two lines' head-to-head record
// and the for-keeps duel per pet day. Each device applies them to ITS kid and
// ITS book; the duel runs at the lower of the two choices, so either side's
// refusal holds.
Ceiling pairCeiling(const Ceiling& pet, const BattleCard& mine, const BattleCard& theirs, uint32_t myManualTurns,
                    DuelBook::HeadToHead, uint8_t forKeepsToday);

// What the Dish hands open(): this pet, as he is now.
struct DuelSetup {
  BattleCard card{};
  DuelSkill skill{};
  const Genome* genome = nullptr;   // sent only after both hugs
  Ceiling ceiling{};                // ceilingFor(creature, device ceiling)
  uint8_t skull[3]{};               // forecast(creature, tier).skull, by tier
  uint32_t manualTurns = 0;         // book.manualTurns(): Mortal needs 30 picked turns
  const DuelBook* book = nullptr;   // the head-to-head and today's for-keeps count, once the friend is known
  uint16_t petDay = 0;
  uint32_t nonce = 0;               // fresh per open: half the battle seed
  // Set when readiness() refuses but a friend may be near: the lobby beacons
  // the picture so it shows on their rim too, and never matches.
  Decline decline = Decline::None;
  // A rematch: reopened pinned to the last friend, who alone may match, at
  // the next bout number. Staking runs again in full.
  struct Pin { PeerAddr peer{}; uint8_t rematch = 0; };
  std::optional<Pin> pin;
};

// Plain data for the renderer and the goldens.
struct DuelView {
  DuelPhase phase = DuelPhase::Closed;
  EndReason ended = EndReason::None;
  uint16_t phaseTicks = 0;              // animation clock for this phase

  struct Peer { SpeciesId species; CardLook look; Decline decline; bool wantsMe, chosen, compatible; };
  Peer peers[kMaxLobbyPeers]{};
  uint8_t peerCount = 0;
  int8_t aimedPeer = -1;
  Decline myDecline = Decline::None;    // the lobby: this pet only beacons his picture and never matches

  BattleCard me{}, them{};
  // Staking: [me, them]. A tier above a side's ceiling is greyed with why[side][tier].
  Stakes ceiling[2]{};
  Decline why[2][3]{};
  uint8_t skull[2][3]{};                // each pet's forecast, by tier
  Stakes chosen = Stakes::Spar, theyChose = Stakes::Spar, agreed = Stakes::Spar;
  bool confirmed = false, theyConfirmed = false;
  uint8_t confirmHold = 0, theirHold = 0;   // 0..255: the Mortal hold, or in Mercy the Finish hold

  bool iAmDown = false, theyAreDown = false;
  uint16_t mercyTicksLeft = 0;
  Verdict verdict = Verdict::None;
  uint8_t newScars = 0;
  SideState mine{}, theirs{};
  int8_t aim = -1;
  Lean lean{};                          // what my pet would pick: glows brighter with the assist
  uint8_t assistHearts[2]{};
  bool locked = false, theyLocked = false;
  bool specialReady = false;            // the meter holds the power's cost and a special still fits
  uint8_t pipsShort = 0;                // greyed hint: how many pips short
  uint16_t chooseTicksLeft = 0;
  bool hasLast = false;
  TurnReport last{};                    // mirrored so index 0 is always me
  uint8_t lastTurn = 0;
  Ending ending = Ending::Ongoing;
  bool iWon = false, draw = false;

  bool prophesied = false;
  Foretelling prophecy{};
  bool prophecyKept = false, prophecyJudged = false;

  Emote emoteIn = Emote::None;
  uint16_t emoteTicks = 0;
  bool rematchAsked = false, theyAskedRematch = false;
  bool iHugged = false, theyHugged = false;
  uint8_t hugHold = 0;
  uint8_t eggProgress = 0;              // 0..255 while the genomes swap
  bool eggFizzled = false, eggLaid = false;
};

// What the Dish does with one duel tick.
struct DuelEffects {
  StimId stimuli[4]{};                  // Duel-sourced; the creature applies them through duelFilter()
  uint8_t stimCount = 0;
  uint64_t captured = 0;                // raw stimuli that were the duel's: the Dish removes them
  // Scrap and Mortal: each turn that resolved this tick, for my pet, in order.
  // Unclamped: Creature::wound clamps to woundRoom; then the Dish saves at once.
  TurnWound wounds[DuelRules::kMaxTurns]{};
  uint8_t woundCount = 0;
  bool downed = false;                  // Mortal: my pet was just downed: book.recordDowned(battle, victor), save
  BattleId battle{};                    // valid with `downed`
  uint64_t victor = 0;
  bool fall = false;                    // Mortal, finished, and I am the one downed: Creature::fall()
  std::optional<Meeting> met;           // the battle ended: DuelBook::record
  std::optional<FriendEgg> egg;         // both hugged and the genome arrived: FriendEggStore::save
  std::optional<DuelSetup::Pin> rematch;   // both asked: reopen with this pin (unless a pet fell)
  bool radioClosed = false;
  void stim(StimId s) { if (stimCount < 4) stimuli[stimCount++] = s; }
};

class DuelSession {
 public:
  // The only way in. The Dish has already checked readiness(). False, and
  // still closed, when Air::open() is refused (an OTA, an image on trial).
  bool open(LocalConsent, Air&, const DuelSetup&, uint32_t tick);
  // Drain the air, apply the body, advance timers, send what is due.
  DuelEffects tick(Air&, const DuelInput&, uint32_t tick);
  // Idempotent. Sends `leaving` (in a battle) and closes the radio; the
  // Meeting, if a battle was under way, is the last tick's.
  void close(Air&, EndReason);
  bool active() const { return !std::holds_alternative<Closed>(state_); }
  DuelView view(uint32_t tick) const;

 private:
  friend struct DuelTestAccess;
  struct Closed { EndReason why = EndReason::None; };
  struct Lobby {
    struct Seen { PeerAddr addr; BeaconPacket beacon; uint32_t heardTick; int8_t rssi; };
    Seen seen[kMaxLobbyPeers]{};
    uint8_t count = 0;
    int8_t aimed = -1;
    PeerAddr wants{};
    uint32_t openedTick = 0, lastBeacon = 0;
    std::optional<uint32_t> myKnock;
  };
  struct Battle {
    PeerAddr peer{};
    uint32_t theirNonce = 0;
    BattleCard theirs{};
    BattleId battle{};                  // the pair's id before turn 0, the agreed tier's after
    Side me = Side::A;                  // A is the lower address
    // Greeting, Staking, then Choosing for the whole battle proper (Waiting and
    // Revealing are derived in view()), then Mercy, Fallen or Fainting.
    DuelPhase phase = DuelPhase::Greeting;
    uint32_t phaseTick = 0, lastHeard = 0, lastSent = 0, lastBeacon = 0;
    // The lobby knock that matched: the greeting's beacons still carry it, so
    // a friend who knocked first, and matches only on hearing mine, can.
    std::optional<uint32_t> knock;
    bool theyLeft = false, conflict = false;
    // Staking
    Ceiling pair{};                     // mine against this friend
    Stakes chosen = Stakes::Scrap, theyChose = Stakes::Spar, theirCeiling = Stakes::Spar;
    Decline theirWhy[3]{};
    uint8_t theirSkull[3]{};
    bool confirmed = false, theyConfirmed = false;
    uint16_t holdTicks = 0;             // BOOT held: the Mortal confirm, or the Finish
    uint8_t theirHold = 0;
    // BOOT's level last tick, and whether it was already down when the skull
    // showed: a hold carried in (a heavy special held through the reveal) is
    // no Finish until BOOT comes up once.
    bool bootWasDown = false, heldIntoMercy = false;
    // The battle
    MoveLog mine, heard;                // per actor: I append to mine, I adopt into heard
    uint8_t theyHold = 0;               // their `heard`: how many of my moves they have
    Outcome outcome{};                  // derived: resolve() again whenever a log grows
    uint8_t applied = 0;                // turns whose wounds, stimuli and learning are done
    uint32_t revealTick = 0, chooseTick = 0, heardGrew = 0, emoteOutTick = 0;
    bool revealing = false, choosing = false, endSeen = false, downedSent = false;
    bool metSent = false, slain = false;
    std::optional<Foretelling> prophecy;
    uint8_t prophecyTurn = 0;
    bool prophecyJudged = false, prophecyKept = false;
    uint8_t emoteSeqOut = 0, emoteSeqIn = 0;
    Emote emoteOut = Emote::None, emoteIn = Emote::None;
    uint32_t emoteInTick = 0;
    bool emoteSeqKnown = false;
    DuelSkill skill{};
    uint8_t scarsTaken = 0;
  };
  struct Farewell {
    Battle last;
    uint32_t since = 0;
    bool iAsked = false, theyAsked = false;
    bool iHugged = false, theyHugged = false;
    uint16_t hugHold = 0;
    uint16_t theirGenomeBytes = 0;
    uint32_t theirGenomeHash = 0;
    bool swapping = false, eggDone = false, eggFizzled = false;
    uint32_t swapSince = 0, fizzledAt = 0;
    uint64_t haveTheirs = 0, theyHaveMine = 0;  // chunk bitmaps
    uint8_t nextChunk = 0;
    std::vector<uint8_t> theirGenome;           // up to kMaxGenomeBytes, only while swapping (PSRAM on the board)
  };
  std::variant<Closed, Lobby, Battle, Farewell> state_;
  DuelSetup setup_{};
  PeerAddr self_{};
  int8_t aim_ = -1;

  // The match rule, computed by each side alone, so no "accept" message exists:
  // I take friend P when we chose each other (P.wants == me && my wants == P),
  // or when our knocks landed within kClinkWindowTicks and P is the only
  // friend whose knock did. A peer under kNearRssi, on another roster, or
  // declining is never matched; a pinned session matches its pin only.
  static std::optional<uint8_t> match(const Lobby&, PeerAddr self, const std::optional<DuelSetup::Pin>&,
                                      uint32_t tick);

  void enterLobby(Air&, uint32_t tick);
  void lobbyTick(Lobby&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void beacon(Air&, PeerAddr wants, std::optional<uint32_t> knock, uint32_t tick);
  void enterBattle(const Lobby&, uint8_t peer, uint32_t tick);
  void battleTick(Battle&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void onState(Battle&, const StatePacket&, uint32_t tick);
  void sendState(Battle&, Air&, uint32_t tick, const Farewell*, bool leaving);
  void stake(Battle&, const DuelInput&, uint32_t tick);
  bool play(Battle&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void applyResolved(Battle&, uint32_t tick, DuelEffects&);
  void cheer(Battle&, Emote, uint32_t tick);
  void finish(Battle&, Air&, DuelEffects&, EndReason, bool theyLeft);
  void enterFarewell(Battle&, uint32_t tick, DuelEffects&);
  void farewellTick(Farewell&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void closeWith(Air&, EndReason, bool goodbye);
  bool sameBout(const Battle&, BattleId) const;
  Outcome resolveFor(const Battle&) const;
  bool specialFits(const Battle&, Windup) const;
  bool holdsVerdict(const Battle&) const;
  uint8_t assistFor(const Battle&, uint8_t side) const;
  Meeting meetingOf(const Battle&) const;
};

}  // namespace blorb
