#pragma once
// A host fake for the RTC time source: a battery clock that kept counting
// while the dish was unpowered. It reads the wall time it held at boot plus
// the powered time since, so it runs on while the dish runs. The phone source
// is the Dish's own PhoneTime, driven through TIME or dish.phoneTime().set().
#include <cstdint>
#include <optional>
#include "blorb/seams.h"

namespace blorbtest {

struct FakeRtc : blorb::TimeSource {
  std::optional<uint32_t> atBoot;      // nullopt: the battery is flat
  const uint32_t* poweredMs = nullptr; // the caller's millisecond clock since boot
  std::optional<uint32_t> unixSeconds() override {
    if (!atBoot) return std::nullopt;
    return *atBoot + (poweredMs ? *poweredMs / 1000 : 0);
  }
};

}  // namespace blorbtest
