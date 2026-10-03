#!/usr/bin/env python3
# Scores a learnsim run directory against the learning-depth success criteria.
#   python3 score.py <out dir> [--json]
# Prints one table row per measure. With --json, prints the same numbers as
# JSON so two labels can be diffed.
import glob, json, math, os, statistics as st, sys

CAUSES = ["old_age", "starved", "injured", "poisoned", "unknown"]
ACTS = ["rest", "wander", "eat", "sleep", "foresee", "call", "curl", "follow_tilt", "flee_tilt", "chase", "hop_circles"]


def parse(path):
    run = {"deaths": [], "heirs": [], "picks": {}, "ticks": {}, "rot": {}, "cue": {}, "bites": {}, "hour": {},
           "drive": {}, "heirgenes": [], "garden": None}
    cur = run
    for line in open(path):
        if not line.startswith("M "):
            continue
        k, *a = line.split()[1:]
        if k == "garden_begin":
            cur = {"picks": {}, "ticks": {}, "rot": {}, "cue": {}, "bites": {}, "hour": {}, "drive": {},
                   "deaths": [], "heirs": []}
            run["garden"] = cur
        elif k == "garden_end":
            cur = run
        elif k == "run":
            run["style"], run["seed"], run["days"] = a[0], int(a[1]), int(a[2])
        elif k == "death":
            cur["deaths"].append({"gen": int(a[0]), "day": float(a[1]), "cause": CAUSES[int(a[2])], "n": int(a[4])})
        elif k == "heir":
            cur["heirs"].append({"gen": int(a[0]), "cue": a[1], "context": a[2] == "1", "action": a[3],
                                 "drive": a[4], "effect": float(a[5]), "hatch": float(a[6]), "base": float(a[7])})
        elif k in ("picks", "ticks"):
            cur[k][int(a[0])] = [int(x) for x in a[1:]]
        elif k == "rot":
            cur["rot"][int(a[0])] = [int(x) for x in a[1:]]
        elif k in ("cue", "bites"):
            cur[k][int(a[0])] = [int(x) for x in a[1:]]
        elif k == "hour":
            cur["hour"][int(a[0])] = [int(x) for x in a[1:]]
        elif k == "drive":
            cur["drive"][a[0]] = (float(a[1]), float(a[2]))
        elif k == "painhigh":
            cur["painhigh"] = float(a[0])
        elif k == "wakehunger":
            cur["wakehunger"] = float(a[0])
        elif k == "starving_by_food":
            cur.setdefault("sbf", {})[int(a[0])] = (int(a[1]), int(a[2]))
        elif k == "heirgene":
            run["heirgenes"].append(a[0] + ">" + a[1] + ">" + a[2])
        elif k == "end":
            run["endgen"] = int(a[0])
    return run


def load(d):
    runs = {}
    for p in sorted(glob.glob(os.path.join(d, "*.txt"))):
        scen = os.path.basename(p).split("_")[0]
        runs.setdefault(scen, []).append(parse(p))
    return runs


def share(rows, days, idx):
    tot = sum(sum(rows.get(d, [0])) for d in days)
    return sum(rows.get(d, [0] * 11)[idx] for d in days) / tot if tot else 0.0


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else float("nan")


def learned(h):
    # A belief this life built: a specific cue, and it moved away from what instinct alone would give.
    return not h["context"] and abs(h["effect"] - h["base"]) >= 0.1 and abs(h["effect"]) > abs(h["base"])


def score(runs):
    m = {}
    life = [r for r in runs.get("life", []) if r["style"] in ("rich", "doting", "quiet")]
    first = [(r, [d for d in r["deaths"] if d["gen"] == 0]) for r in life]
    deaths = [d[0] for _, d in first if d]
    heirs = [h for r in life for h in r["heirs"] if h["gen"] == 0]
    m["lives"] = len(life)
    m["life_death_day"] = mean(d["day"] for d in deaths)
    m["life_death_min"] = min((d["day"] for d in deaths), default=float("nan"))
    m["life_causes"] = {c: sum(d["cause"] == c for d in deaths) for c in CAUSES if any(d["cause"] == c for d in deaths)}
    m["life_unfinished"] = sum(1 for _, d in first if not d)
    m["heir_lives_with_any"] = sum(1 for d in deaths if d["n"] > 0)
    m["heir_per_life"] = mean(d["n"] for d in deaths)
    m["heir_total"] = len(heirs)
    m["heir_context"] = sum(h["context"] for h in heirs)
    m["heir_learned"] = sum(learned(h) for h in heirs)
    m["heir_examples"] = sorted({"%s>%s>%s %+.2f (instinct %+.2f)" % (h["cue"], h["action"], h["drive"], h["effect"],
                                                                     h["base"]) for h in heirs})[:12]

    def callshare(style):
        rs = [r for r in runs.get("life", []) if r["style"] == style]
        return [share(r["picks"], range(1, 9), ACTS.index("call")) for r in rs]
    tr, ct = callshare("trainer"), callshare("rich")
    m["reward_call_share_trainer"] = mean(tr)
    m["reward_call_share_control"] = mean(ct)
    m["reward_ratio"] = mean(tr) / mean(ct) if ct and mean(ct) else float("nan")
    m["reward_seed_wins"] = "%d/%d" % (sum(a > b for a, b in zip(tr, ct)), len(tr))
    pun = [share(r["picks"], range(1, 6), ACTS.index("chase")) for r in runs.get("punish", [])]
    ctl = [share(r["picks"], range(1, 6), ACTS.index("chase")) for r in runs.get("life", []) if r["style"] == "rich"]
    m["punish_chase_ratio"] = mean(pun) / mean(ctl) if ctl and mean(ctl) else float("nan")
    # Time shares, which interrupts cannot inflate the way they inflate switch-ins.
    trt = [share(r["ticks"], range(1, 9), ACTS.index("call")) for r in runs.get("life", []) if r["style"] == "trainer"]
    ctt = [share(r["ticks"], range(1, 9), ACTS.index("call")) for r in runs.get("life", []) if r["style"] == "rich"]
    m["reward_call_time_ratio"] = mean(trt) / mean(ctt) if ctt and mean(ctt) else float("nan")
    m["reward_time_seed_wins"] = "%d/%d" % (sum(a > b for a, b in zip(trt, ctt)), len(trt))
    pt = [share(r["ticks"], range(1, 6), ACTS.index("chase")) for r in runs.get("punish", [])]
    ct = [share(r["ticks"], range(1, 6), ACTS.index("chase")) for r in runs.get("life", []) if r["style"] == "rich"]
    m["punish_chase_time_ratio"] = mean(pt) / mean(ct) if ct and mean(ct) else float("nan")

    def cue(style, days):
        rs = [r for r in runs.get("cue", []) if r["style"] == style]
        cues = sum(r["cue"].get(d, [0, 0])[0] for r in rs for d in days)
        hops = sum(r["cue"].get(d, [0, 0])[1] for r in rs for d in days)
        return hops / cues if cues else float("nan")
    m["cue_hop_rate_trained_late"] = cue("cuetrainer", range(3, 6))
    m["cue_hop_rate_control_late"] = cue("cuecontrol", range(3, 6))
    m["cue_hop_rate_trained_early"] = cue("cuetrainer", range(0, 2))

    def rot(days):
        rs = runs.get("rot", [])
        f = [sum(r["rot"].get(d, [0] * 4)[i] for r in rs for d in days) for i in range(4)]
        pf = f[1] / (f[0] + f[1]) if f[0] + f[1] else float("nan")
        pr = f[3] / (f[2] + f[3]) if f[2] + f[3] else float("nan")
        bites = sum(r["bites"].get(d, [0, 0])[0] for r in rs for d in days)
        bad = sum(r["bites"].get(d, [0, 0])[1] for r in rs for d in days)
        return pf, pr, (bad / bites if bites else float("nan")), f[2] + f[3]
    e, l = rot(range(0, 2)), rot(range(4, 8))
    m["rot_p_eat_fresh_early"], m["rot_p_eat_rotten_early"], m["rot_bad_bite_share_early"], _ = e
    m["rot_p_eat_fresh_late"], m["rot_p_eat_rotten_late"], m["rot_bad_bite_share_late"], m["rot_late_rotten_decisions"] = l
    m["rot_late_ratio"] = l[1] / l[0] if l[0] else float("nan")

    # Time: picks per action in the two hours before each awake meal vs the other waking hours.
    tr_ = runs.get("time", [])
    pre, other = [11, 12, 17, 18], [10, 14, 15, 16, 20]
    def rate(hours, i):
        return sum(r["hour"].get(h, [0] * 12)[i] for r in tr_ for h in hours) / len(hours)
    m["time_eat_nofood_pre"] = rate(pre, 11)
    m["time_eat_nofood_other"] = rate(other, 11)
    m["time_anticipation_ratio"] = (m["time_eat_nofood_pre"] / m["time_eat_nofood_other"]
                                    if m["time_eat_nofood_other"] else float("nan"))
    ctl = runs.get("timectl", [])
    def rate_in(rs, hours, i):
        return sum(r["hour"].get(h, [0] * 12)[i] for r in rs for h in hours) / max(1, len(hours) * len(rs))
    awake = [10, 11, 12, 14, 15, 16, 17, 18, 20]
    def premeal_share(rs):
        tot = rate_in(rs, awake, 11) * len(awake)
        return rate_in(rs, [12, 18], 11) * 2 / tot if tot else float("nan")
    m["time_premeal_share_routine"] = premeal_share(tr_)
    m["time_premeal_share_random"] = premeal_share(ctl)
    m["time_routine_vs_random"] = (m["time_premeal_share_routine"] / m["time_premeal_share_random"]
                                   if m["time_premeal_share_random"] else float("nan"))
    best = max(range(11), key=lambda i: rate(pre, i) / max(1e-9, rate(other, i)) if rate(other, i) > 5 else 0)
    m["time_best_action"] = "%s %.2fx" % (ACTS[best], rate(pre, best) / max(1e-9, rate(other, best)))

    # Divergence: the common garden. Each lineage's final genome hatched under one owner and seed.
    lin = [r for r in runs.get("lineage", []) if r["garden"]]
    vecs = []
    for r in lin:
        g = r["garden"]["ticks"]
        tot = [sum(g.get(d, [0] * 11)[i] for d in g if d >= 1) for i in range(11)]
        s = sum(tot)
        vecs.append((r["style"], [x / s for x in tot] if s else tot))
    def l1(a, b):
        return sum(abs(x - y) for x, y in zip(a, b))
    within = [l1(a[1], b[1]) for i, a in enumerate(vecs) for b in vecs[i + 1:] if a[0] == b[0]]
    between = [l1(a[1], b[1]) for i, a in enumerate(vecs) for b in vecs[i + 1:] if a[0] != b[0]]
    m["diverge_within"] = mean(within)
    m["diverge_between"] = mean(between)
    m["diverge_ratio"] = mean(between) / mean(within) if within and mean(within) else float("nan")
    m["diverge_generations"] = mean(r.get("endgen", 0) for r in runs.get("lineage", []))
    by = {}
    for r in runs.get("lineage", []):
        by.setdefault(r["style"], []).append(len(r["heirgenes"]))
    m["diverge_heirgenes_by_style"] = {k: mean(v) for k, v in sorted(by.items())}
    sig = {}
    for st_, v in vecs:
        sig.setdefault(st_, []).append(v)
    m["garden_profile"] = {k: " ".join("%s=%.2f" % (ACTS[i], mean(x[i] for x in vs)) for i in range(11)
                                       if mean(x[i] for x in vs) >= 0.04) for k, vs in sorted(sig.items())}

    # Balance.
    care = life
    m["boredom_low_share"] = mean(r["drive"]["boredom"][1] for r in care)
    m["boredom_mean"] = mean(r["drive"]["boredom"][0] for r in care)
    m["pain_mean"] = mean(r["drive"]["pain"][0] for r in care)
    m["pain_high_share"] = mean(r.get("painhigh", 0) for r in care)
    m["wake_hunger"] = mean(r.get("wakehunger", 0) for r in care)
    m["loneliness_low_share"] = mean(r["drive"]["loneliness"][1] for r in care)
    m["need_touch_low_share"] = mean(r["drive"]["need_touch"][1] for r in care)
    for stage, name in ((0, "baby"), (2, "adult")):
        num = sum(r.get("sbf", {}).get(stage, (0, 0))[0] for r in care)
        den = sum(r.get("sbf", {}).get(stage, (0, 0))[1] for r in care)
        m["hungry_beside_food_" + name] = num / den if den else float("nan")
    neg = [r["deaths"][0] for r in runs.get("neglect", []) if r["deaths"]]
    m["neglect_deaths"] = "%d/%d" % (len(neg), len(runs.get("neglect", [])))
    m["neglect_death_day"] = mean(d["day"] for d in neg)
    m["neglect_causes"] = {c: sum(d["cause"] == c for d in neg) for c in CAUSES if any(d["cause"] == c for d in neg)}
    m["decision_spread"] = " ".join("%s=%.2f" % (ACTS[i], mean(share(r["picks"], range(1, 9), i) for r in care))
                                    for i in range(11))
    return m


if __name__ == "__main__":
    res = score(load(sys.argv[1]))
    if "--json" in sys.argv:
        print(json.dumps(res, indent=1, default=str))
    else:
        for k, v in res.items():
            print("%-32s %s" % (k, ("%.3f" % v) if isinstance(v, float) else v))
