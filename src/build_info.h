// What this image is, built in build_info.cpp: the one file the commit stamp
// reaches (tools/fw_stamp.py), so a new commit recompiles it and nothing else.
#pragma once

#include <array>
#include <cstdint>

// What tools/ota_release.py reads back out of firmware.bin, so a release can
// only be signed as the version, board and save format the image really is.
// reportBoot() prints from it, which keeps the linker from dropping it.
struct ReleaseStamp {
  std::array<char, 12> magic;
  std::array<char, 16> fw, board;
  uint16_t fmt;
  std::array<char, 24> commit;   // git describe --always --dirty, as fw_stamp.py saw it
};
extern const ReleaseStamp kReleaseStamp;
extern const char kBuildFw[];   // the same describe, whole
