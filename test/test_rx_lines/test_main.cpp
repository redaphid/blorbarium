#include <gtest/gtest.h>
#include <string>
#include "blorb/rx_lines.h"

using blorb::RxLines;

namespace {

template <size_t N>
void push(RxLines<N>& rx, const std::string& s) {
  rx.push(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

template <size_t N>
std::string next(RxLines<N>& rx, uint32_t* connection = nullptr) {
  const auto l = rx.line();
  if (!l) return "<none>";
  if (connection) *connection = l->connection;
  return std::string(l->text);
}

}  // namespace

TEST(RxLines, LinesCarryTheConnectionTheirBytesCameOn) {
  RxLines<64> rx;
  rx.connected(1);
  push(rx, "#1 HELLO\r\n#2 ST");
  uint32_t c = 0;
  EXPECT_EQ(next(rx, &c), "#1 HELLO");
  EXPECT_EQ(c, 1u);
  push(rx, "ATE\n");
  EXPECT_EQ(next(rx, &c), "#2 STATE");
  EXPECT_EQ(c, 1u);
  EXPECT_EQ(next(rx), "<none>");
}

TEST(RxLines, ANewConnectionDropsTheLastOnesUnreadBytesAndHalfLine) {
  RxLines<64> rx;
  rx.connected(1);
  push(rx, "#1 OTA ABORT tok\n#2 OTA AB");   // phone 1 is cut off mid-line, its whole line never read
  rx.connected(2);
  push(rx, "ORT\n#3 HELLO\n");
  uint32_t c = 0;
  EXPECT_EQ(next(rx, &c), "ORT") << "phone 2's bytes are never glued to phone 1's half line";
  EXPECT_EQ(c, 2u);
  EXPECT_EQ(next(rx, &c), "#3 HELLO");
  EXPECT_EQ(c, 2u);
}

TEST(RxLines, AHalfLineReadBeforeAReconnectIsDropped) {
  RxLines<64> rx;
  rx.connected(1);
  push(rx, "#1 OTA REB");
  EXPECT_EQ(next(rx), "<none>");   // the loop has taken the half line in
  rx.connected(2);
  push(rx, "#2 HELLO\n");
  uint32_t c = 0;
  EXPECT_EQ(next(rx, &c), "#2 HELLO");
  EXPECT_EQ(c, 2u);
}

TEST(RxLines, AFullRingDropsTheRest) {
  RxLines<8> rx;
  rx.connected(1);
  push(rx, "abcdefghij\n");
  EXPECT_EQ(next(rx), "<none>") << "the newline never fitted";
  push(rx, "\n");
  EXPECT_EQ(next(rx), "abcdefg");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
