#include "ota/trial_guard.h"

namespace ota {

std::optional<uint32_t> TrialGuard::lineageTwist(std::string_view line) {
  blorb::Args args{line};
  const std::optional<std::string_view> tag = args.word(), verb = args.word(), kind = args.word();
  if (!tag || tag->size() < 2 || tag->size() > 7 || (*tag)[0] != '#' || verb != std::optional<std::string_view>("TWIST"))
    return std::nullopt;
  if (kind != std::optional<std::string_view>("rename") && kind != std::optional<std::string_view>("gene_edit") &&
      kind != std::optional<std::string_view>("pick"))
    return std::nullopt;
  uint32_t id = 0;
  for (char c : tag->substr(1)) {
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (v < 0) return std::nullopt;
    id = id << 4 | uint32_t(v);
  }
  return id;
}

}  // namespace ota
