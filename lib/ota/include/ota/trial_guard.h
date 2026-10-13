#pragma once
// The protocol's link with the lineage-writing twists held back while this
// image is on trial. Its storage refuses every write then (held_storage.h),
// so a rename or a gene edit would be answered OK and quietly lost; a page is
// told to wait instead (docs/ota.md "The trial").
#include <functional>
#include <optional>
#include <string_view>
#include "blorb/protocol.h"
#include "blorb/seams.h"

namespace ota {

class TrialGuard : public blorb::Link {
 public:
  TrialGuard(blorb::Link& inner, std::function<bool()> onTrial) : inner_(inner), onTrial_(std::move(onTrial)) {}

  bool connected() override { return inner_.connected(); }
  void writeLine(std::string_view s) override { inner_.writeLine(s); }
  std::optional<std::string_view> readLine() override {
    while (std::optional<std::string_view> line = inner_.readLine()) {
      std::optional<uint32_t> id;
      if (!onTrial_() || !(id = lineageTwist(*line))) return line;
      blorb::Reply(inner_, *id).err(409, "TRIAL");
    }
    return std::nullopt;
  }

 private:
  // The id of `#<id> TWIST <kind>` when that kind writes the lineage. In
  // trial_guard.cpp: Arduino.h's word() macro would rewrite it in a header.
  static std::optional<uint32_t> lineageTwist(std::string_view line);

  blorb::Link& inner_;
  std::function<bool()> onTrial_;
};

}  // namespace ota
