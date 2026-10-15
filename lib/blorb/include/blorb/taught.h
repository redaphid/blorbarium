#pragma once
// Lines the owner taught him from the phone (TWIST teach). He says one now
// and then as a thought (the parrot row of thoughts.def), so they stay after
// the phone has gone. They live in their own file beside the keepsake: kept
// across reboots and lives, never in the save, its hash or the lineage. A
// board that cannot write (an image on trial, a sealed pet) keeps what it had.
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "blorb/seams.h"
#include "blorb/thoughts.h"

namespace blorb {

struct Taught {
  static constexpr const char* kFile = "taught.lines";
  static constexpr uint8_t kMax = 4;
  char at[kMax][kMaxTaughtText + 1]{};   // oldest first
  uint8_t count = 0;

  // The file's lines; none when it is missing, torn or not ours.
  static Taught load(Storage&);
  bool save(Storage&) const;
  // `line` (already in the font, 1 to kMaxTaughtText) as the newest. One
  // already held moves to newest; the oldest goes when all kMax are held.
  Taught with(std::string_view line) const;
  std::string_view line(uint8_t i) const { return at[i]; }
};

}  // namespace blorb
