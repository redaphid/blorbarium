#include "blorb/genome.h"

#include <algorithm>
#include "blorb/fixed.h"

namespace blorb {
namespace {

uint16_t uidAt(const uint8_t* gene) { return uint16_t(gene[6] | (gene[7] << 8)); }

GeneHeader headerAt(const uint8_t* g) {
  return GeneHeader{g[0], g[1], g[2], Stage(g[3]), g[4], g[5], GeneUid{uidAt(g)}};
}

void putHeader(std::vector<uint8_t>& out, size_t at, const GeneHeader& h) {
  const uint8_t bytes[kGeneHeaderLen] = {h.type,     h.len,       h.flags,           uint8_t(h.stage),
                                         h.featGate, h.mutWeight, uint8_t(h.uid.v), uint8_t(h.uid.v >> 8)};
  out.insert(out.begin() + std::ptrdiff_t(at), bytes, bytes + kGeneHeaderLen);
}

// Start offset of the gene with this uid, walking the length chain; npos if absent.
constexpr size_t npos = SIZE_MAX;
size_t locate(const std::vector<uint8_t>& bytes, GeneUid uid) {
  for (size_t at = 0; at + kGeneHeaderLen <= bytes.size(); at += kGeneHeaderLen + bytes[at + 1]) {
    if (uidAt(&bytes[at]) == uid.v) return at;
  }
  return npos;
}

}  // namespace

std::optional<Genome> Genome::parse(const uint8_t* bytes, size_t len) {
  if (len > kMaxGenomeBytes) return std::nullopt;
  Genome g;
  std::vector<uint16_t> uids;
  for (size_t at = 0; at < len;) {
    if (len - at < kGeneHeaderLen || len - at - kGeneHeaderLen < bytes[at + 1]) return std::nullopt;
    g.offsets_.push_back(uint16_t(at));
    uids.push_back(uidAt(bytes + at));
    at += kGeneHeaderLen + bytes[at + 1];
  }
  std::sort(uids.begin(), uids.end());
  if (std::adjacent_find(uids.begin(), uids.end()) != uids.end()) return std::nullopt;
  g.bytes_.assign(bytes, bytes + len);
  return g;
}

GeneView Genome::gene(uint16_t index) const {
  const uint8_t* g = bytes_.data() + offsets_[index];
  return GeneView{headerAt(g), g + kGeneHeaderLen, index};
}

std::optional<GeneView> Genome::find(GeneUid uid) const {
  for (uint16_t i = 0; i < geneCount(); ++i) {
    if (uidAt(bytes_.data() + offsets_[i]) == uid.v) return gene(i);
  }
  return std::nullopt;
}

GeneUid Genome::nextUid() const {
  uint16_t top = 0;
  for (uint16_t off : offsets_) top = std::max(top, uidAt(bytes_.data() + off));
  return GeneUid{uint16_t(top + 1)};
}

uint32_t Genome::hash() const { return fnv1a(bytes_.data(), bytes_.size()); }

GenomeBuilder GenomeBuilder::from(const Genome& g) {
  GenomeBuilder b;
  b.bytes_ = g.bytes();
  return b;
}

GenomeBuilder& GenomeBuilder::append(GeneHeader h, const uint8_t* body) {
  putHeader(bytes_, bytes_.size(), h);
  bytes_.insert(bytes_.end(), body, body + h.len);
  return *this;
}

// An op naming a missing uid leaves the builder unchanged. Callers that must
// fail loudly on it (apply) check the uid first.
GenomeBuilder& GenomeBuilder::insertAfter(GeneUid after, GeneHeader h, const uint8_t* body) {
  size_t at = locate(bytes_, after);
  if (at == npos) return *this;
  size_t end = at + kGeneHeaderLen + bytes_[at + 1];
  putHeader(bytes_, end, h);
  bytes_.insert(bytes_.begin() + std::ptrdiff_t(end + kGeneHeaderLen), body, body + h.len);
  return *this;
}

GenomeBuilder& GenomeBuilder::erase(GeneUid uid) {
  size_t at = locate(bytes_, uid);
  if (at == npos) return *this;
  auto first = bytes_.begin() + std::ptrdiff_t(at);
  bytes_.erase(first, first + std::ptrdiff_t(kGeneHeaderLen + bytes_[at + 1]));
  return *this;
}

GenomeBuilder& GenomeBuilder::setByte(GeneUid uid, uint8_t bodyOffset, uint8_t value) {
  size_t at = locate(bytes_, uid);
  if (at != npos && bodyOffset < bytes_[at + 1]) bytes_[at + kGeneHeaderLen + bodyOffset] = value;
  return *this;
}

GenomeBuilder& GenomeBuilder::setFlags(GeneUid uid, uint8_t flags) {
  size_t at = locate(bytes_, uid);
  if (at != npos) bytes_[at + 2] = flags;
  return *this;
}

std::optional<Genome> GenomeBuilder::build() const { return Genome::parse(bytes_.data(), bytes_.size()); }

}  // namespace blorb
