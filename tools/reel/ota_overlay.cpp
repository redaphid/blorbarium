// The update screen over a filmed pet, for the reel. The sim builds without
// OTA (it lives behind the board's #else in main.cpp), so this links the
// firmware's own ota/screen.cpp and draws its View over each recorded frame,
// as main.cpp does over the live canvas. Nothing here decides what the screen
// says; it only picks which phase a frame is in.
//
//   ota_overlay <in_dir> <out_dir> <fps> <start_ms> <ask_ms> <receive_ms> <ready_ms>
//
// From start_ms: asking for ask_ms (the ring drains as the 30 s question would),
// then receiving 0..100% over receive_ms, then restarting for ready_ms.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ota/screen.h"

static bool readBmp(const std::string& path, paint::Canvas240& cv, std::vector<uint8_t>& head) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  head.resize(54);
  if (fread(head.data(), 1, 54, f) != 54) { fclose(f); return false; }
  uint8_t row[240 * 3];
  for (int y = 239; y >= 0; y--) {
    if (fread(row, 1, sizeof row, f) != sizeof row) { fclose(f); return false; }
    for (int x = 0; x < 240; x++) {
      const uint8_t b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
      cv.px[y * 240 + x] = uint16_t((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
    }
  }
  fclose(f);
  return true;
}

static void writeBmp(const std::string& path, const paint::Canvas240& cv, const std::vector<uint8_t>& head) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { fprintf(stderr, "ota_overlay: cannot write %s\n", path.c_str()); exit(1); }
  fwrite(head.data(), 1, head.size(), f);
  uint8_t row[240 * 3];
  for (int y = 239; y >= 0; y--) {
    for (int x = 0; x < 240; x++) {
      const uint16_t p = cv.px[y * 240 + x];
      const uint8_t r = uint8_t((p >> 11) << 3 | (p >> 13)), g = uint8_t(((p >> 5) & 63) << 2 | ((p >> 9) & 3)),
                    b = uint8_t((p & 31) << 3 | ((p >> 2) & 7));
      row[x * 3] = b, row[x * 3 + 1] = g, row[x * 3 + 2] = r;
    }
    fwrite(row, 1, sizeof row, f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc != 8) { fprintf(stderr, "usage: ota_overlay <in> <out> <fps> <start_ms> <ask_ms> <receive_ms> <ready_ms>\n"); return 2; }
  const std::string in = argv[1], out = argv[2];
  const uint32_t frameMs = 1000 / uint32_t(atoi(argv[3]));
  const uint32_t start = uint32_t(atoi(argv[4]));
  const uint32_t ask = uint32_t(atoi(argv[5])), receive = uint32_t(atoi(argv[6])), ready = uint32_t(atoi(argv[7]));
  std::vector<std::string> names;
  if (DIR* d = opendir(in.c_str())) {
    while (dirent* e = readdir(d))
      if (strstr(e->d_name, ".bmp")) names.push_back(e->d_name);
    closedir(d);
  }
  std::sort(names.begin(), names.end());
  constexpr uint32_t kSize = 993088;   // the 0.4.0 image's size, so the percentages are a real transfer's
  paint::Canvas240 cv;
  std::vector<uint8_t> head;
  for (size_t i = 0; i < names.size(); i++) {
    if (!readBmp(in + "/" + names[i], cv, head)) { fprintf(stderr, "ota_overlay: bad %s\n", names[i].c_str()); return 1; }
    const uint32_t at = uint32_t(i) * frameMs;
    if (at >= start && at - start < ask + receive + ready) {
      const uint32_t t = at - start;
      if (t < ask) ota::drawUpdate(cv, {ota::View::Phase::Asking, 0, kSize, ota::Session::kConfirmMs - t});
      else if (t < ask + receive)
        ota::drawUpdate(cv, {ota::View::Phase::Receiving, uint32_t(uint64_t(kSize) * (t - ask) / receive), kSize, 0});
      else ota::drawUpdate(cv, {ota::View::Phase::Ready, kSize, kSize, 0});
    }
    writeBmp(out + "/" + names[i], cv, head);
  }
  return 0;
}
