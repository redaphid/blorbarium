"""Throwaway model of the queued special (BATTLES.md, "The queued special") on
top of roster_probe.py's rules, to choose the numbers before D4 writes C++.
tools/duel_balance.cpp (unit D4) replaces it. Usage: python -I special_probe.py [n]

What changes from roster_probe: a power is never instant. BOOT queues it with a
hidden landing stance and a wind-up (quick 1 turn, heavy 2). While winding up
the pet Braces: a Pounce bonks off it, a Guard does nothing, and a Trick lands
and staggers it (a quick special is lost, a heavy one drops to quick). On the
landing turn the hidden stance meets the friend's: if the friend's stance beats
it, the special is ripposted (no power, the owner takes 1 + the extra); if a
hushing friend dodges, it whiffs; otherwise it lands (ties go to the special):
the friend takes 1 + extra (quick 1, heavy 2) and the power fires."""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roster_probe as rp  # noqa: E402

POUNCE, GUARD, TRICK, DOZE, BRACE = 0, 1, 2, 3, 4
SPECIES, POOL = rp.SPECIES, rp.GADGET_POOL
COST = dict(rp.COST, lantern=3) if not os.environ.get("COSTS") else rp.COST   # the special makes lantern 3 (powers.def)
HEARTS, MAX_TURNS, METER = rp.HEARTS, rp.MAX_TURNS, rp.METER
EXTRA = {1: int(os.environ.get("QUICK", 1)), 2: int(os.environ.get("HEAVY", 2))}
# noriposte (chosen): a countered Foresee landing whiffs instead of backfiring.
# counter (grungo 0.667) and plain (0.429) were measured and rejected.
FORESEE_LANDS = os.environ.get("FORESEE", "noriposte")


def beats(a, b):
    if a in (DOZE, BRACE): return False
    if b == DOZE: return True
    if b == BRACE: return a == TRICK
    return b == (a + 2) % 3


def battle(sa, sb, rng, players, assist=(0, 0), stats=None):
    sp = [SPECIES[sa], SPECIES[sb]]
    hearts = [HEARTS + assist[0], HEARTS + assist[1]]
    maxh = list(hearts)
    spark = [1 if rp.beats(sp[i][1], sp[1 - i][1]) else 0 for i in range(2)]
    hist = [[], []]
    queue = [None, None]            # [turnsLeft, windup, landingStance, power]
    shell, dozing, hushed, dazzled, bonk = [False] * 2, [False] * 2, [0, 0], [0, 0], [0, 0]
    for turn in range(MAX_TURNS):
        st = [None, None]
        for i in range(2):
            if dozing[i]: st[i] = DOZE
            elif queue[i] and queue[i][0] > 0: st[i] = BRACE
            elif queue[i] and queue[i][0] == 0: st[i] = queue[i][2]
            else:
                ctx = dict(hist=hist, sp=sp, me=i, theirQueue=queue[1 - i], spark=spark[i])
                s, special = players[i](ctx, rng)
                st[i] = s
                if special and spark[i] >= COST[sp[i][3]]:
                    spark[i] -= COST[sp[i][3]]
                    windup = special
                    p = sp[i][3]
                    p = POOL[rng.randrange(len(POOL))] if p == "gadget" else p
                    queue[i] = [windup - 1, windup, s, p]      # this turn is the first wind-up turn
                    st[i] = BRACE if windup >= 1 else s
                    if stats is not None: stats["queued"] += 1
        dozed = [s == DOZE for s in st]
        dozing = [False, False]
        landing = [queue[i] is not None and queue[i][0] == 0 and st[i] == queue[i][2] and not dozed[i] for i in range(2)]
        landing = [landing[i] and turn > 0 and hist[i] and hist[i][-1] == BRACE for i in range(2)]
        shown = list(st)
        for i in range(2):
            if landing[i] and queue[i][3] == "foresee" and FORESEE_LANDS == "counter" and st[1 - i] in (0, 1, 2):
                shown[i] = (st[1 - i] + 1) % 3
        hit = [0, 0]
        fire = [None, None]
        for i in range(2):
            if not landing[i]: continue
            extra = EXTRA[queue[i][1]]
            o = 1 - i
            if beats(shown[o], shown[i]) and queue[i][3] == "foresee" and FORESEE_LANDS == "noriposte":
                if stats is not None: stats["whiffed"] += 1          # he saw the counter coming: it whiffs, no backfire
            elif beats(shown[o], shown[i]):
                hit[i] += 1 + extra
                if stats is not None: stats["riposted"] += 1
            elif hushed[o] > 0:
                if stats is not None: stats["whiffed"] += 1
            else:
                hit[o] += 1 + extra
                fire[i] = queue[i][3]
                if stats is not None: stats["landed"] += 1
        if not any(landing):
            if beats(shown[0], shown[1]): win = 0
            elif beats(shown[1], shown[0]): win = 1
            else:
                t = [bonk[i] > 0 for i in range(2)]
                win = 0 if t[0] and not t[1] else 1 if t[1] and not t[0] else None
            if win is not None and shown[win] not in (DOZE, BRACE):
                lose = 1 - win
                missed = (dazzled[win] > 0 and rng.random() < 0.5) or hushed[lose] > 0
                if not missed: hit[lose] += 1
                if st[lose] == BRACE and queue[lose]:
                    if stats is not None: stats["staggered"] += 1
                    if queue[lose][1] == 2 and queue[lose][0] > 0: queue[lose][1] = 1
                    else: queue[lose] = None
        elif landing[0] != landing[1]:
            i = 0 if landing[0] else 1
            if st[1 - i] == BRACE and queue[1 - i]:   # both queued: the landing one hits a bracer; no stagger
                pass
        for i in range(2):
            p = fire[i]
            if p == "zap": hit[1 - i] += 1
            if p == "spores": dozing[1 - i] = True
            if p == "shell": shell[i] = True
            if p == "dash": bonk[i] = 2
            if p == "lantern": dazzled[1 - i] = 2
            if p == "hush": hushed[i] = 2
        for i in range(2):
            if hit[i] and shell[i]:
                hit[i] -= 1; shell[i] = False
            hearts[i] = max(0, hearts[i] - hit[i])
            if fire[i] == "mend": hearts[i] = min(maxh[i], hearts[i] + 1)
        for i in range(2):
            if queue[i] is not None:
                if landing[i]: queue[i] = None
                elif queue[i][0] > 0: queue[i][0] -= 1
            if not dozed[i]: spark[i] += 1
            if hit[i] and not hit[1 - i]: spark[i] += 1
            spark[i] = min(METER, spark[i])
        bonk = [max(0, x - 1) for x in bonk]; hushed = [max(0, x - 1) for x in hushed]; dazzled = [max(0, x - 1) for x in dazzled]
        hist[0].append(st[0]); hist[1].append(st[1])
        if hearts[0] == 0 or hearts[1] == 0:
            if hearts[0] == hearts[1]: return None, turn + 1
            return (0 if hearts[1] == 0 else 1), turn + 1
    if hearts[0] == hearts[1]: return None, MAX_TURNS
    return (0 if hearts[0] > hearts[1] else 1), MAX_TURNS


def plain(h): return [s for s in h if s in (0, 1, 2)]
def most(h, rng): return max(range(3), key=lambda s: (h.count(s), rng.random())) if h else rng.randrange(3)


def random_player(ctx, rng):
    q = ctx["theirQueue"]
    return rng.randrange(3), (rng.choice((1, 2)) if rng.random() < 0.7 else 0)


def habit(ctx, rng):
    sp, me = ctx["sp"], ctx["me"]
    s = sp[me][2] if rng.random() < 0.6 else rng.randrange(3)
    return s, 1


def reader(ctx, rng):
    """A parent: staggers a winding-up pet with Trick, guesses the landing from the friend's habit, and queues a read."""
    me, hist = ctx["me"], ctx["hist"]
    q = ctx["theirQueue"]
    theirs = plain(hist[1 - me])
    if q is not None and q[0] > 0: return TRICK, 0                       # it is bracing: stagger it
    if q is not None and q[0] == 0: return (most(theirs, rng) + 1) % 3, 0  # it lands now: counter their habit
    target = (most(theirs, rng) + 1) % 3
    return target, (2 if rng.random() < 0.5 else 1)


def follower(p):
    def play(ctx, rng):
        return reader(ctx, rng) if rng.random() < p else habit(ctx, rng)
    return play


def run(n, pa, pb, seed=1, assist=(0, 0)):
    rng = random.Random(seed)
    stats = dict(queued=0, landed=0, riposted=0, whiffed=0, staggered=0)
    overall = [[0.0, 0] for _ in SPECIES]
    table, turns = {}, []
    for a in range(9):
        for b in range(9):
            w = [0, 0, 0]
            for _ in range(n):
                win, t = battle(a, b, rng, (pa, pb), assist, stats)
                turns.append(t)
                w[2 if win is None else win] += 1
            table[a, b] = w
            overall[a][0] += w[0] + w[2] / 2; overall[a][1] += n
            overall[b][0] += w[1] + w[2] / 2; overall[b][1] += n
    return table, sorted(turns)[len(turns) // 2], overall, stats


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 800
    t, med, ov, stats = run(n, random_player, random_player)
    rate = lambda w, k: (w[0] + w[2] / 2) / k
    worst = max(abs(rate(w, n) - 0.5) for w in t.values())
    print(f"random v random, n={n}/pair, quick +{EXTRA[1]} heavy +{EXTRA[2]}: median turns {med}")
    print("  species overall:", " ".join(f"{SPECIES[i][0]}:{o[0]/o[1]:.3f}" for i, o in enumerate(ov)))
    print(f"  worst pair: {0.5 + worst:.3f}")
    q = max(stats["queued"], 1)
    print("  specials: " + ", ".join(f"{k} {v / q:.3f}" for k, v in stats.items() if k != "queued") + f" (of {stats['queued']} queued)")
    m = 200
    for label, kid in (("habit kid", habit), ("kid follows lean 50%", follower(0.5))):
        for steps in (0, 2):
            tt, _, _, _ = run(m, reader, kid, seed=2, assist=(0, steps))
            print(f"  parent(reader) v {label}, assist {steps}: parent wins {sum(rate(w, m) for w in tt.values()) / 81:.3f}")
