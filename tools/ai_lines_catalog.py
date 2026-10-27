#!/usr/bin/env python3
"""The facts the AI lines Worker (#37, web/src/) needs from the firmware, as one JSON file.

  python3 tools/ai_lines_catalog.py            write web/src/catalog.json
  python3 tools/ai_lines_catalog.py --check    exit 1 if it is stale

It holds: the screen's limits and font; every situation a pack may fill (the
thoughts.def rows, what each means from the pet-personality skill's
situations.md, which mark it gives, its shared line); each species' character
notes and built-in lines (personalities/*.txt, read by tools/personality.py);
and the id tables the genome decoder (web/src/genome.js) names things with.

Rows a pack never fills: the parrot (it says the owner's taught line) and the
teaching rows (instructions a kid needs word for word). Standard library only.
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import personality as P  # noqa: E402

ROOT = P.ROOT
OUT = ROOT / "web/src/catalog.json"
SITUATIONS_MD = ROOT / ".claude/skills/pet-personality/references/situations.md"
SKIP_ROWS = {"parrot", "teach_fight", "teach_shake", "teach_feed", "teach_hold", "grew_child", "grew_adult", "grew_elder"}
# The Magic 8-Ball's half of a shake (#34, species_verdicts.def): a pack may
# give the species-own yes / no / not-yet too. Built-in: the ball's plain ones.
VERDICTS = {
    "verdict_yes": ("The board was shaken as a question and the answer is yes: his own way of saying YES.", "YES."),
    "verdict_no": ("The board was shaken as a question and the answer is no: his own way of saying NO.", "NO."),
    "verdict_later": ("The board was shaken as a question and the answer is not yet: his own ASK AGAIN LATER.",
                      "ASK AGAIN LATER."),
}


def situation_meanings():
    out = {}
    for line in SITUATIONS_MD.read_text().splitlines():
        m = re.match(r"^\|\s*(\w+)\s*\|\s*(.+?)\s*\|\s*$", line)
        if m and m[1] not in ("situation",) and not set(m[1]) <= {"-"}:
            out[m[1]] = re.sub(r"\*\*#\*\*\s*", "", m[2])
    return out


def defs_rows(name, pattern):
    rows = []
    for line in (P.DEFS / name).read_text().splitlines():
        if m := re.match(pattern, line.strip()):
            rows.append(m)
    return rows


def id_tables():
    t = {}
    t["chemicals"] = {m[1]: m[2] for m in defs_rows("chemicals.def", r"BLORB_CHEM\(\s*(\d+),\s*(\w+)")}
    t["drives"] = {m[1]: m[2] for m in defs_rows("drives.def", r"BLORB_DRIVE\(\s*(\d+),\s*(\w+)")}
    t["stimuli"] = {m[1]: m[2] for m in defs_rows("stimuli.def", r"BLORB_STIM\(\s*(\d+),\s*(\w+)")}
    t["loci"] = {m[1]: m[2] for m in defs_rows("loci.def", r"BLORB_LOCUS\(\s*(\d+),\s*(\w+)")}
    t["actions"] = {m[1]: m[2] for m in defs_rows("actions.def", r"BLORB_ACTION\(\s*(\d+),\s*(\w+)")}
    t["regions"] = {m[1]: m[2] for m in defs_rows("regions.def", r"BLORB_REGION\(\s*(\d+),\s*(\w+)")}
    t["powers"] = {m[1]: m[2] for m in defs_rows("powers.def", r"BLORB_POWER\(\s*(\d+),\s*(\w+)")}
    t["voices"] = {m[1]: m[2] for m in defs_rows("voices.def", r"BLORB_VOICE\(\s*(\d+),\s*(\w+)")}
    t["geneKinds"] = {str(int(m[1], 16)): m[2] for m in defs_rows("gene_kinds.def", r"BLORB_GENE\(\s*0x([0-9A-Fa-f]+),\s*(\w+)")}
    t["species"] = {}
    for m in defs_rows("species.def", r"BLORB_SPECIES\(\s*(\d+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\w+)\)\s*(?://\s*(.*))?"):
        t["species"][m[1]] = {"name": m[2], "element": m[3].lower(), "style": m[4].lower(), "power": m[5],
                              "bold": int(m[6]), "nocturnal": m[7] == "1", "appetite": int(m[8]), "voice": m[9],
                              "sketch": m[10] or ""}
    return t


def build():
    engine = P.load_engine()
    problems = P.Problems()
    book = P.read_book(engine, problems)
    meanings = situation_meanings()
    situations = []
    for row in engine.rows:
        if row.name in SKIP_ROWS:
            continue
        mark = "#" if row.when in engine.numbered else ("@" if row.when.startswith("named") else None)
        situations.append({
            "name": row.name, "id": row.id, "when": row.when,
            "kind": "prophecy" if row.topic != "none" else "thought", "topic": row.topic,
            "meaning": meanings.get(row.when, row.when.replace("_", " ")),
            "mark": mark, "shared": book.shared.get(row.name, row.text),
        })
    for name, (meaning, plain) in VERDICTS.items():
        situations.append({"name": name, "id": None, "when": "shaken_question", "kind": "verdict", "topic": "none",
                           "meaning": meaning, "mark": None, "shared": plain})
    species = {}
    for name, sp in engine.species.items():
        src = (P.PERSONALITIES / f"{name}.txt").read_text(encoding="utf-8")
        notes = re.split(r"^\s*=+\s*LINES\s*=+\s*$", src, maxsplit=1, flags=re.M | re.I)[0].strip()
        # The template's own instructions to the owner are not character.
        notes = "\n\n".join(p for p in re.split(r"\n\s*\n", notes) if not p.startswith("Write anything above"))
        own = book.own.get(name, {})
        species[name] = {"id": sp.id, "voice": sp.voice, "sketch": sp.sketch, "notes": notes,
                         "lines": {k: v for k, v in own.items()}}
    return {
        "generatedBy": "tools/ai_lines_catalog.py",
        "maxText": P.MAX_TEXT,
        "font": "".join(sorted(P.FONT)),
        "situations": situations,
        "species": species,
        "tables": id_tables(),
    }


def main():
    text = json.dumps(build(), indent=1, ensure_ascii=False) + "\n"
    if "--check" in sys.argv:
        stale = not OUT.exists() or OUT.read_text() != text
        print("stale: run python3 tools/ai_lines_catalog.py" if stale else "ok")
        return 1 if stale else 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
