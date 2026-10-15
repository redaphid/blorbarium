#!/usr/bin/env python3
"""The pets' lines, from personalities/*.txt to the C tables. Standard library only.

  python3 tools/personality.py check            find mistakes, file:line
  python3 tools/personality.py say grungo       every line grungo can say, as the screen says it
  python3 tools/personality.py say all
  python3 tools/personality.py build            check, then write the tables

What it owns: the text column of thoughts.def (from personalities/_shared.txt),
all of species_lines.def (from personalities/<species>.txt) and the parity
fixture test/test_thoughts/said_lines.inc. Ids, situations, priorities and
cooldowns stay hand-written in thoughts.def.
"""
import difflib
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFS = ROOT / "lib/blorb/include/blorb/defs"
THOUGHTS_DEF = DEFS / "thoughts.def"
SPECIES_LINES_DEF = DEFS / "species_lines.def"
PARITY_FIXTURE = ROOT / "test/test_thoughts/said_lines.inc"
PERSONALITIES = ROOT / "personalities"
SHARED = "_shared"

FONT = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?-':")
# Typing on a laptop produces these; the screen has a plain twin for each.
FRIENDLY = {"’": "'", "‘": "'", "…": "...", "–": "-", "—": "-"}
MARKER = re.compile(r"^\s*=+\s*LINES\s*=+\s*$", re.I)
# One mark per line, each given by some situations only: # a number, @ his
# name (situations named named...), * a line the owner taught him (taught).
MARKS = "#@*"
# What `say` and the parity fixture put in place of @ and *.
PARITY_NAME, PARITY_TAUGHT = "MOSS BOY", "I LOVE YOU, LITTLE ONE"


def _limits():
    h = (ROOT / "lib/blorb/include/blorb/thoughts.h").read_text()
    text, frame = (int(re.search(rf"{k} = (\d+);", h)[1]) for k in ("kMaxThoughtText", "kMaxFrameText"))
    return text, text + frame + (text - 1) + 1   # kThoughtLineCap: '*' can grow to a taught line (kMaxTaughtText)


MAX_TEXT, LINE_CAP = _limits()


@dataclass
class Row:
    id: int
    name: str
    topic: str
    when: str
    priority: int
    cooldown: int
    text: str


@dataclass
class Voice:
    id: int
    name: str
    first_only: bool
    frames: list = field(default_factory=list)


@dataclass
class Species:
    id: int
    name: str
    voice: str
    sketch: str


@dataclass
class Engine:
    rows: list
    voices: dict
    species: dict
    voiced: list          # (thought, voice, text) from thought_lines.def
    numbered: set         # situations that give a number for '#'

    def row(self, name):
        return next((r for r in self.rows if r.name == name), None)


def _strings(s):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', s)


def load_engine():
    rows = []
    for line in THOUGHTS_DEF.read_text().splitlines():
        m = re.match(r"BLORB_THOUGHT\(\s*(\d+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\d+),\s*(\d+),\s*\"(.*)\"\)\s*$", line)
        if m:
            rows.append(Row(int(m[1]), m[2], m[3], m[4], int(m[5]), int(m[6]), m[7]))
    voices = {}
    for line in (DEFS / "voices.def").read_text().splitlines():
        if m := re.match(r"BLORB_VOICE\((\d+),\s*(\w+),\s*(\d)\)", line):
            voices[m[2]] = Voice(int(m[1]), m[2], m[3] == "1")
        elif m := re.match(r"BLORB_FRAME\((\w+),", line):
            voices[m[1]].frames.append(tuple(_strings(line)))
    species = {}
    for line in (DEFS / "species.def").read_text().splitlines():
        if m := re.match(r"BLORB_SPECIES\((\d+),\s*(\w+),(?:[^,]*,){6}\s*(\w+)\)\s*(?://\s*(.*))?", line):
            species[m[2]] = Species(int(m[1]), m[2], m[3], m[4] or "")
    voiced = []
    for line in (DEFS / "thought_lines.def").read_text().splitlines():
        if m := re.match(r"BLORB_LINE\((\w+),\s*(\w+),", line):
            voiced.append((m[1], m[2], _strings(line)[0]))
    cpp = (ROOT / "lib/blorb/src/thoughts.cpp").read_text()
    numbered = set(re.findall(r"bool (\w+)\(const Observed& o, uint16_t& num\)", cpp))
    return Engine(rows, voices, species, voiced, numbered)


# ---- the source files --------------------------------------------------------------

@dataclass
class Said:
    path: str
    line: int
    row: str
    text: str


class Problems(list):
    def add(self, path, line, msg):
        self.append(f"{path}:{line}: {msg}")


def clean_text(raw, path, n, row, engine, problems):
    text = raw.strip()
    for k, v in FRIENDLY.items():
        text = text.replace(k, v)
    text = text.upper()
    bad = sorted({c for c in text if c not in FONT and c not in MARKS})
    if bad:
        shown = " ".join(repr(c) for c in bad)
        hint = " (use ' for quotes)" if '"' in bad else ""
        problems.add(path, n, f"the screen cannot draw {shown}{hint}. It has A-Z, 0-9, space and . , ! ? - ' :")
    if len(text) > MAX_TEXT:
        over = len(text) - MAX_TEXT
        problems.add(path, n, f"too long by {over}: {len(text)} characters, the screen holds {MAX_TEXT}. \"{text}\"")
    if text.count("#") > 1:
        problems.add(path, n, "only one '#' per line (it is the one number this situation gives)")
    elif sum(text.count(m) for m in MARKS) > 1:
        problems.add(path, n, "only one of # @ * per line")
    if "#" in text and row and row.when not in engine.numbered:
        problems.add(path, n, f"'#' shows a number, but '{row.name}' ({row.when}) gives none. "
                              f"Numbers come with: {', '.join(sorted(engine.numbered))}")
    if "@" in text and row and not row.when.startswith("named"):
        problems.add(path, n, f"'@' shows his name, but '{row.name}' ({row.when}) does not know the owner named him. "
                              f"Names come with situations that start named")
    if "*" in text and row and not row.when.startswith("taught"):
        problems.add(path, n, f"'*' shows a line the owner taught him, and '{row.name}' ({row.when}) has none. "
                              f"Only the taught situation has one")
    return text


def resolve(word, engine, path, n, problems):
    row = engine.row(word)
    if row:
        return row
    fits = [r for r in engine.rows if r.when == word]
    if len(fits) == 1:
        return fits[0]
    if fits:
        problems.add(path, n, f"'{word}' is the situation of several lines; head the section with one of: "
                              f"{', '.join(r.name for r in fits)}")
        return None
    names = [r.name for r in engine.rows] + sorted({r.when for r in engine.rows})
    close = difflib.get_close_matches(word, names, n=1, cutoff=0.5)
    hint = f" Did you mean '{close[0]}'?" if close else ""
    problems.add(path, n, f"no situation or line called '{word}'.{hint} "
                          "(A line he says must be indented under its situation.)")
    return None


def parse_source(path, text, engine, problems):
    """The lines under the LINES marker: an unindented word heads a section, indented lines are said."""
    said = []
    lines = text.splitlines()
    start = next((i + 1 for i, l in enumerate(lines) if MARKER.match(l)), None)
    if start is None:
        problems.add(path, 1, "no '=== LINES ===' line, so nothing here is said. Put it above the first situation")
        return said
    row, header_ok = None, False
    for i in range(start, len(lines)):
        n, raw = i + 1, lines[i]
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if raw[0] not in " \t":
            row = resolve(raw.split()[0].lower(), engine, path, n, problems)
            header_ok = True
            continue
        if not header_ok:
            problems.add(path, n, "a line with no situation above it")
            continue
        text = clean_text(raw, path, n, row, engine, problems)
        if row:
            said.append(Said(path, n, row.name, text))
    return said


@dataclass
class Book:
    shared: dict          # row -> text
    own: dict             # species -> {row: [texts]}


def read_book(engine, problems, root=PERSONALITIES):
    shared, own, theirs = {}, {}, []
    for f in sorted(root.glob("*.txt")):
        rel = f.relative_to(ROOT).as_posix() if f.is_relative_to(ROOT) else f.name
        said = parse_source(rel, f.read_text(encoding="utf-8"), engine, problems)
        if f.stem == SHARED:
            for s in said:
                if s.row in shared:
                    problems.add(s.path, s.line, f"'{s.row}' already has its shared line; every pet shares exactly one. "
                                                 "Give a species more in its own file")
                shared[s.row] = s.text
        elif f.stem in engine.species:
            for s in said:
                own.setdefault(f.stem, {}).setdefault(s.row, []).append(s.text)
                theirs.append((f.name, s))
        else:
            close = difflib.get_close_matches(f.stem, engine.species, n=1)
            problems.add(rel, 1, f"'{f.stem}' is not a species ({', '.join(engine.species)})."
                                 + (f" Did you mean {close[0]}.txt?" if close else ""))
    for r in engine.rows:
        if r.name not in shared:
            problems.add(f"personalities/{SHARED}.txt", 1, f"'{r.name}' has no shared line")
    # A species line is his own: one that repeats the shared line or another
    # species' line says nothing the others don't.
    first = {t: f"{SHARED}.txt" for t in shared.values()}
    for name, s in theirs:
        if first.setdefault(s.text, name) != name:
            problems.add(s.path, s.line, f"\"{s.text}\" is already said in {first[s.text]}; give him his own")
    return Book(shared, own)


# ---- saying it, as thoughtLine() in lib/blorb/src/thoughts.cpp does ----------------

def say_line(engine, book, row, num, voice, pick, species, name=PARITY_NAME, taught=PARITY_TAUGHT):
    text = book.shared.get(row.name, row.text)
    mine = book.own.get(species, {}).get(row.name, [])
    if mine:
        text = mine[pick % len(mine)]
        pick //= len(mine)
    own = [] if mine else [t for (th, v, t) in engine.voiced if th == row.name and v == voice]
    if own:
        text = own[pick % len(own)]
    v = engine.voices[voice]
    frame = None if own else v.frames[pick % len(v.frames)]
    out = []

    def put(s):
        for c in s:
            if len(out) + 1 < LINE_CAP:
                out.append(c)

    if frame:
        put(frame[0])
    body = len(out)
    put(text.replace("#", str(num)).replace("@", name).replace("*", taught))
    if v.first_only and not own and "*" not in text:
        for i in range(body, len(out) - 1):
            if out[i] in ".!?" and out[i + 1] == " " and (i == body or out[i - 1] != "."):
                del out[i + 1:]
                break
    if frame:
        suffix = frame[1]
        if len(out) > body and out[-1] in ".!?":
            suffix = suffix.lstrip(".,")
        put(suffix)
    return "".join(out)


def variants(engine, book, row, voice, species, num=3):
    seen = []
    for pick in range(256):
        s = say_line(engine, book, row, num, voice, pick, species)
        if s not in seen:
            seen.append(s)
    return seen


def cmd_say(engine, book, who):
    names = list(engine.species) if who == "all" else [who]
    for name in names:
        if name not in engine.species:
            close = difflib.get_close_matches(name, engine.species, n=1)
            sys.exit(f"no species '{name}'." + (f" Did you mean {close[0]}?" if close else ""))
        sp = engine.species[name]
        print(f"=== {name.upper()}: {sp.sketch}. Voice: {sp.voice} ===")
        print(f"    (* his own line; # shows as 3, @ as {PARITY_NAME}, a taught line as {PARITY_TAUGHT})\n")
        for row in engine.rows:
            kind = f"prophecy about {row.topic}, {row.when}" if row.topic != "none" else row.when
            print(f"{row.name}  [{kind}]")
            mine = set(book.own.get(name, {}).get(row.name, []))
            for s in variants(engine, book, row, sp.voice, name):
                said = (m.replace("#", "3").replace("@", PARITY_NAME).replace("*", PARITY_TAUGHT) for m in mine)
                star = "*" if any(m in s for m in said) else " "
                print(f"  {star} {s}")
        print()


# ---- writing the tables ------------------------------------------------------------

def render_thoughts_def(book):
    out = []
    for line in THOUGHTS_DEF.read_text().splitlines(keepends=True):
        m = re.match(r'(BLORB_THOUGHT\(\s*\d+,\s*(\w+),.*?)"(?:[^"\\]|\\.)*"(\)\s*)$', line, re.S)
        out.append(f'{m[1]}"{book.shared[m[2]]}"{m[3]}' if m else line)
    return "".join(out)


def render_species_lines(engine, book):
    out = ["// Generated by tools/personality.py from personalities/<species>.txt. Do not edit:",
           "// edit those files and run `python3 tools/personality.py build`.",
           "// A species' lines for a row stand in for the shared line (thoughts.h).",
           "// BLORB_SPECIES_LINE(speciesId, species, thought, text)"]
    for name, sp in engine.species.items():
        for row in engine.rows:
            for text in book.own.get(name, {}).get(row.name, []):
                out.append(f'BLORB_SPECIES_LINE({sp.id}, {name}, {row.name}, "{text}")')
    return "\n".join(out) + "\n"


NO_SPECIES = 0xFE   # kSharedOnly in thoughts.h


def render_parity(engine, book):
    out = ["// Generated by tools/personality.py: what its Python port of thoughtLine() says.",
           "// test_thoughts holds the C++ to the same text, so `say` never lies.",
           f"// @ is {PARITY_NAME} and * is {PARITY_TAUGHT} (kParityWords in the test).",
           "// {species, thought, voice, pick, num, text}"]
    cases = [(NO_SPECIES, None)] + [(engine.species[s].id, s) for s in book.own]
    for sid, sname in cases:
        for v in engine.voices.values():
            for row in engine.rows:
                if sname and row.name not in book.own[sname]:
                    continue
                seen = set()
                for pick in range(6):
                    s = say_line(engine, book, row, 7, v.name, pick, sname)
                    if s not in seen:
                        seen.add(s)
                        out.append(f'{{{sid}, {row.id}, {v.id}, {pick}, 7, "{s}"}},')
    return "\n".join(out) + "\n"


def outputs(engine, book):
    return {THOUGHTS_DEF: render_thoughts_def(book),
            SPECIES_LINES_DEF: render_species_lines(engine, book),
            PARITY_FIXTURE: render_parity(engine, book)}


def checked():
    engine = load_engine()
    problems = Problems()
    book = read_book(engine, problems)
    return engine, book, problems


def main(argv):
    if len(argv) < 2 or argv[1] not in ("check", "say", "build"):
        sys.exit(__doc__)
    engine, book, problems = checked()
    for p in problems:
        print(p)
    if problems:
        sys.exit(f"\n{len(problems)} to fix. Nothing was written.")
    if argv[1] == "check":
        lines = sum(len(v) for own in book.own.values() for v in own.values())
        print(f"ok: {len(book.shared)} shared lines, {lines} species lines in {len(book.own)} species")
    elif argv[1] == "say":
        cmd_say(engine, book, argv[2] if len(argv) > 2 else "all")
    else:
        for path, text in outputs(engine, book).items():
            changed = not path.exists() or path.read_text() != text
            path.write_text(text, newline="\n")
            print(f"{'wrote' if changed else 'same '} {path.relative_to(ROOT).as_posix()}")


if __name__ == "__main__":
    main(sys.argv)
