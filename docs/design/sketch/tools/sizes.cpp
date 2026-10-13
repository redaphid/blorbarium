// Prints the sketch's measured sizes (x86-64 here; the structs hold no
// pointers except DuelSession's genome buffer, so the S3 layout matches up to
// padding). check.sh builds and runs it; BATTLES.md quotes its output.
#include <cstdio>
#include "blorb/dish_duel.h"
#include "blorb/duel_session.h"
#include "blorb/duel_wire.h"
#include "blorb/friend_egg.h"

int main() {
  using namespace blorb;
  std::printf("sizeof BattleCard=%zu Outcome=%zu DuelView=%zu DuelSkill=%zu MoveLog=%zu\n", sizeof(BattleCard),
              sizeof(Outcome), sizeof(DuelView), sizeof(DuelSkill), sizeof(MoveLog));
  std::printf("sizeof DuelSession=%zu DishDuel=%zu FriendEggRecord=%zu\n", sizeof(DuelSession), sizeof(DishDuel),
              sizeof(FriendEggRecord));
  std::printf("wire card=%zu beacon=%zu state=%zu genome=%zu (cap %zu)\n", kCardWireBytes, kBeaconWireBytes,
              kStateWireBytes, kGenomeWireBytes, kDatagramMax);
}
