#include "ota/trial_guard.h"

namespace ota {

// The id as the protocol's takeId() reads it: no '#' is id 0, and a line
// whose '#' id is malformed is the protocol's to refuse.
std::optional<uint32_t> TrialGuard::lineageTwist(std::string_view line) {
  uint32_t id = 0;
  if (!line.empty() && line[0] == '#') {
    size_t i = 1;
    for (; i < line.size() && i <= 7; ++i) {
      const char c = line[i];
      const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (v < 0) break;
      id = id << 4 | uint32_t(v);
    }
    if (i == 1 || i > 7 || (i < line.size() && line[i] != ' ')) return std::nullopt;
    line.remove_prefix(i);
  }
  blorb::Args args{line};
  const std::optional<std::string_view> verb = args.word(), kind = args.word();
  if (verb != std::optional<std::string_view>("TWIST")) return std::nullopt;
  if (kind != std::optional<std::string_view>("rename") && kind != std::optional<std::string_view>("gene_edit") &&
      kind != std::optional<std::string_view>("pick"))
    return std::nullopt;
  return id;
}

}  // namespace ota
