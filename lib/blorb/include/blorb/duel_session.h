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
#include "blorb/controls.h"
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
// double press (or v2's wheel row, or a body-sourced hang: held upside down,
// the same ask from a case that covers the button) while a creature lives. The Dish exposes
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
  bool hang = false;   // held upside down 1 s: in the lobby it leaves, on the friends screen it searches again (the double press, without a button)
  bool lid = false, dropped = false;
  bool bootDown = false;   // the button's level: the Mortal confirm, the Finish and the hug are 3 s holds
  // A hand moved the badge in this turn (tilt_aim.h AimArming): only then does
  // the time running out lock the aim; a badge lying tilted since before the
  // turn chooses nothing, and the pet picks (#24).
  bool aimMoved = true;
  // `remote` is the stimuli a phone fired this tick (SenseOut::remote, unit D9):
  // none of them is a gesture here, so no verb or twist can lock a move.
  static DuelInput from(const SenseOut& raw, uint64_t remote, Fx tiltX, Fx tiltY, bool bootDown);
};

// Which stimuli the duel takes for itself, and which gestures ask for it,
// are the controls table (controls.h) — one place to rebind them.

enum class DuelPhase : uint8_t {
  Closed,     // radio off: not in fight mode
  Lobby,      // fight mode: beaconing and claiming, until a pair locks, a double press, or 5 min with no fight
  Greeting,   // a pair locked: still drawn as fight mode ("connecting") until the friend's State proves they are in too
  Staking,    // the fight shows. Each kid's BOOT press steps Spar, Scrap, Mortal; a 3 s hold locks it, and
              // after kStakeAutoTicks the pick locks itself (an unheld Mortal as Scrap); both pets' skulls show
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

inline constexpr uint8_t kMaxLobbyPeers = 4;   // drawn in a view
inline constexpr uint8_t kMaxLobbyHeard = 8;   // remembered in fight mode: nine badges in a room each hear eight

struct DuelTimings {
  static constexpr uint16_t kFightModeTicks = 3000; // 5 min in fight mode with no fight: radio off
  static constexpr uint16_t kGreetTicks = 50;       // a locked pair that never hears the friend's State: back to searching
  // A turn's time to choose: kChooseTicks the first turn, kChooseShrinkPct
  // shorter each turn after, never under kChooseFloorTicks (the owner,
  // 2026-10-21: shorter as the rounds go on, "but not so dramatically it's
  // unusable"). By turn: 10.0, 8.8, 7.7, 6.7, then 6.0 s. Six seconds still
  // gives a six-year-old time to read the turn, tilt (~0.5-1 s) and see the
  // aim light up before it locks. From the turn both badges share, so they
  // agree with no clock between them. When it runs out the aimed stance
  // locks as a knock would lock it (tilt and wait); level, the pet picks.
  static constexpr uint16_t kChooseTicks = 100;
  static constexpr uint8_t kChooseShrinkPct = 12;
  static constexpr uint16_t kChooseFloorTicks = 60;
  static constexpr uint16_t chooseTicks(uint32_t turn) {
    uint32_t t = kChooseTicks;
    for (uint32_t i = 0; i < turn && t > kChooseFloorTicks; ++i) t = t * (100 - kChooseShrinkPct) / 100;
    return uint16_t(t < kChooseFloorTicks ? kChooseFloorTicks : t);
  }
  static constexpr uint16_t kRevealTicks = 30;
  static constexpr uint16_t kFaintTicks = 40;
  static constexpr uint16_t kFallenTicks = 50;      // the fallen side keeps sending, so the victor hears the ack
  static constexpr uint16_t kFriendsTicks = 120;
  static constexpr uint16_t kStakingTicks = 200;    // 20 s to agree on stakes, else back to searching
  static constexpr uint16_t kStakeAutoTicks = 80;   // 8 s of Staking: my pick locks itself
  static constexpr uint16_t kSilentTicks = 200;     // 20 s with no packet: they wandered off
  static constexpr uint16_t kLobbyForgetTicks = 30; // a friend unheard for 3 s is forgotten (and so is a claim on them)
  static constexpr uint16_t kPinTicks = 100;        // a rematch waits 10 s for its friend, then searches for anyone
  static constexpr uint16_t kHugHoldTicks = 30;     // BOOT held 3 s on the friends screen
  static constexpr uint8_t kBeaconEvery = 3, kHurryEvery = 2, kIdleEvery = 10;
  static constexpr uint16_t kClaimPatienceTicks = 40;  // 4 s claiming a friend who claims nobody and never me: try another
  static constexpr uint16_t kBusyPatienceTicks = 120;  // 12 s (+ up to 3 s jitter) for a friend busy with a pending claim
  static constexpr uint16_t kSetAsideTicks = 150;      // 15 s a friend who claims nobody and never answered waits
  static constexpr uint16_t kGreetAsideTicks = 60;     // 6 s a friend whose greeting fell through waits (a crossed race costs little)
  static constexpr uint8_t kLeavingBurst = 3;       // `leaving` goes out this many times on the way out
  static constexpr uint16_t kGenomeTicks = 150;     // 15 s to swap genomes after both hugs, else the egg fizzles
  static constexpr uint8_t kChunksPerSend = 4;
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
  uint8_t heardCount = 0;               // fight mode: every friend heard in the last 3 s (up to kMaxLobbyHeard)
  Decline myDecline = Decline::None;    // the lobby: this pet only beacons his picture and never matches
  uint16_t fightTicksLeft = 0;          // fight mode: until the radio gives up with no fight

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
  uint16_t chooseTicks = DuelTimings::kChooseTicks;   // this turn's whole time to choose (DuelTimings::chooseTicks)
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
  std::optional<DuelSetup::Pin> rematch;   // both asked: fight mode searches for this friend first
  bool backToLobby = false;             // a battle (or a pairing) ended and fight mode searches again: refresh()
  bool radioClosed = false;             // fight mode ended
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
  bool searching() const { return std::holds_alternative<Lobby>(state_); }
  bool pinned() const { return setup_.pin.has_value(); }   // a rematch is searching for its friend
  // Fight mode between fights: the pet as he is now (wounds, ceiling, card),
  // keeping this lobby's nonce and pin. Only while searching().
  void refresh(const DuelSetup&);
  // Whether he is up for a fight now: a resting or sleepy pet stays in fight
  // mode, beaconing his picture, and is claimed again once he is ready.
  void setDecline(Decline);
  DuelView view(uint32_t tick) const;
  // The turn being chosen now (0, 1, ...), or -1: none (a light view, for
  // the Dish's tick, where a whole DuelView is too big for the stack).
  int16_t choosingTurn() const;

 private:
  friend struct DuelTestAccess;
  struct Closed { EndReason why = EndReason::None; };
  struct Lobby {
    struct Seen { PeerAddr addr; BeaconPacket beacon; uint32_t heardTick; int8_t rssi; };
    Seen seen[kMaxLobbyHeard]{};
    uint8_t count = 0;
    PeerAddr wants{};                   // my claim
    uint32_t wantsSince = 0;            // when I began claiming `wants`
    uint32_t openedTick = 0, lastBeacon = 0;
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
    uint16_t lockSeq = 0;               // the friend's beacon that locked the pair: a newer one that
                                        // claims someone else, or has another nonce, means they left
    // The start, in three steps over a lossy air: each locks on the other's
    // claim; each hears the other's State (greeted, sent from then on); each
    // shows the fight only on hearing a State that says the friend heard
    // mine. A link lost either way shows the fight on neither badge.
    bool greeted = false;
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
  std::variant<Closed, Lobby, Battle, Farewell> state_{Closed{}};
  DuelSetup setup_{};
  PeerAddr self_{};
  int8_t aim_ = -1;
  uint32_t modeSince_ = 0;   // fight mode began, or its last fight ended: 5 min from here it gives up
  uint16_t beaconSeq_ = 0;
  EndReason lastEnd_ = EndReason::None;   // why the last battle or pairing ended, for the log

  // Fight mode's pairing, computed by each badge alone from beacons, so no
  // "accept" message exists. A badge that is ready and claims nobody claims
  // the lowest-address friend it hears who is ready and claims nobody or it,
  // and keeps that claim while that stays true (so a lower friend arriving
  // never breaks a claim already answered). A claim on someone else is the
  // "already paired" reply. The pair locks when two claims meet, so no badge
  // is ever claimed by two: its one claim names one friend. A pinned lobby (a
  // rematch) claims its pin only. Any range the radio reaches; no RSSI gate.
  //
  // A crowded room. A claim I can see go unanswered is pending, not paired,
  // so its maker is still free to claim. A friend who claims nobody and never
  // claims me back (he hears nobody: across the room, in a case, a body
  // between) is set aside for kSetAsideTicks; one whose greeting fell through
  // (his claim reaches me, his States never do) for kGreetAsideTicks. A set
  // aside is lifted while nobody else is open, so two badges alone never wait
  // on it. A new claim prefers a friend claiming me, then the lowest address.
  // A busy friend (his own claim pending) gets a longer, jittered patience,
  // so a ring of badges each waiting on the next always breaks. Without these, a
  // badge everyone heard but who heard nobody held every claim forever.
  std::optional<uint8_t> claim(Lobby&, uint32_t tick);
  struct SetAside { PeerAddr addr; uint32_t until = 0; bool hard = false; };   // hard: his greeting fell through
  SetAside setAside_[kMaxLobbyHeard]{};
  bool setAside(const PeerAddr&, uint32_t tick, bool hardOnly) const;
  void setAsideFor(const PeerAddr&, uint32_t tick, bool hard);

  void enterLobby(uint32_t tick);
  void lobbyTick(Lobby&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void beacon(Air&, PeerAddr wants);
  // A battle or a pairing is over: say goodbye (in a battle), and fight mode
  // searches again. `fought`: the fight showed, so the 5 min starts over.
  void backToLobby(Air&, uint32_t tick, EndReason, bool goodbye, DuelEffects&, bool fought);
  void enterBattle(const Lobby&, uint8_t peer, uint32_t tick);
  void battleTick(Battle&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void onState(Battle&, const StatePacket&, uint32_t tick);
  void sendState(Battle&, Air&, uint32_t tick, const Farewell*, bool leaving);
  void stake(Battle&, const DuelInput&, uint32_t tick);
  bool play(Battle&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void applyResolved(Battle&, uint32_t tick, DuelEffects&);
  void cheer(Battle&, Emote, uint32_t tick);
  void finish(Battle&, Air&, uint32_t tick, DuelEffects&, EndReason, bool theyLeft);
  void enterFarewell(Battle&, uint32_t tick, DuelEffects&);
  void farewellTick(Farewell&, Air&, const DuelInput&, uint32_t tick, DuelEffects&);
  void closeWith(Air&, EndReason, bool goodbye);
  bool sameBout(const Battle&, BattleId) const;
  Outcome resolveFor(const Battle&) const;
  bool specialFits(const Battle&, Windup) const;
  uint8_t assistFor(const Battle&, uint8_t side) const;
  Meeting meetingOf(const Battle&) const;
};

}  // namespace blorb
