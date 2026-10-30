// Which way up is home, per badge (owner, 2026-10-21: the three flipped
// badges must stay flipped after a normal release). Home was a build flag,
// BLORB_ORIENT_HOME (board_lcd128.h): 2 = USB-C up, 0 = USB-C down
// (env badge128_flip). A release is one image for every badge, so an update
// over the air would turn a flipped badge back. Now each badge keeps its own
// home in NVS (src/hw/home_store.h: the default `nvs` partition, namespace
// "badge", which neither an update nor a pet wipe touches), set once over the
// cable (`DEBUG home down|up`), and the build flag is only the default for a
// badge that never had one set.
//
// A badge built flipped writes "down" on its first boot when nothing is set,
// so the flipped badges migrate themselves the first time they run a build
// that has this: after that, any release keeps them flipped.
//
// Pure, so test/test_orient checks every case natively.
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>

namespace hw::home {

constexpr uint8_t kUp = 2;     // USB-C up: the kids' cases (rotation 2)
constexpr uint8_t kDown = 0;   // USB-C down (rotation 0)

constexpr bool valid(uint8_t rot) { return rot == kUp || rot == kDown; }
inline const char* name(uint8_t rot) { return rot == kDown ? "down" : "up"; }

// What a boot settles on, and whether it writes it.
struct AtBoot {
  uint8_t rot;
  bool write;        // store `rot` now: a flip build's first boot
  const char* why;   // for the boot log: "nvs", "migrated", "default"
};

// `stored`: what NVS holds (nothing when unset or unreadable). `built`: the
// build flag's home (BLORB_ORIENT_HOME).
inline AtBoot atBoot(std::optional<uint8_t> stored, uint8_t built) {
  if (stored && valid(*stored)) return {*stored, false, "nvs"};
  if (built == kDown) return {kDown, true, "migrated"};   // a flip build: keep it flipped from now on
  return {valid(built) ? built : kUp, false, "default"};
}

// `DEBUG home <up|down>` (also "2" and "0"). Nothing for anything else.
inline std::optional<uint8_t> parse(const char* s) {
  if (!s) return std::nullopt;
  if (!std::strcmp(s, "up") || !std::strcmp(s, "2")) return kUp;
  if (!std::strcmp(s, "down") || !std::strcmp(s, "0")) return kDown;
  return std::nullopt;
}

}  // namespace hw::home
