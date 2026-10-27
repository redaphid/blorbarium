#pragma once
// Thoughts and prophecies: grungo says a line now and then, and the renderer
// scrolls it once across the dish (defs/thoughts.def). The Thinker picks
// which, from what anyone could see of him (drives, the action or reflex,
// stage, sickness, foresee, what his brain learned, the heirlooms in his
// genome, the pet clock) plus a draw seeded by his genome and the tick. Every
// shake brings a prophecy instead of a hop, the Magic 8-Ball way: a trance
// first (kTranceTicks of foresee face and silence), then the answer lands
// whole -- a day's first foretelling is always a fortune in his own voice,
// and after that a coin flip between an omen (his oracle weights its topics
// and picks the voice, voices.def) and a bare verdict (YES / NO / ASK AGAIN
// LATER, drawn in thoughts.cpp). He needs no phone; what one gives him
// (FromPhone) only adds lines.
//
// Presentation, not life: no line changes him, and the Thinker is not saved
// or hashed. A reboot starts it afresh, so a replay of the creature stays
// bit-identical with or without it.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include "blorb/creature.h"
#include "blorb/habitat.h"
#include "blorb/senses.h"

namespace blorb {

class Lineage;
struct Appearance;
struct Taught;   // taught.h

// An heirloom instinct in his genome, and the generation whose brain learned
// it (kUnknownGeneration when the lineage no longer says: founder-carried or compacted).
struct Heirloom { LocusId cue; ActionId action; DriveId drive; Fx level; uint16_t learnedBy; };
constexpr uint16_t kUnknownGeneration = 0xFFFF;
struct Heirlooms {
  Heirloom at[8]{};
  uint8_t count = 0;
};
Heirlooms heirloomsOf(const Genome&, const Lineage&);

// What a phone has given him. Each one adds lines; with none of them he
// still thinks and foretells on his own clock. None is saved with him or hashed.
struct FromPhone {
  uint32_t awayTicks = 0;             // powered ticks since a phone last gave the time
  std::optional<uint32_t> localNow;   // the owner's local wall time, seconds since 1970: a TIME this boot
  bool named = false;                 // the owner chose his name (TWIST rename)
  const Taught* taught = nullptr;     // what the owner taught him to say (TWIST teach)
};

// What a line's condition may look at.
struct Observed {
  const Creature& c;
  const Habitat& habitat;
  const PetClock& clock;
  const Heirlooms& heirlooms;
  uint32_t tick;
  FromPhone phone{};
};

// A condition; it may set the number the text shows in place of its '#'.
using ThoughtWhen = bool (*)(const Observed&, uint16_t& num);

namespace when {
#define BLORB_THOUGHT(id, name, topic, pred, priority, cooldownS, text) bool pred(const Observed&, uint16_t& num);
#include "blorb/defs/thoughts.def"
#undef BLORB_THOUGHT
}  // namespace when

namespace thought {
#define BLORB_THOUGHT(id, name, topic, pred, priority, cooldownS, text) inline constexpr ThoughtId name{id};
#include "blorb/defs/thoughts.def"
#undef BLORB_THOUGHT
}  // namespace thought

struct ThoughtInfo {
  ThoughtId id; const char* name; TopicId topic; ThoughtWhen when; uint8_t priority; uint16_t cooldownS; const char* text;
  const char* whenName;
  constexpr bool prophecy() const { return topic != topic::none; }
};
inline constexpr ThoughtInfo THOUGHTS[] = {
#define BLORB_THOUGHT(id, name, top, pred, priority, cooldownS, text) \
  {ThoughtId{id}, #name, topic::top, when::pred, priority, cooldownS, text, #pred},
#include "blorb/defs/thoughts.def"
#undef BLORB_THOUGHT
};
inline constexpr size_t kThoughtCount = countOf(THOUGHTS);
const ThoughtInfo* thoughtInfo(ThoughtId);

// A species' own lines for a row (species_lines.def, written by
// tools/personality.py from personalities/<species>.txt).
struct SpeciesLine { SpeciesId species; ThoughtId thought; const char* text; };
inline constexpr SpeciesLine SPECIES_LINES[] = {
#define BLORB_SPECIES_LINE(sid, sp, t, text) {SpeciesId{sid}, thought::t, text},
#include "blorb/defs/species_lines.def"
#undef BLORB_SPECIES_LINE
  {SpeciesId{0xFF}, ThoughtId{0xFF}, ""},   // keeps the array non-empty when no species has lines
};

// A species' own answers to a shake (species_verdicts.def, written by
// tools/personality.py from the verdict_* sections of personalities/<species>.txt):
// kind 0 yes, 1 no, 2 not yet. They stand in for the ball's plain ones, kind
// by kind, so a frog's YES is his own and not every pet's.
struct SpeciesVerdict { SpeciesId species; uint8_t kind; const char* text; };
inline constexpr SpeciesVerdict SPECIES_VERDICTS[] = {
#define BLORB_SPECIES_VERDICT(sid, sp, kind, text) {SpeciesId{sid}, kind, text},
#include "blorb/defs/species_verdicts.def"
#undef BLORB_SPECIES_VERDICT
  {SpeciesId{0xFF}, 0xFF, ""},   // keeps the array non-empty when no species has verdicts
};

// ---- the text ---------------------------------------------------------------------
constexpr size_t kMaxThoughtText = 30;
constexpr size_t kMaxVoicedText = 52;          // the room a said line has before its mark grows it
constexpr size_t kMaxNameText = 15;           // the saved name's 16 bytes
constexpr size_t kMaxTaughtText = kMaxThoughtText;
constexpr uint8_t kUrgentThought = 7;

// A line holds at most one mark, standing for what its condition gives: '#' a
// number (a condition that sets one), '@' his name (a condition named
// named...: the owner chose it), '*' a line the owner taught him (taught).
constexpr char kNumberMark = '#', kNameMark = '@', kTaughtMark = '*';
constexpr bool isMark(char c) { return c == kNumberMark || c == kNameMark || c == kTaughtMark; }

constexpr bool inFont(char c) {
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
  for (char p : {' ', '.', ',', '!', '?', '-', '\'', ':'})
    if (c == p) return true;
  return false;
}
constexpr size_t textLen(const char* s) {
  size_t n = 0;
  while (s[n]) ++n;
  return n;
}
constexpr bool startsWith(const char* s, const char* prefix) {
  while (*prefix && *s == *prefix) ++s, ++prefix;
  return !*prefix;
}
constexpr const char* whenOf(ThoughtId id) {
  for (const ThoughtInfo& t : THOUGHTS)
    if (t.id == id) return t.whenName;
  return "";
}
// In the font, 1 to `max` characters, at most one mark, and only a mark its condition gives.
constexpr bool writable(const char* text, size_t max, const char* when) {
  size_t marks = 0;
  for (const char* s = text; *s; ++s) {
    if (!inFont(*s) && !isMark(*s)) return false;
    if (*s == kNameMark && !startsWith(when, "named")) return false;
    if (*s == kTaughtMark && !startsWith(when, "taught")) return false;
    marks += isMark(*s);
  }
  const size_t n = textLen(text);
  return n > 0 && n <= max && marks <= 1;
}
constexpr bool thoughtsWritable() {
  for (const ThoughtInfo& t : THOUGHTS)
    if (!writable(t.text, kMaxThoughtText, t.whenName)) return false;
  for (const SpeciesLine& l : SPECIES_LINES)
    if (l.species.v != 0xFF && !writable(l.text, kMaxThoughtText, whenOf(l.thought))) return false;
  for (const SpeciesVerdict& v : SPECIES_VERDICTS) {
    if (v.species.v == 0xFF) continue;
    if (v.kind > 2 || !writable(v.text, kMaxThoughtText, "")) return false;
    for (const char* s = v.text; *s; ++s) if (isMark(*s)) return false;
  }
  return true;
}
constexpr bool sameName(const char* a, const char* b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}
constexpr bool speciesLinesNameTheirSpecies() {
#define BLORB_SPECIES_LINE(sid, sp, t, text)   if (sid >= kSpeciesCount || !sameName(SPECIES[sid].name, #sp)) return false;
#include "blorb/defs/species_lines.def"
#undef BLORB_SPECIES_LINE
#define BLORB_SPECIES_VERDICT(sid, sp, kind, text)   if (sid >= kSpeciesCount || !sameName(SPECIES[sid].name, #sp)) return false;
#include "blorb/defs/species_verdicts.def"
#undef BLORB_SPECIES_VERDICT
  return true;
}
static_assert(idsUnique(THOUGHTS), "thoughts.def has a duplicate id");
static_assert(kThoughtCount <= 255, "the Thinker indexes rows by a byte");
static_assert(thoughtsWritable(), "a line is too long, has two marks, a mark its condition does not "
                                  "give, or a character the font lacks");
static_assert(speciesLinesNameTheirSpecies(), "species_lines.def or species_verdicts.def: an id and a name disagree "
                                              "with species.def; rerun tools/personality.py build");

// What a line's '@' and '*' stand for: his name and one taught line, in the font.
struct Words { std::string_view name, taught; };

// The line as said, its mark replaced: '#' by `num`, '@' and '*' by `words`.
// If his species has its own lines for the row (species_lines.def), one of
// them is said exactly as written: his file is already his voice, so no terse
// cut shortens it. Otherwise the row's shared text, which a terse voice cuts to
// its first sentence. Nothing ever wraps or reshapes a line: no voice frame,
// prefix, suffix or voice's own rewrite (the owner removed "THE REEDS SAY:",
// "... SO IT IS." and the voices' versions of shared lines).
// `pick` chooses among them, modulo their count. Returns the length.
// The one mark grows a line most as a taught line ('#' is 5 digits, '@' a name).
constexpr size_t kThoughtLineCap = kMaxVoicedText + (kMaxTaughtText - 1) + 1;
static_assert(kMaxTaughtText >= kMaxNameText && kMaxTaughtText >= 5);
// kSharedOnly is no species: the shared lines alone, as tests and
// previews want them whatever lines the species files hold.
inline constexpr SpeciesId kSharedOnly{0xFE};
size_t thoughtLine(ThoughtId, uint16_t num, VoiceId, uint8_t pick, char* out, SpeciesId = kSharedOnly,
                   const Words& = {});
// The phone's text as the marquee draws it: uppercased, kept to the font,
// trimmed of spaces at either end, at most `cap` characters. Returns the
// length; 0 when nothing is left.
size_t toFont(std::string_view in, char* out, size_t cap);

// How long a line holds: one pass of the marquee, which moves 3 px a tick
// over the dish's chord (about 200 px) plus 12 px a character.
constexpr uint16_t kThoughtLeadTicks = 70, kThoughtTicksPerChar = 4;
constexpr uint16_t passTicks(size_t len) { return uint16_t(kThoughtLeadTicks + len * kThoughtTicksPerChar); }

// What the phone has him say (TWIST say): a voiced line with room for a 5-digit number.
constexpr size_t kMaxSaidText = kMaxVoicedText + 4;
static_assert(kMaxSaidText < kThoughtLineCap);

// ---- choosing ---------------------------------------------------------------------
struct ThoughtPick { ThoughtId id; uint16_t num; };

// The prophecy a shake brings, by the oracle gene's topic weights; rows still
// cooling down (readyAt, per row, nullable) are skipped. Nullopt if none holds.
std::optional<ThoughtPick> chooseProphecy(const Observed&, const uint32_t* readyAt, Rng&);
// The ball's bare answer to a shake: yes, no or not yet (45 / 30 / 25), in
// the species' own words when his file gives that kind (SPECIES_VERDICTS),
// else the ball's plain ones.
const char* drawVerdict(Rng&, SpeciesId);

class Thinker {
 public:
  // Once per tick while a creature lives, wall time or not. `shook`: a
  // shake landed this tick. A new life (or a reboot) restarts it: fresh
  // cooldowns, heirlooms re-read, a quiet gap before the first thought.
  void step(const Creature&, const Habitat&, const PetClock&, const Lineage&, uint32_t tick, bool shook,
            const FromPhone& = {});
  // Sim scripts and tests: that line starts now, as if its condition held. A
  // taught-line row with nothing taught stays silent.
  void force(ThoughtId, const Creature&, const Habitat&, const PetClock&, const Lineage&, uint32_t tick,
             const FromPhone& = {});
  // The phone's line (already in the font, at most kMaxSaidText): it starts
  // now over anything showing, and no thought or prophecy cuts it short.
  void say(std::string_view line, const Creature&, const Lineage&, uint32_t tick);
  // Fills the Appearance's line. While he prophesies he wears the foresee
  // face and glow, and the shake's hop does not show; while he says the
  // phone's line he wears the croak face.
  void show(Appearance&, uint32_t tick) const;
  // Foretellings told (a shake's trance ended in its answer) since boot: the life stats count them (life_tally.h).
  uint32_t foretold() const { return foretold_; }

  static constexpr uint32_t kMissPhoneTicks = 12 * kTicksPerHour;      // powered time without a phone before he asks for one
  static constexpr uint32_t kTranceTicks = 12;                         // a shake's trance: the swirl before the answer lands
  static constexpr uint32_t kWarmUpTicks = 3 * kTicksPerMinute;        // a hatchling's first minutes are wordless
  static constexpr uint32_t kQuietTicks = kTicksPerMinute;             // after a line, at least this ...
  static constexpr uint32_t kQuietJitterTicks = 2 * kTicksPerMinute;   // ... plus up to this, drawn

 private:
  enum class Spoken : uint8_t { thought, prophecy, said };   // his own line, a shake's foretelling, the phone's
  struct Shown { Spoken kind; ThoughtId id; uint32_t since; uint16_t ticks; char line[kThoughtLineCap]; };
  // A foretelling on its way: the pick is made at the shake, the line lands
  // when the trance ends. `verdict` is the Magic 8-Ball's own text when the
  // answer is a verdict; null when the pick's omen line is the answer.
  struct Trance { uint32_t until; ThoughtPick pick; const char* verdict; };
  void meet(const Creature&, const Lineage&, uint32_t tick);
  void start(const Creature&, const Lineage&, const FromPhone&, ThoughtPick, uint32_t tick, Rng&, const char* verdict = nullptr);

  uint32_t life_ = 0, lifeGeneration_ = 0xFFFFFFFFu;   // whose lines these are
  Heirlooms heirlooms_;
  uint32_t readyAt_[kThoughtCount]{};                   // per row: its cooldown ends
  uint32_t quietUntil_ = 0;                             // only urgent thoughts before this
  uint32_t fortuneDay_ = 0xFFFFFFFFu;                   // the pet day whose first foretelling is still owed
  std::optional<Shown> now_;
  std::optional<Trance> trance_;
  uint32_t foretold_ = 0;
};

}  // namespace blorb
