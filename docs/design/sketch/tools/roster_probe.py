"""Throwaway model of the synthesized duel rules bound to roster.json, to measure
the wheel before anyone writes C++. tools/duel_balance.cpp (unit D4) replaces it.
Usage: python -I roster_probe.py [n]"""
import random
import sys

POUNCE, GUARD, TRICK, DOZE = 0, 1, 2, 3
SPLASH, SPROUT, SPARK = 0, 1, 2
def beats(a, b):
    if a == DOZE: return False
    if b == DOZE: return True
    return b == (a + 2) % 3          # pounce > trick > guard > pounce
def counter_of(s): return (s + 1) % 3

# id: (name, element, favourite stance, power) -- mirrors species.def / powers.def
SPECIES = [
    ("grungo",   SPLASH, TRICK,  "foresee"),
    ("mossby",   SPROUT, GUARD,  "shell"),
    ("puffkin",  SPROUT, POUNCE, "spores"),
    ("gilly",    SPLASH, GUARD,  "mend"),
    ("mothwick", SPARK,  TRICK,  "lantern"),
    ("mudge",    SPLASH, POUNCE, "dash"),
    ("fizz",     SPARK,  POUNCE, "zap"),
    ("hushwing", SPROUT, TRICK,  "hush"),
    ("tinker",   SPARK,  GUARD,  "gadget"),
]
GADGET_POOL = ["foresee", "shell", "spores", "mend", "lantern", "dash", "zap", "hush"]
HEARTS, MAX_TURNS, METER = 5, 15, 6
import os
COST = {"foresee": 5, "shell": 5, "spores": 6, "mend": 6, "lantern": 4, "dash": 3, "zap": 6, "hush": 5, "gadget": 5}
for kv in os.environ.get("COSTS", "").split(","):
    if kv: k, v = kv.split("="); COST[k] = int(v)


def battle(sa, sb, rng, players, assist=(0, 0)):
    sp = [SPECIES[sa], SPECIES[sb]]
    hearts = [HEARTS + assist[0], HEARTS + assist[1]]
    maxh = list(hearts)
    spark = [0, 0]
    adv = [beats(sp[0][1], sp[1][1]), beats(sp[1][1], sp[0][1])]
    for i in range(2):
        if adv[i]: spark[i] = 1                       # the element edge: a head-start pip
    hist = [[], []]
    shield, dozing, tiewin = [False, False], [False, False], [0, 0]
    hushed, dazzled = [0, 0], [0, 0]
    for turn in range(MAX_TURNS):
        st = [DOZE if dozing[i] else players[i](i, hist, sp, rng) for i in range(2)]
        dozed = list(dozing)
        dozing = [False, False]
        pw = [None, None]
        for i in range(2):
            if not dozed[i] and spark[i] >= COST[sp[i][3]]:
                spark[i] -= COST[sp[i][3]]
                p = sp[i][3]
                pw[i] = GADGET_POOL[rng.randrange(len(GADGET_POOL))] if p == "gadget" else p
        shown = list(st)
        for i in range(2):
            if pw[i] == "foresee" and st[1 - i] != DOZE: shown[i] = counter_of(st[1 - i])
            if pw[i] == "dash": tiewin[i] = 2                 # this turn and the next
            if pw[i] == "lantern": tiewin[i] = max(tiewin[i], 1); dazzled[1 - i] = 2
            if pw[i] == "hush": hushed[i] = 2
        if beats(shown[0], shown[1]): win = 0
        elif beats(shown[1], shown[0]): win = 1
        else:
            t = [tiewin[i] > 0 for i in range(2)]
            win = 0 if t[0] and not t[1] else 1 if t[1] and not t[0] else None
        tiewin = [max(0, x - 1) for x in tiewin]
        hit = [0, 0]
        if win is not None and shown[win] != DOZE:
            lose = 1 - win
            missed = (dazzled[win] > 0 and rng.random() < 0.5) or hushed[lose] > 0
            if not missed: hit[lose] += 1
        for i in range(2):
            if pw[i] == "zap": hit[1 - i] += 1
            if pw[i] == "spores": dozing[1 - i] = True
            if pw[i] == "shell": shield[i] = True
        for i in range(2):
            if hit[i] and shield[i]:
                hit[i] -= 1; shield[i] = False
            hearts[i] = max(0, hearts[i] - hit[i])
            if pw[i] == "mend": hearts[i] = min(maxh[i], hearts[i] + 1)
        for i in range(2):
            if not dozed[i]: spark[i] += 1
            if win is not None and i == 1 - win: spark[i] += 1     # rally: the clash loser
            spark[i] = min(METER, spark[i])
        hushed = [max(0, x - 1) for x in hushed]; dazzled = [max(0, x - 1) for x in dazzled]
        hist[0].append(st[0]); hist[1].append(st[1])
        if hearts[0] == 0 or hearts[1] == 0:
            if hearts[0] == hearts[1]: return None, turn + 1
            return (0 if hearts[1] == 0 else 1), turn + 1
    if hearts[0] == hearts[1]: return None, MAX_TURNS
    return (0 if hearts[0] > hearts[1] else 1), MAX_TURNS


def last(h): return [s for s in h if s != DOZE]
def random_player(i, hist, sp, rng): return rng.randrange(3)
def habit(i, hist, sp, rng): return sp[i][2] if rng.random() < 0.6 else rng.randrange(3)
def reader(i, hist, sp, rng):
    theirs = last(hist[1 - i])
    if not theirs: return rng.randrange(3)
    return counter_of(max(range(3), key=lambda s: (theirs.count(s), rng.random())))
def follower(p):
    """A young kid who takes the pet's whispered lean (the pet's Skill reads the friend) with probability p."""
    def play(i, hist, sp, rng):
        return reader(i, hist, sp, rng) if rng.random() < p else habit(i, hist, sp, rng)
    return play


def rate(w, k): return (w[0] + w[2] / 2) / k


def run(n, pa, pb, seed=1, assist=(0, 0)):
    rng = random.Random(seed)
    table, turns, overall = {}, [], [[0.0, 0] for _ in SPECIES]
    for a in range(9):
        for b in range(9):
            w = [0, 0, 0]
            for _ in range(n):
                win, t = battle(a, b, rng, (pa, pb), assist)
                turns.append(t)
                w[2 if win is None else win] += 1
            table[a, b] = w
            overall[a][0] += w[0] + w[2] / 2; overall[a][1] += n
            overall[b][0] += w[1] + w[2] / 2; overall[b][1] += n
    return table, sorted(turns)[len(turns) // 2], overall


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    t, med, ov = run(n, random_player, random_player)
    worst = max(abs(rate(w, n) - 0.5) for w in t.values())
    print(f"random v random, n={n}/pair: median turns {med}")
    print("  species overall:", " ".join(f"{SPECIES[i][0]}:{o[0]/o[1]:.3f}" for i, o in enumerate(ov)))
    print(f"  worst pair: {0.5 + worst:.3f}")
    for (a, b), w in sorted(t.items(), key=lambda kv: -abs(rate(kv[1], n) - 0.5))[:3]:
        print(f"    {SPECIES[a][0]} v {SPECIES[b][0]}: {rate(w, n):.3f}")
    m = 300
    for label, kid in (("habit kid", habit), ("kid follows lean 50%", follower(0.5))):
        for steps in (0, 1, 2, 3):
            tt, _, _ = run(m, reader, kid, seed=2, assist=(0, steps))
            print(f"  parent(reader) v {label}, assist {steps}: parent wins {sum(rate(w, m) for w in tt.values()) / 81:.3f}")
