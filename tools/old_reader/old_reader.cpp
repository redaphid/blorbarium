// Built by tools/old_reader/old_reader.sh against an OLDER firmware's engine
// (0.4.0 by default), to prove what that firmware does with a newer keepsake.
//
//   old_reader same <blob>...           each loads and re-encodes to its own bytes
//   old_reader cut body|clutch <blob>   the blob loads as the same pet as the blob
//                                       with that chunk's trailing bytes cut off,
//                                       and re-encodes to exactly those bytes
//
// The cut is done here on the bytes, from the chunk layout alone, so the proof
// leans on neither firmware's encoder for what "without the tail" means.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "blorb/keepsake.h"

using namespace blorb;

namespace {

constexpr size_t kHeaderLen = 18, kBodyFixedLen = 27, kFriendTailLen = 5;

std::vector<uint8_t> readFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::vector<uint8_t> cutTail(const std::vector<uint8_t>& blob, uint8_t tag, bool body) {
  for (size_t at = kHeaderLen; at + 4 <= blob.size();) {
    const size_t n = size_t(blob[at + 2]) | size_t(blob[at + 3]) << 8;
    if (blob[at] == tag) {
      const size_t keep = body ? kBodyFixedLen : n - kFriendTailLen;
      std::vector<uint8_t> out(blob.begin(), blob.begin() + long(at + 4 + keep));
      out.insert(out.end(), blob.begin() + long(at + 4 + n), blob.end());
      out[at + 2] = uint8_t(keep), out[at + 3] = uint8_t(keep >> 8);
      const uint32_t payload = uint32_t(out.size() - kHeaderLen);
      for (int i = 0; i < 4; ++i) out[10 + i] = uint8_t(payload >> (8 * i)), out[14 + i] = 0;
      const uint32_t crc = crc32(out.data(), out.size());
      for (int i = 0; i < 4; ++i) out[14 + i] = uint8_t(crc >> (8 * i));
      return out;
    }
    at += 4 + n;
  }
  return {};
}

std::string describe(const Snapshot& s) {
  char line[256];
  if (const Creature* c = std::get_if<Creature>(&s.occupant))
    std::snprintf(line, sizeof line, "creature gen %u stage %u genome %08x creature %08x", c->generation(),
                  unsigned(c->stage()), c->genome().hash(), c->hash());
  else if (const Clutch* k = std::get_if<Clutch>(&s.occupant))
    std::snprintf(line, sizeof line, "clutch gen %u count %u cursor %u parent %08x seeds %016llx %016llx %016llx",
                  k->generation, k->count, k->cursor, k->parent.hash(), (unsigned long long)k->seeds[0],
                  (unsigned long long)k->seeds[1], (unsigned long long)k->seeds[2]);
  else
    std::snprintf(line, sizeof line, "egg gen %u genome %08x", std::get<Egg>(s.occupant).generation(),
                  std::get<Egg>(s.occupant).genome().hash());
  return std::string(line) + " snapshot " + [&] {
    char h[16];
    std::snprintf(h, sizeof h, "%08x", s.hash());
    return std::string(h);
  }();
}

bool same(const char* path) {
  const std::vector<uint8_t> blob = readFile(path);
  static Snapshot s;
  if (!Keepsake::decode(blob.data(), blob.size(), s)) return std::printf("FAIL  %s does not load\n", path), false;
  const bool ok = Keepsake::encode(s) == blob;
  std::printf("%s  %s: %s, re-encodes to its own %zu bytes\n", ok ? "OK  " : "FAIL", path, describe(s).c_str(),
              blob.size());
  return ok;
}

bool cut(const char* what, const char* path) {
  const bool body = std::strcmp(what, "body") == 0;
  const std::vector<uint8_t> blob = readFile(path), plain = cutTail(blob, body ? 6 : 2, body);
  static Snapshot withTail, without;
  if (plain.empty()) return std::printf("FAIL  %s has no chunk to cut\n", path), false;
  if (!Keepsake::decode(blob.data(), blob.size(), withTail)) return std::printf("FAIL  %s does not load\n", path), false;
  if (!Keepsake::decode(plain.data(), plain.size(), without)) return std::printf("FAIL  %s cut does not load\n", path), false;
  const std::string a = describe(withTail), b = describe(without);
  const bool ok = a == b && Keepsake::encode(withTail) == plain;
  std::printf("%s  %s (%zu bytes) loads as: %s\n      cut to %zu bytes it loads as: %s\n      re-encodes to the cut bytes: %s\n",
              ok ? "OK  " : "FAIL", path, blob.size(), a.c_str(), plain.size(), b.c_str(),
              Keepsake::encode(withTail) == plain ? "yes" : "NO");
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "same") == 0) {
    bool ok = true;
    for (int i = 2; i < argc; ++i) ok = same(argv[i]) && ok;
    return ok ? 0 : 1;
  }
  if (argc == 4 && std::strcmp(argv[1], "cut") == 0) return cut(argv[2], argv[3]) ? 0 : 1;
  std::fprintf(stderr, "usage: old_reader same <blob>... | old_reader cut body|clutch <blob>\n");
  return 2;
}
