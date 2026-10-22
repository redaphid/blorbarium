#!/usr/bin/env python3
"""A test-only prototype of genetics v2 (docs/design/LINEAGE.md): the trait
loci, their expression, recombination at egg laying (parent + pond mate, or
parent + friend) and mutation. Nothing in the engine uses it. It simulates
families of badges over generations and prints how often each trait shows.

    python3 scripts/test/lineage_sim.py                 # 2000 families, 10 generations
    python3 scripts/test/lineage_sim.py --families 500 --policy rarest
    python3 scripts/test/lineage_sim.py --selfing 0.3 --friend 0.0

Deterministic: one seed, Python's own Mersenne twister, no other input.
"""
import argparse
import random
from collections import Counter, defaultdict

# ---- the Mendelian loci (traits.def in the design) --------------------------------
# allele: (trait, dominance 'D' or 'r', rarity) ; allele 0 of every locus is wild type.
# pond: the wild allele frequencies a pond mate draws from (index = allele), by default
# for every species; SPECIES_POND overrides a few per species.
COMMON, UNCOMMON, RARE, LEGENDARY = "common", "uncommon", "rare", "legendary"
LOCI = [
    # name       alleles after wild                                                  pond freqs (wild first)
    ("rhythm",  [("Hiccups", "r", COMMON)],                                           [0.62, 0.38]),
    ("footing", [("Wobbly", "D", UNCOMMON), ("Clumsy", "r", COMMON)],                 [0.62, 0.05, 0.33]),
    ("strike",  [("Twin Strike", "r", UNCOMMON)],                                     [0.74, 0.26]),
    ("hide",    [("Thick Shell", "D", UNCOMMON)],                                     [0.965, 0.035]),
    ("fortune", [("Lucky", "r", RARE)],                                               [0.90, 0.10]),
    ("sight",   [("Seer's Eye", "D", UNCOMMON), ("Third Eye", "r", LEGENDARY)],       [0.965, 0.035, 0.0]),
    ("temper",  [("Grumpy", "D", UNCOMMON), ("Stubborn", "r", COMMON)],               [0.59, 0.05, 0.36]),
    ("heart",   [("Cuddlebug", "D", COMMON), ("Show-off", "r", RARE)],                [0.80, 0.12, 0.08]),
    ("balance", [("Upside-Down", "r", RARE), ("Dizzy", "r", UNCOMMON)],               [0.70, 0.12, 0.18]),
    ("eyes",    [("Squinty", "r", UNCOMMON), ("Sepia", "r", RARE)],                   [0.68, 0.20, 0.12]),
    ("coat",    [("Albino", "r", RARE), ("Shadow", "r", RARE), ("Mood Ring", "D", UNCOMMON)], [0.80, 0.09, 0.09, 0.02]),
    ("spirit",  [("Glass Cannon", "D", UNCOMMON), ("Copycat", "r", RARE), ("Ghostly", "r", LEGENDARY)],
                [0.95, 0.03, 0.02, 0.0]),
    ("voice",   [("Chatterbox", "D", COMMON), ("Echo", "r", RARE)],                     [0.84, 0.08, 0.08]),
]
# Species flavour: the pond leans. (grungo hiccups and sees; fizz strikes twice; mossby hides.)
SPECIES_POND = {
    "grungo": {"rhythm": [0.45, 0.55], "sight": [0.85, 0.15, 0.0]},
    "fizz":   {"strike": [0.55, 0.45]},
    "mossby": {"hide": [0.85, 0.15]},
    "mudge":  {"footing": [0.55, 0.15, 0.30]},
    "puffkin": {"heart": [0.65, 0.25, 0.10]},
    "gilly":  {"heart": [0.70, 0.20, 0.10]},
    "mothwick": {"coat": [0.72, 0.09, 0.09, 0.10]},
    "hushwing": {"eyes": [0.55, 0.20, 0.25]},
    "tinker": {"spirit": [0.88, 0.04, 0.08, 0.0]},
}
SPECIES = ["grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz", "hushwing", "tinker"]

# Per-allele chance, per egg, that it mutates to another allele of its locus,
# drawn by rarity weight: so a legendary allele (pond 0) can still be born.
MUTATION_RATE = 1 / 64
RARITY_WEIGHT = {COMMON: 8.0, UNCOMMON: 4.0, RARE: 1.0, LEGENDARY: 0.5}

# ---- polygenic traits from real gene bytes -----------------------------------------
# Each byte starts at the species norm (128 here: the doc's thresholds are relative
# to the norm), spread at founding, point-mutates like mutate.cpp's Nudge rule,
# and expresses a trait when it sits a threshold away from the norm.
POLY = [
    # gene byte                      low trait        high trait
    ("SizeGene.base (adult)",        "Tiny",          "Giant"),
    ("startle receptor gain",        "Fearless",      "Jumpy"),
    ("melatonin emitter gain",       None,            "Night Owl"),
    ("sleep_gate receptor gain",     "Insomniac",     "Sleepyhead"),
    ("hunger tonic fill",            "Picky Eater",   "Glutton"),
    ("toxin->injury reaction rate",  "Iron Stomach",  None),
    ("TemperamentGene.explore",      "Scatterbrain",  "Curious"),
]
POLY_THRESHOLD = 18       # bytes from the norm
POND_SD = 16              # the pond mate's bytes scatter round the norm like this
POINT_RATE = 0.30         # chance a given gene byte point-mutates per egg (the forced changes included)
NUDGE_MAX = 8             # mutate.cpp's kNudgeMax
WILD_RATE = 10 / 255      # MutationPolicyGene.wild: a point mutation rerolls the whole range

# Sports: only mutation makes them. Shiny is a wild palette roll into the band's edge.
SHINY_RATE = 1 / 300

DUEL_TRAITS = {"Tiny", "Giant", "Jumpy", "Fearless", "Night Owl", "Insomniac", "Sleepyhead", "Glutton",
               "Iron Stomach", "Twin Strike", "Hiccups", "Wobbly", "Clumsy", "Thick Shell", "Lucky", "Grumpy",
               "Stubborn", "Show-off", "Cuddlebug", "Glass Cannon", "Copycat", "Ghostly"}
MAX_DUEL_ACTIVE = 2


def rarity_of(trait):
    for _, alleles, _ in LOCI:
        for t, _, r in alleles:
            if t == trait:
                return r
    return RARE if trait == "Shiny" else UNCOMMON   # the polygenic traits


def pond_of(species, locus):
    name, _, freqs = LOCI[locus]
    return SPECIES_POND.get(species, {}).get(name, freqs)


def draw(rng, weights):
    x = rng.random() * sum(weights)
    for i, w in enumerate(weights):
        x -= w
        if x < 0:
            return i
    return len(weights) - 1


class Pet:
    __slots__ = ("species", "alleles", "poly", "shiny")

    def __init__(self, species, alleles, poly, shiny=False):
        self.species, self.alleles, self.poly, self.shiny = species, alleles, poly, shiny

    def traits(self):
        out = []
        for li, (_, alleles, _) in enumerate(LOCI):
            a, b = self.alleles[li]
            doms = sorted(x for x in (a, b) if x and alleles[x - 1][1] == "D")
            if doms:
                out.append(alleles[doms[0] - 1][0])
            elif a and a == b:
                out.append(alleles[a - 1][0])
        for (_, lo, hi), v in zip(POLY, self.poly):
            if lo and v <= 128 - POLY_THRESHOLD:
                out.append(lo)
            if hi and v >= 128 + POLY_THRESHOLD:
                out.append(hi)
        if self.shiny:
            out.append("Shiny")
        return out

    def carried(self):
        """Recessive alleles held but not shown: the egg's '?' speckle."""
        out = []
        for li, (_, alleles, _) in enumerate(LOCI):
            a, b = self.alleles[li]
            for x in (a, b):
                if x and alleles[x - 1][1] == "r" and a != b and not any(
                        y and alleles[y - 1][1] == "D" for y in (a, b) if y != x):
                    out.append(alleles[x - 1][0])
        return out


def mutate_allele(rng, locus, allele):
    if rng.random() >= MUTATION_RATE:
        return allele
    alleles = LOCI[locus][1]
    weights = [RARITY_WEIGHT[COMMON]] + [RARITY_WEIGHT[r] for _, _, r in alleles]
    weights[allele] = 0.0
    return draw(rng, weights)


def nudge(rng, v):
    if rng.random() >= POINT_RATE:
        return v
    if rng.random() < WILD_RATE:
        return rng.randrange(256)
    step = rng.randint(1, NUDGE_MAX)
    return max(0, min(255, v + (step if rng.random() < 0.5 else -step)))


FOUNDER_SD = 12           # a founder sits where a line settles (about the mean of itself and the pond)


def pond_byte(rng, sd=POND_SD):
    return max(0, min(255, 128 + round(rng.gauss(0, sd))))


def founder(rng, species):
    alleles = [(draw(rng, pond_of(species, li)), draw(rng, pond_of(species, li))) for li in range(len(LOCI))]
    poly = [pond_byte(rng, FOUNDER_SD) for _ in POLY]
    return Pet(species, alleles, poly, rng.random() < SHINY_RATE)


def egg(rng, parent, selfing, friend=None):
    """One egg. Allele one is the parent's (either of its pair). Allele two is the
    friend's (either of its pair) in a friend egg; otherwise the parent's again
    with chance `selfing`, else the pond mate's, drawn from the species' pond."""
    alleles = []
    for li in range(len(LOCI)):
        a = rng.choice(parent.alleles[li])
        if friend is not None:
            b = rng.choice(friend.alleles[li])
        elif rng.random() < selfing:
            b = rng.choice(parent.alleles[li])
        else:
            b = draw(rng, pond_of(parent.species, li))
        alleles.append((mutate_allele(rng, li, a), mutate_allele(rng, li, b)))
    # Polygenic bytes: the mean of the two parents' bytes (the friend's, else the
    # pond mate's, drawn round the species norm), then point mutation. The mean is
    # what keeps a line from drifting to the extremes over many generations.
    poly = []
    for k, p in enumerate(parent.poly):
        q = friend.poly[k] if friend is not None else pond_byte(rng)
        poly.append(nudge(rng, (p + q + rng.randrange(2)) // 2))
    shiny = parent.shiny if rng.random() > 0.25 else False   # wild bytes drift back
    shiny = shiny or rng.random() < SHINY_RATE
    return Pet(parent.species, alleles, poly, shiny)


def pick(rng, eggs, policy):
    if policy == "random":
        return rng.choice(eggs)
    if policy == "most_traits":
        return max(eggs, key=lambda e: (len(e.traits()), rng.random()))
    if policy == "rarest":
        rank = {COMMON: 1, UNCOMMON: 2, RARE: 4, LEGENDARY: 8}
        return max(eggs, key=lambda e: (sum(rank[rarity_of(t)] for t in e.traits()), rng.random()))
    if policy == "fewest_traits":
        return min(eggs, key=lambda e: (len(e.traits()), rng.random()))
    raise ValueError(policy)


def simulate(args):
    rng = random.Random(args.seed)
    gens = args.generations
    shown = [Counter() for _ in range(gens + 1)]
    carried = [Counter() for _ in range(gens + 1)]
    ntraits = [Counter() for _ in range(gens + 1)]
    nduel = [Counter() for _ in range(gens + 1)]
    resurfaced = Counter()      # shown by a child whose parent did not show it (recessive came back)
    resurf_n = Counter()
    pets_per_gen = 0
    for _ in range(args.families):
        # The founding nest: three eggs of the badge's species, the kid picks one.
        family = [pick(rng, [founder(rng, s) for _ in range(3)], args.policy) for s in SPECIES]
        pets_per_gen += len(family)
        for gen in range(gens + 1):
            for p in family:
                t = p.traits()
                shown[gen].update(t)
                carried[gen].update(set(p.carried()))
                ntraits[gen][len(t)] += 1
                nduel[gen][min(MAX_DUEL_ACTIVE + 1, len([x for x in t if x in DUEL_TRAITS]))] += 1
            if gen == gens:
                break
            nxt = []
            for i, p in enumerate(family):
                eggs = [egg(rng, p, args.selfing) for _ in range(args.clutch)]
                if rng.random() < args.friend:   # a hug after a duel: a friend egg takes the last slot
                    f = family[rng.choice([j for j in range(len(family)) if j != i])]
                    fe = egg(rng, p, args.selfing, friend=f)
                    if rng.random() < args.pick_friend:
                        child = fe
                    else:
                        child = pick(rng, eggs[:-1], args.policy)
                else:
                    child = pick(rng, eggs, args.policy)
                before = set(p.traits())
                for t in child.traits():
                    resurf_n[t] += 1
                    if t not in before:
                        resurfaced[t] += 1
                nxt.append(child)
            family = nxt
    per = args.families * len(SPECIES)
    return shown, carried, ntraits, nduel, resurfaced, resurf_n, per


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--families", type=int, default=2000)
    ap.add_argument("--generations", type=int, default=10)
    ap.add_argument("--clutch", type=int, default=3)
    ap.add_argument("--selfing", type=float, default=0.35, help="chance allele two is the parent's again, not the pond's")
    ap.add_argument("--friend", type=float, default=0.3, help="chance a generation's clutch holds a friend egg")
    ap.add_argument("--pick-friend", type=float, default=0.5, help="chance the kid keeps the friend egg")
    ap.add_argument("--policy", default="random", choices=["random", "most_traits", "rarest", "fewest_traits"])
    ap.add_argument("--seed", type=int, default=45)
    args = ap.parse_args()
    shown, carried, ntraits, nduel, resurfaced, resurf_n, per = simulate(args)

    print(f"# {args.families} families x 9 badges, {args.generations} generations, clutch {args.clutch}, "
          f"selfing {args.selfing}, friend eggs {args.friend} (kept {args.pick_friend}), kids pick '{args.policy}'")
    traits = [t for _, al, _ in LOCI for t, _, _ in al] + [x for _, lo, hi in POLY for x in (lo, hi) if x] + ["Shiny"]
    cols = [0, 1, 2, 5, args.generations]
    print("\n% of pets showing each trait, by generation (gen 0 = the founding nest's pick)")
    print(f"{'trait':14} {'rarity':10} " + " ".join(f"g{g:<5}" for g in cols) + "  carried@g0  came back*  seen as")
    for t in traits:
        row = " ".join(f"{100 * shown[g][t] / per:5.1f}" for g in cols)
        back = f"{100 * resurfaced[t] / resurf_n[t]:5.0f}%" if resurf_n[t] else "   - "
        f = 100 * shown[args.generations][t] / per
        seen = COMMON if f >= 12 else UNCOMMON if f >= 4 else RARE if f >= 1 else LEGENDARY
        print(f"{t:14} {rarity_of(t):10} {row}   {100 * carried[0][t] / per:5.1f}     {back}     {seen}")
    print("\n* of the pets showing it, the share whose parent did not. 'seen as': the tier its g%d share falls in"
          " (common >= 12%%, uncommon >= 4%%, rare >= 1%%, else legendary)" % args.generations)
    print("\ntraits per pet (all kinds):")
    for g in cols:
        dist = ntraits[g]
        mean = sum(k * v for k, v in dist.items()) / per
        print(f"  g{g:<3} mean {mean:4.2f}   " + "  ".join(f"{k}:{100 * dist[k] / per:4.1f}%" for k in range(0, 9)))
    print(f"\nduel traits per pet (capped at {MAX_DUEL_ACTIVE} active; '{MAX_DUEL_ACTIVE + 1}' = more expressed than active):")
    for g in cols:
        dist = nduel[g]
        print(f"  g{g:<3} " + "  ".join(f"{k}:{100 * dist[k] / per:4.1f}%" for k in range(0, MAX_DUEL_ACTIVE + 2)))
    leg = sum(shown[args.generations][t] for t in traits if rarity_of(t) == LEGENDARY)
    print(f"\nlegendary traits shown at g{args.generations}: {100 * leg / per:.2f}% of pets "
          f"(about {leg / args.families:.2f} per family of 9)")


if __name__ == "__main__":
    main()
