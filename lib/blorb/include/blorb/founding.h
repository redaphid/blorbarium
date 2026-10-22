#pragma once
// How a device gets its species (docs/notes/decisions-2026-10-15.md D3): one
// per board, from a fixed MAC table (defs/badges.def), so the nine boards in
// it hold the regular cast's nine species once each. Any other board takes a
// species of the regular cast from a hash of its MAC (castSpecies). No
// chooser: a kid is handed the badge's creature, and the same board always
// founds the same pet.
//
// The regular cast is every species.def row but the octopus, a one-off
// variant: a board gets one only by name (a table row or found.pin), and an
// egg is one only when a parent was. Nothing picks it by chance.
//
// First boot only. A keepsake or a lineage, in any state, means no founding
// ever, so a pet that lives is never founded again. The firmware
// asks needsFounding() before the Dish exists and founds the Dish from
// foundingGenome(foundingSpecies(...)); the line takes the species' name
// (lineage.cpp).
//
// The owner can override the table over the cable first: `DEBUG found <id>`
// writes found.pin, and a fresh founding founds that species instead. The
// website cannot: the founding runs before the Dish and its Bluetooth exist.
//
// The badges flashed with 0.4.0 founded grungos with no SpeciesGene, before
// species existed. adoptSpecies makes such a pet his board's species
// (badgeSpecies, grungo included), once, keeping the pet (Dish::adoptSpecies).
#include <cstdint>
#include <optional>
#include "blorb/genome.h"
#include "blorb/registry.h"
#include "blorb/seams.h"
#include "blorb/species.h"

namespace blorb {

class Creature;   // creature.h

// The table's species for this MAC; nullopt for a board not in it.
std::optional<SpeciesId> tableSpecies(const uint8_t mac[6]);
// True for every species but the octopus, the one-off variant.
constexpr bool inRegularCast(SpeciesId s) { return s != species::octopus; }
// The regular cast's species number `n` modulo its size, in species.def order.
SpeciesId castSpecies(uint32_t n);
// The board's species: the table's, else castSpecies(fnv1a of the MAC).
SpeciesId badgeSpecies(const uint8_t mac[6]);

// True when the Dish would found: no snapshot slot, no rescued slot and no
// lineage.log. Reads only, because a Keepsake load quarantines a torn slot.
bool needsFounding(Storage&);
// What a fresh founding founds: the pinned species, else badgeSpecies(mac).
SpeciesId foundingSpecies(Storage&, const uint8_t mac[6]);

// found.pin on petfs (its bytes are in founding.cpp). A pin naming a species
// this firmware has no row for reads as no pin.
std::optional<SpeciesId> readFoundPin(Storage&);
bool writeFoundPin(Storage&, SpeciesId);
bool clearFoundPin(Storage&);   // moved aside to found.off: nothing on petfs is deleted

// A board's founder: starterGenomeOf(species, seed) with every expressed
// palette gene's hue, saturation and value, and the mottling density, rolled
// across the species' pack bands (paint::bandsFor) from the seed. The
// starter only nudges those bytes, so it has about 10^4 distinct founders and
// two boards of one species could hatch the same pet; this has about 10^20,
// and one seed (the board's MAC hash) founds the same pet every time.
Genome foundingGenome(SpeciesId, uint32_t speciesSeed, const LookBands&);

// The species a genome expresses: its first SpeciesGene's row, else grungo.
SpeciesId speciesOf(const Genome&);

// True for a genome from before species: no SpeciesGene, so it expresses as grungo.
bool predatesSpecies(const Genome&);

// A pet from before species becomes `species` as if he had been founded one,
// and stays himself. The genes a founder of the species carries past grungo's
// recipe (SpeciesGene, PowerGene, KnackGene, the duel genes, war paint) are
// appended at the uids foundingGenome gives them (species.h), so a friend
// egg's cross lines them up with any other badge's. Then the bytes the
// species owns in the recipe are written: the founder's rolled look and the
// row's starter bytes (boldness, sleep, appetite; the voice follows the
// SpeciesGene). A row byte is written only where he still holds grungo's
// recipe value, so a byte his own recipe wrote differently (a pet founded by
// an older recipe) stays his. Age, chemistry, brain, scars and every other
// byte stay his. Grungo is adopted like any species: he gains grungo's
// SpeciesGene and the rest, and the rolled look. False, and nothing changes,
// unless predatesSpecies. The caller records the lineage Checkpoint and saves.
bool adoptSpecies(Creature&, SpeciesId, uint32_t speciesSeed, const LookBands&);

// The species a board shows the world as (its Bluetooth name): its pet's, or
// the board's for a pet from before species, which the board will adopt. pet
// is null when the pet was not read (a sealed board): then badgeSpecies.
SpeciesId advertisedSpecies(const Genome* pet, const uint8_t mac[6]);

}  // namespace blorb
