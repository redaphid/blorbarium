#include "wifiota/manifest.h"

#include <cstring>

namespace wifiota {
namespace {

// A cursor over the JSON text. Enough of JSON for the manifest: one object of
// strings without escapes, unsigned integers, true, false and null; nested
// values are skipped whole.
struct Reader {
  std::string_view s;
  size_t at = 0;

  void space() {
    while (at < s.size() && (s[at] == ' ' || s[at] == '\n' || s[at] == '\r' || s[at] == '\t')) ++at;
  }
  bool take(char c) {
    space();
    if (at >= s.size() || s[at] != c) return false;
    ++at;
    return true;
  }
  std::optional<std::string_view> string() {
    if (!take('"')) return std::nullopt;
    const size_t start = at;
    while (at < s.size() && s[at] != '"') {
      if (s[at] == '\\' || uint8_t(s[at]) < 0x20) return std::nullopt;   // no field the badge reads needs one
      ++at;
    }
    if (at >= s.size()) return std::nullopt;
    return s.substr(start, at++ - start);
  }
  std::optional<uint32_t> number() {
    space();
    const size_t start = at;
    uint64_t v = 0;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
      v = v * 10 + uint64_t(s[at++] - '0');
      if (v > 0xffffffffu) return std::nullopt;
    }
    if (at == start) return std::nullopt;
    return uint32_t(v);
  }
  bool word(const char* w) {
    space();
    const size_t n = std::strlen(w);
    if (s.substr(at, n) != w) return false;
    at += n;
    return true;
  }
  // Any value, read and dropped. Strings with escapes are fine here: nobody reads them.
  bool skip() {
    space();
    if (at >= s.size()) return false;
    const char c = s[at];
    if (c == '"') {
      for (++at; at < s.size() && s[at] != '"'; ++at)
        if (s[at] == '\\') ++at;
      return at++ < s.size();
    }
    if (c == '{' || c == '[') {
      const char close = c == '{' ? '}' : ']';
      ++at;
      if (take(close)) return true;
      do {
        if (c == '{' && (!string() || !take(':'))) return false;
        if (!skip()) return false;
      } while (take(','));
      return take(close);
    }
    if (c == 't') return word("true");
    if (c == 'f') return word("false");
    if (c == 'n') return word("null");
    if (c == '-') ++at;
    if (!number()) return false;
    if (at < s.size() && s[at] == '.') ++at, number();
    return true;
  }
};

bool fileNameOk(std::string_view f) {
  if (f.empty() || f.size() >= sizeof Manifest{}.file || f[0] == '.') return false;
  for (char c : f)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
      return false;
  return true;
}

constexpr uint8_t kWifiMark[4] = {'W', 'I', 'F', 'I'};

}  // namespace

std::optional<Manifest> parseManifest(std::string_view json) {
  Manifest m;
  Reader r{json};
  enum : uint8_t { kBoard = 1, kVersion = 2, kFlags = 4, kSize = 8, kSha = 16, kSig = 32, kFile = 64, kFmt = 128 };
  uint8_t got = 0;
  if (!r.take('{')) return std::nullopt;
  if (!r.take('}')) {
    do {
      const std::optional<std::string_view> key = r.string();
      if (!key || !r.take(':')) return std::nullopt;
      if (*key == "board" || *key == "version" || *key == "sha256" || *key == "sig" || *key == "image") {
        const std::optional<std::string_view> v = r.string();
        if (!v) return std::nullopt;
        if (*key == "board") {
          if (v->empty() || v->size() >= sizeof m.image.board) return std::nullopt;
          std::memcpy(m.image.board, v->data(), v->size());
          m.image.board[v->size()] = 0;
          got |= kBoard;
        } else if (*key == "version") {
          const std::optional<ota::Version> ver = ota::parseVersion(*v);
          if (!ver) return std::nullopt;
          m.image.version = *ver;
          got |= kVersion;
        } else if (*key == "sha256") {
          if (!ota::unb64url(*v, m.image.sha256, sizeof m.image.sha256)) return std::nullopt;
          got |= kSha;
        } else if (*key == "sig") {
          if (!ota::unb64url(*v, m.image.sig, sizeof m.image.sig)) return std::nullopt;
          got |= kSig;
        } else {
          if (!fileNameOk(*v)) return std::nullopt;
          std::memcpy(m.file, v->data(), v->size());
          m.file[v->size()] = 0;
          got |= kFile;
        }
      } else if (*key == "flags" || *key == "size" || *key == "fmt") {
        const std::optional<uint32_t> v = r.number();
        if (!v) return std::nullopt;
        if (*key == "flags") m.image.flags = *v, got |= kFlags;
        else if (*key == "size") m.image.size = *v, got |= kSize;
        else m.image.fmt = *v, got |= kFmt;
      } else if (*key == "auto") {
        if (r.word("true")) m.autoInstall = true;
        else if (r.word("false")) m.autoInstall = false;
        else return std::nullopt;
      } else if (!r.skip()) {
        return std::nullopt;
      }
    } while (r.take(','));
    if (!r.take('}')) return std::nullopt;
  }
  r.space();
  if (r.at != json.size() || got != 0xff) return std::nullopt;
  return m;
}

bool imageUrl(std::string_view manifestUrl, const char* file, char* out, size_t cap) {
  const size_t slash = manifestUrl.rfind('/');
  if (slash == std::string_view::npos) return false;
  const size_t n = std::strlen(file);
  if (slash + 1 + n + 1 > cap) return false;
  std::memcpy(out, manifestUrl.data(), slash + 1);
  std::memcpy(out + slash + 1, file, n + 1);
  return true;
}

const char* offerName(Offer o) {
  constexpr const char* kNames[] = {"install", "up_to_date", "wrong_board", "older_format", "rolled_back", "auto_off"};
  return kNames[size_t(o)];
}

Offer judge(const Manifest& m, const char* board, ota::Version running, uint32_t fmt,
            std::optional<ota::Version> rolledBack) {
  if (std::strcmp(m.image.board, board) != 0) return Offer::WrongBoard;
  if (!(running < m.image.version)) return Offer::UpToDate;
  if (m.image.fmt < fmt) return Offer::OlderFormat;
  if (rolledBack && *rolledBack == m.image.version) return Offer::RolledBack;
  if (!m.autoInstall) return Offer::AutoOff;
  return Offer::Install;
}

ota::Token wifiToken(const uint8_t random[12]) {
  ota::Token t;
  std::memcpy(t.b, kWifiMark, sizeof kWifiMark);
  std::memcpy(t.b + sizeof kWifiMark, random, 12);
  return t;
}

bool isWifiToken(const ota::Token& t) { return std::memcmp(t.b, kWifiMark, sizeof kWifiMark) == 0; }

}  // namespace wifiota
