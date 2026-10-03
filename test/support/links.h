#pragma once
// Link doubles: NullLink is a phone that never connects; ScriptedLink is one
// that is connected and replays queued lines, recording every reply.
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "blorb/seams.h"

namespace blorbtest {

struct NullLink : blorb::Link {
  bool connected() override { return false; }
  std::optional<std::string_view> readLine() override { return std::nullopt; }
  void writeLine(std::string_view) override {}
};

struct ScriptedLink : blorb::Link {
  std::deque<std::string> inbound;
  std::vector<std::string> sent;
  std::string current;   // keeps the line readLine() handed out alive until the next call

  bool connected() override { return true; }
  std::optional<std::string_view> readLine() override {
    if (inbound.empty()) return std::nullopt;
    current = std::move(inbound.front());
    inbound.pop_front();
    return std::string_view(current);
  }
  void writeLine(std::string_view line) override { sent.emplace_back(line); }
};

}  // namespace blorbtest
