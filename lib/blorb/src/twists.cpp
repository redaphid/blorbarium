// The phone's twists (defs/twists.def, DEVIATIONS.md 4). Each handler takes
// every word it needs and checks it before touching the dish, so a refused op
// changes nothing.
#include <cstring>
#include "blorb/dish.h"
#include "blorb/protocol.h"

namespace blorb {
namespace {

constexpr size_t kNameMax = 15;
constexpr Fx kProphecyTreatVision = Fx::ratio(1, 2);

std::optional<unsigned> smallNumber(std::string_view w) {
  if (w.empty() || w.size() > 3) return std::nullopt;
  unsigned v = 0;
  for (char c : w) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + unsigned(c - '0');
  }
  return v;
}

// A registry row by its name or its numeric id.
template <class Row, size_t N>
const Row* rowNamed(const Row (&rows)[N], std::string_view w) {
  std::optional<unsigned> n = smallNumber(w);
  for (const Row& r : rows)
    if (n ? r.id.v == *n : w == r.name) return &r;
  return nullptr;
}

// A brain feature: a locus name, recent_<stim>, or a locus number.
std::optional<LocusId> featureNamed(std::string_view w) {
  std::optional<LocusId> id;
  constexpr std::string_view kRecent = "recent_";
  if (std::optional<unsigned> n = smallNumber(w); n && *n < kLocusSlots) id = LocusId{uint8_t(*n)};
  else if (w.substr(0, kRecent.size()) == kRecent) {
    if (const StimInfo* s = rowNamed(STIMULI, w.substr(kRecent.size()))) id = locus::recent(s->id);
  } else if (const LocusInfo* l = rowNamed(LOCI, w)) {
    id = l->id;
  }
  for (LocusId f : FEATURES)
    if (id && f == *id) return id;
  return std::nullopt;
}

bool nameChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

}  // namespace

TwistStatus twist_stimulus(Dish& dish, Args& a) {
  std::optional<std::string_view> w = a.word();
  if (!w || !a.done()) return TwistStatus::Malformed;
  const StimInfo* s = rowNamed(STIMULI, *w);
  if (!s) return TwistStatus::OutOfRange;
  if (s->id == stim::button || s->id == stim::fed) return TwistStatus::BodyOnly;   // a pellet, or the meal it gives
  dish.fire(s->id);
  return TwistStatus::Applied;
}

TwistStatus twist_gene_edit(Dish& dish, Args& a) {
  std::optional<uint32_t> uid = a.number(UINT16_MAX);
  std::optional<uint32_t> offset = a.number(UINT8_MAX);
  std::optional<uint32_t> value = a.number(UINT8_MAX);
  if (!uid || !offset || !value || !a.done()) return TwistStatus::Malformed;
  if (!std::holds_alternative<Creature>(dish.occupant())) return TwistStatus::NotNow;
  return dish.editGene(GeneUid{uint16_t(*uid)}, uint8_t(*offset), uint8_t(*value)) ? TwistStatus::Applied
                                                                                     : TwistStatus::OutOfRange;
}

TwistStatus twist_rename(Dish& dish, Args& a) {
  std::optional<std::string_view> w = a.word();
  if (!w || !a.done() || w->empty() || w->size() > kNameMax) return TwistStatus::Malformed;
  for (char c : *w)
    if (!nameChar(c)) return TwistStatus::Malformed;
  char name[kNameMax + 1] = {};
  std::memcpy(name, w->data(), w->size());
  dish.rename(name);
  return TwistStatus::Applied;
}

TwistStatus twist_pick(Dish& dish, Args& a) {
  std::optional<uint32_t> egg = a.number(UINT8_MAX);
  if (!egg || !a.done()) return TwistStatus::Malformed;
  const Clutch* k = std::get_if<Clutch>(&dish.occupant());
  if (!k) return TwistStatus::NotNow;
  return dish.pick(uint8_t(*egg)) ? TwistStatus::Applied : TwistStatus::OutOfRange;
}

TwistStatus twist_prophecy(Dish& dish, Args& a) {
  std::optional<std::string_view> feature = a.word(), action = a.word(), drive = a.word();
  std::optional<int32_t> permille = a.signedNumber(-1000, 1000);
  if (!feature || !action || !drive || !permille || !a.done()) return TwistStatus::Malformed;
  std::optional<LocusId> f = featureNamed(*feature);
  const ActionInfo* act = rowNamed(ACTIONS, *action);
  const DriveInfo* d = rowNamed(DRIVES, *drive);
  if (!f || !act || !d) return TwistStatus::OutOfRange;
  Creature* c = std::get_if<Creature>(&dish.occupant());
  if (!c) return TwistStatus::NotNow;
  c->prophesy(*f, act->id, d->id, Fx{int32_t(int64_t(*permille) * Fx::kOne / 1000)});
  return TwistStatus::Applied;
}

TwistStatus twist_prophecy_treat(Dish& dish, Args& a) {
  if (!a.done()) return TwistStatus::Malformed;
  Creature* c = std::get_if<Creature>(&dish.occupant());
  if (!c) return TwistStatus::NotNow;
  c->inject(chem::vision, c->chemistry().chem[chem::vision.v] + kProphecyTreatVision);
  return TwistStatus::Applied;
}

}  // namespace blorb
