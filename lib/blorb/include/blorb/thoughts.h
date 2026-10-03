#pragma once
// Thoughts and prophecies: grungo says a line now and then, and the renderer
// scrolls it once across the dish (defs/thoughts.def). The Thinker picks
// which, from what anyone could see of him (drives, the action or reflex,
// stage, sickness, foresee, what his brain learned, the heirlooms in his
// genome, the pet clock) plus a draw seeded by his genome and the tick. Every
// shake brings a prophecy instead of a hop. His oracle weights its topics and
// picks the voice (voices.def) every line is said in.
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

// An heirloom instinct in his genome, and the generation whose brain learned
// it (kUnknownGeneration when the lineage no longer says: founder-carried or compacted).
struct Heirloom { LocusId cue; ActionId action; DriveId drive; Fx level; uint16_t learnedBy; };
constexpr uint16_t kUnknownGeneration = 0xFFFF;
struct Heirlooms {
  Heirloom at[8]{};
  uint8_t count = 0;
};
Heirlooms heirloomsOf(const Genome&, const Lineage&);

// What a line's condition may look at.
struct Observed {
  const Creature& c;
  const Habitat& habitat;
  const PetClock& clock;
  const Heirlooms& heirlooms;
  uint32_t tick;
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
  constexpr bool prophecy() const { return topic != topic::none; }
};
inline constexpr ThoughtInfo THOUGHTS[] = {
#define BLORB_THOUGHT(id, name, top, pred, priority, cooldownS, text) \
  {ThoughtId{id}, #name, topic::top, when::pred, priority, cooldownS, text},
#include "blorb/defs/thoughts.def"
#undef BLORB_THOUGHT
};
inline constexpr size_t kThoughtCount = countOf(THOUGHTS);
const ThoughtInfo* thoughtInfo(ThoughtId);

struct VoicedLine { ThoughtId thought; VoiceId voice; const char* text; };
inline constexpr VoicedLine VOICED_LINES[] = {
#define BLORB_LINE(t, v, text) {thought::t, voice::v, text},
#include "blorb/defs/thought_lines.def"
#undef BLORB_LINE
};

// ---- the text ---------------------------------------------------------------------
constexpr size_t kMaxThoughtText = 30;
constexpr size_t kMaxFrameText = 22;          // a voice's prefix and suffix together
constexpr size_t kMaxVoicedText = kMaxThoughtText + kMaxFrameText;   // a row of thought_lines.def
constexpr uint8_t kUrgentThought = 7;

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
constexpr bool thoughtsWritable() {
  for (const ThoughtInfo& t : THOUGHTS) {
    size_t marks = 0;
    for (const char* s = t.text; *s; ++s) {
      if (!inFont(*s) && *s != '#') return false;
      marks += *s == '#';
    }
    const size_t n = textLen(t.text);
    if (n == 0 || n > kMaxThoughtText || marks > 1) return false;
  }
  for (const VoicedLine& l : VOICED_LINES) {
    size_t marks = 0;
    for (const char* s = l.text; *s; ++s) {
      if (!inFont(*s) && *s != '#') return false;
      marks += *s == '#';
    }
    const size_t n = textLen(l.text);
    if (n == 0 || n > kMaxVoicedText || marks > 1) return false;
  }
  for (const FrameInfo& f : FRAMES) {
    for (const char* s = f.prefix; *s; ++s) if (!inFont(*s)) return false;
    for (const char* s = f.suffix; *s; ++s) if (!inFont(*s)) return false;
    if (textLen(f.prefix) + textLen(f.suffix) > kMaxFrameText) return false;
  }
  return true;
}
constexpr bool everyVoiceFramed() {
  for (const VoiceInfo& v : VOICES) {
    bool framed = false;
    for (const FrameInfo& f : FRAMES) framed = framed || f.voice == v.id;
    if (!framed) return false;
  }
  return true;
}
static_assert(idsUnique(THOUGHTS), "thoughts.def has a duplicate id");
static_assert(kThoughtCount <= 255, "the Thinker indexes rows by a byte");
static_assert(thoughtsWritable(), "a line or a voice frame is too long, has two '#' or a character the font lacks");
static_assert(everyVoiceFramed(), "every voice needs at least one BLORB_FRAME");

// The line as said, with its '#' replaced by `num`: one of the voice's own
// lines for the row (thought_lines.def) if it has any, else the row's text,
// cut to the first sentence if the voice is terse, inside one of the voice's
// frames. `pick` chooses among them, modulo their count. Returns the length.
constexpr size_t kThoughtLineCap = kMaxVoicedText + 4 + 1;   // '#' can grow to 5 digits
size_t thoughtLine(ThoughtId, uint16_t num, VoiceId, uint8_t pick, char* out);

// How long a line holds: one pass of the marquee, which moves 3 px a tick
// over the dish's chord (about 200 px) plus 12 px a character.
constexpr uint16_t kThoughtLeadTicks = 70, kThoughtTicksPerChar = 4;
constexpr uint16_t passTicks(size_t len) { return uint16_t(kThoughtLeadTicks + len * kThoughtTicksPerChar); }

// What the phone has him say (TWIST say): the longest line the strip holds.
constexpr size_t kMaxSaidText = kThoughtLineCap - 1;

// ---- choosing ---------------------------------------------------------------------
struct ThoughtPick { ThoughtId id; uint16_t num; };

// The prophecy a shake brings, by the oracle gene's topic weights; rows still
// cooling down (readyAt, per row, nullable) are skipped. Nullopt if none holds.
std::optional<ThoughtPick> chooseProphecy(const Observed&, const uint32_t* readyAt, Rng&);

class Thinker {
 public:
  // Once per tick while a creature lives and wall time is known. `shook`: a
  // shake landed this tick. A new life (or a reboot) restarts it: fresh
  // cooldowns, heirlooms re-read, a quiet gap before the first thought.
  void step(const Creature&, const Habitat&, const PetClock&, const Lineage&, uint32_t tick, bool shook);
  // Sim scripts and tests: that line starts now, as if its condition held.
  void force(ThoughtId, const Creature&, const Habitat&, const PetClock&, const Lineage&, uint32_t tick);
  // The phone's line (already in the font, at most kMaxSaidText): it starts
  // now over anything showing, and no thought or prophecy cuts it short.
  void say(std::string_view line, const Creature&, const Lineage&, uint32_t tick);
  // Fills the Appearance's line. While he prophesies he wears the foresee
  // face and glow, and the shake's hop does not show; while he says the
  // phone's line he wears the croak face. Before wall time only the phone's
  // line shows: the time-unknown marquee outranks his own.
  void show(Appearance&, uint32_t tick, bool wallKnown) const;

  static constexpr uint32_t kWarmUpTicks = 3 * kTicksPerMinute;        // a hatchling's first minutes are wordless
  static constexpr uint32_t kQuietTicks = kTicksPerMinute;             // after a line, at least this ...
  static constexpr uint32_t kQuietJitterTicks = 2 * kTicksPerMinute;   // ... plus up to this, drawn

 private:
  enum class Spoken : uint8_t { thought, prophecy, said };   // his own line, a shake's foretelling, the phone's
  struct Shown { Spoken kind; ThoughtId id; uint32_t since; uint16_t ticks; char line[kThoughtLineCap]; };
  void meet(const Creature&, const Lineage&, uint32_t tick);
  void start(const Creature&, ThoughtPick, uint32_t tick, Rng&);

  uint32_t life_ = 0, lifeGeneration_ = 0xFFFFFFFFu;   // whose lines these are
  Heirlooms heirlooms_;
  uint32_t readyAt_[kThoughtCount]{};                   // per row: its cooldown ends
  uint32_t quietUntil_ = 0;                             // only urgent thoughts before this
  std::optional<Shown> now_;
};

}  // namespace blorb
