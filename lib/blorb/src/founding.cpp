#include "blorb/founding.h"

#include <cstring>
#include "blorb/creature.h"

namespace blorb {
namespace {

// found.pin: magic "BFPN", version u8, species u8. Six bytes or it is no pin.
constexpr const char* kPinFile = "found.pin";
constexpr const char* kPinAside = "found.off";
constexpr char kMagic[4] = {'B', 'F', 'P', 'N'};
constexpr uint8_t kVersion = 1;
constexpr size_t kPinLen = sizeof kMagic + 2;

// Every name the keepsake and the lineage ever leave on the flash. A rescued
// slot alone still means a pet lived here.
constexpr const char* kPetFiles[] = {"snap.a", "snap.b", "rescue/snap.a.0", "rescue/snap.b.0", "lineage.log",
                                     "lineage.tmp"};

}  // namespace

bool needsFounding(Storage& s) {
  for (const char* f : kPetFiles)
    if (s.size(f) > 0) return false;
  return true;
}

std::optional<Nest> nestFor(Storage& s, uint32_t speciesSeed) {
  if (!needsFounding(s)) return std::nullopt;
  if (std::optional<SpeciesId> pin = readFoundPin(s)) return Nest{{*pin}, 1};
  const FoundingNest n = foundingNest(speciesSeed);
  return Nest{{n.eggs[0], n.eggs[1], n.eggs[2]}, kCycle};
}

std::optional<SpeciesId> readFoundPin(Storage& s) {
  uint8_t b[kPinLen + 1];
  std::optional<size_t> n = s.read(kPinFile, 0, b, sizeof b);
  if (!n || *n != kPinLen || std::memcmp(b, kMagic, sizeof kMagic) != 0 || b[4] != kVersion) return std::nullopt;
  if (b[5] >= kSpeciesCount) return std::nullopt;
  return SpeciesId{b[5]};
}

bool writeFoundPin(Storage& s, SpeciesId species) {
  const uint8_t b[kPinLen] = {uint8_t(kMagic[0]), uint8_t(kMagic[1]), uint8_t(kMagic[2]), uint8_t(kMagic[3]),
                              kVersion, species.v};
  return s.writeAtomic(kPinFile, b, sizeof b);
}

bool clearFoundPin(Storage& s) {
  uint8_t probe;
  return !s.read(kPinFile, 0, &probe, 0) || s.rename(kPinFile, kPinAside);
}

std::optional<SpeciesId> FoundingChooser::sample(const BodySample& body, uint32_t nowMs) {
  if (done_) return std::nullopt;
  if (!started_) started_ = true, idleSinceMs_ = nowMs;
  SenseOut out;
  detectors_.sample(body, nowMs, out);
  const uint8_t was = lit_;
  std::optional<uint8_t> kept = pickEgg(out, lit_, nest_.count);
  if (lit_ != was) idleSinceMs_ = nowMs;
  if (!kept && nowMs - idleSinceMs_ >= kTimeoutMs) kept = 0;
  if (!kept) return std::nullopt;
  done_ = true;
  return nest_.eggs[*kept];
}

}  // namespace blorb
