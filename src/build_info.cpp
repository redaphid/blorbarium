// The only file built with BLORB_FW (tools/fw_stamp.py): see build_info.h.
#include "build_info.h"

#include <cstddef>

#include "blorb/keepsake.h"
#include "blorb/protocol.h"
#include "hw/board_name.h"

template <size_t N>
constexpr std::array<char, N> stampField(const char* s) {
  std::array<char, N> a{};
  for (size_t i = 0; i + 1 < N && s[i]; ++i) a[i] = s[i];
  return a;
}

extern const char kBuildFw[] = BLORB_FW;
extern const ReleaseStamp kReleaseStamp = {stampField<12>("blorbstamp1"), stampField<16>(blorb::kFirmwareVersion),
                                           stampField<16>(BOARD_NAME), blorb::kFormatVersion,
                                           stampField<24>(BLORB_FW)};
