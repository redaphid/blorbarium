#pragma once

// The site's lines for this pet (#59): the pack a Wi-Fi window brought back from
// POST /api/pet-lines, in its text form (web/src/pack.js):
//
//   blorbpack1 <genome hash, first 16 hex> <pack version> <line count>
//   <thought name> <TAB> <LINE>
//   ...
//
// parsed in place. While one is set, a thought it has lines for says only those,
// as written; every other thought keeps the built-in lines.

#include <cstddef>
#include <cstdint>

#include "blorb/ids.h"

namespace blorb {

class PackLines {
 public:
  static constexpr const char* kMagic = "blorbpack1";
  struct Entry { ThoughtId thought; const char* text; };

  // `text` (`len` bytes and room for a terminator after them, writable, kept
  // alive by the caller) is cut in place into
  // `entries`: rows naming no thought this firmware has, and lines too long to
  // say, are skipped. False when it is not a pack, or has no line this badge can say.
  bool parse(char* text, size_t len, Entry* entries, size_t cap);

  // The first 16 hex digits of the genome's SHA-256 the pack was written for.
  const char* genome() const { return genome_; }
  uint32_t version() const { return version_; }
  size_t size() const { return n_; }
  uint8_t count(ThoughtId id) const;
  const char* line(ThoughtId id, uint8_t i) const;

 private:
  Entry* e_ = nullptr;
  size_t n_ = 0;
  uint32_t version_ = 0;
  char genome_[17] = "";
};

// The pack thoughtLine() says from; nullptr (the default) for the built-in lines only.
void setPackLines(const PackLines*);
const PackLines* packLines();

}  // namespace blorb
