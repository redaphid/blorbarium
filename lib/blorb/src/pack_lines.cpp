#include "blorb/pack_lines.h"

#include <cstdlib>
#include <cstring>

#include "blorb/thoughts.h"

namespace blorb {

namespace {
const PackLines* active = nullptr;

// The next line of `s` (up to `end`), cut at its newline; nullptr past the end.
char* nextLine(char*& s, char* end) {
  if (s >= end) return nullptr;
  char* line = s;
  char* nl = static_cast<char*>(std::memchr(s, '\n', size_t(end - s)));
  if (nl) *nl = 0, s = nl + 1;
  else *end = 0, s = end;
  const size_t n = std::strlen(line);
  if (n && line[n - 1] == '\r') line[n - 1] = 0;
  return line;
}

const ThoughtInfo* thoughtNamed(const char* name) {
  for (const ThoughtInfo& t : THOUGHTS)
    if (std::strcmp(t.name, name) == 0) return &t;
  return nullptr;
}
}  // namespace

bool PackLines::parse(char* text, size_t len, Entry* entries, size_t cap) {
  e_ = entries;
  n_ = 0;
  version_ = 0;
  genome_[0] = 0;
  if (!text || !entries || !len) return false;
  char* s = text;
  char* end = text + len;
  char* head = nextLine(s, end);
  const size_t magic = std::strlen(kMagic);
  if (!head || std::strncmp(head, kMagic, magic) != 0 || head[magic] != ' ') return false;
  char* p = head + magic + 1;
  char* sp = std::strchr(p, ' ');
  if (!sp || sp - p != 16) return false;
  std::memcpy(genome_, p, 16);
  genome_[16] = 0;
  version_ = uint32_t(std::strtoul(sp + 1, nullptr, 10));
  while (char* row = nextLine(s, end)) {
    char* tab = std::strchr(row, '\t');
    if (!tab || n_ >= cap) continue;
    *tab = 0;
    const char* said = tab + 1;
    const ThoughtInfo* t = thoughtNamed(row);
    if (!t || !*said || std::strlen(said) > kMaxThoughtText) continue;
    e_[n_++] = Entry{t->id, said};
  }
  return n_ > 0;
}

uint8_t PackLines::count(ThoughtId id) const {
  uint8_t n = 0;
  for (size_t i = 0; i < n_; ++i) n += e_[i].thought == id && n < UINT8_MAX;
  return n;
}

const char* PackLines::line(ThoughtId id, uint8_t want) const {
  for (size_t i = 0; i < n_; ++i)
    if (e_[i].thought == id && want-- == 0) return e_[i].text;
  return nullptr;
}

void setPackLines(const PackLines* p) { active = p; }
const PackLines* packLines() { return active; }

}  // namespace blorb
