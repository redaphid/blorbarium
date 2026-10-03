#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "blorb/notify_queue.h"

using blorb::NotifyQueue;

namespace {

// A GENOME reply as the protocol frames it: 160-character base64 lines, then OK.
std::vector<std::string> genomeReply(size_t bytes) {
  std::vector<std::string> lines;
  for (size_t at = 0; at < bytes; at += 120) {
    std::string b64;
    for (size_t i = 0; i < 160; ++i) b64 += char('A' + (at + i * 7) % 26);
    lines.push_back("#4 + " + std::to_string(at) + " " + b64);
  }
  lines.push_back("#4 OK " + std::to_string(bytes) + " 1a2b3c4d");
  return lines;
}

// Refuses every `every`th send, and every send from `burstFrom` for `burstLen` tries.
struct FlakySink {
  size_t every, burstFrom, burstLen;
  size_t tries = 0;
  std::vector<std::string> got;
  bool operator()(const uint8_t* p, size_t n) {
    ++tries;
    if (tries % every == 0) return false;
    if (tries >= burstFrom && tries < burstFrom + burstLen) return false;
    got.emplace_back(reinterpret_cast<const char*>(p), n);
    return true;
  }
};

struct Case {
  size_t mtu, every;
};

class Lossless : public ::testing::TestWithParam<Case> {};

TEST_P(Lossless, EveryByteArrivesInOrderAndEachNewlineAlone) {
  const size_t payload = GetParam().mtu - 3;
  NotifyQueue<2048> q;
  FlakySink sink{GetParam().every, 5, 40};
  std::string sent;
  size_t polls = 0;
  for (const std::string& line : genomeReply(1146)) {
    while (!q.push(line)) {   // full: drain as the link's writeLine does
      q.drain(payload, sink);
      ASSERT_LT(++polls, 100000u);
    }
    sent += line + "\n";
  }
  ASSERT_GT(sent.size(), 1500u);
  while (!q.drain(payload, sink)) ASSERT_LT(++polls, 100000u);

  std::string received;
  for (const std::string& n : sink.got) {
    received += n;
    ASSERT_LE(n.size(), payload);
    if (n.find('\n') != std::string::npos) EXPECT_EQ(n, "\n") << "a newline shares its notification";
  }
  EXPECT_EQ(received, sent);
  EXPECT_EQ(q.chunks(), sink.got.size());
  EXPECT_EQ(q.retries(), sink.tries - sink.got.size());
  EXPECT_GE(q.retries(), 40u);
  EXPECT_EQ(q.burst(), sent.size());
}

INSTANTIATE_TEST_SUITE_P(Mtus, Lossless,
                         ::testing::Values(Case{23, 3}, Case{23, 7}, Case{247, 2}, Case{247, 5}));

TEST(NotifyQueue, ASmallQueueBlocksAndDrainsWithoutLoss) {
  NotifyQueue<256> q;
  FlakySink sink{4, 10, 20};
  std::string sent;
  for (const std::string& line : genomeReply(1146)) {
    while (!q.push(line)) q.drain(20, sink);
    sent += line + "\n";
  }
  while (!q.drain(20, sink)) {}
  std::string received;
  for (const std::string& n : sink.got) received += n;
  EXPECT_EQ(received, sent);
}

TEST(NotifyQueue, ClearDropsWhatADisconnectLeftBehind) {
  NotifyQueue<512> q;
  ASSERT_TRUE(q.push("#1 OK hello"));
  q.clear();
  EXPECT_TRUE(q.empty());
  std::vector<std::string> got;
  EXPECT_TRUE(q.drain(20, [&](const uint8_t* p, size_t n) {
    got.emplace_back(reinterpret_cast<const char*>(p), n);
    return true;
  }));
  EXPECT_TRUE(got.empty());
}

TEST(NotifyQueue, AnEmptyLineIsOneNewline) {
  NotifyQueue<64> q;
  ASSERT_TRUE(q.push(""));
  std::vector<std::string> got;
  q.drain(20, [&](const uint8_t* p, size_t n) {
    got.emplace_back(reinterpret_cast<const char*>(p), n);
    return true;
  });
  EXPECT_EQ(got, std::vector<std::string>{"\n"});
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
