"""Generate Grungo's dialogue table with a small base (non-instruct) model.

Each prompt is a page of "The Field Notebook of a Pond Seer": a word cloud and
seed sayings in one voice (or two crossed voices) about one topic, ending on an
open numbered item for the model to continue. Completions are cached in
raw.jsonl, so a rerun only asks the model for pages it has not seen.

    python tools/dialogue/generate.py            # stdlib only; Ollama via ollama-proxy
"""
import difflib
import hashlib
import json
import os
import random
import re
import subprocess
import sys
import time
import tomllib
import urllib.error
import urllib.request
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blocklist import blocked  # noqa: E402

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
DEFS = "lib/blorb/include/blorb/defs"
DEF_OUT = REPO / DEFS / "thought_lines.def"
# The thoughts registry (thoughts.def, voices.def) this table is keyed by: the
# files in this tree once merged, else this ref.
THOUGHTS_REF = os.environ.get("THOUGHTS_REF", "origin/thoughts")
VOICED_LINE_BYTES = 8   # VoicedLine on the ESP32: two one-byte ids, padding, a pointer
SAMPLES_OUT = HERE / "samples.md"
RAW = HERE / "raw.jsonl"
DRAW_CPP = REPO / "lib/paint/src/draw.cpp"

# ollama-proxy's front door: it 503s while the GPU is busy and we wait it out.
OLLAMA = os.environ.get("OLLAMA_URL", "http://localhost:11434")
MODEL = "llama3.2:3b-text-q8_0"
OPTIONS = {"temperature": 1.15, "top_p": 0.95, "top_k": 100, "repeat_penalty": 1.1, "num_predict": 200, "stop": ["\n\n"]}
PURE_SEEDS = tuple(range(1, 9))
BLEND_SEEDS = (1, 2, 3, 4)
SEEDS_PER_PAGE = 8
FLAVOUR_PER_PAGE = 5
CAP_PURE, CAP_BLEND = 12, 8
MIN_CHARS, MAX_CHARS = 6, 32   # the marquee scrolls; 32 keeps a line to about two screen-widths
NEAR_DUP = 0.78
FIRST_PERSON = {"I", "I'M", "I'LL", "I'VE", "I'D", "ME", "MY", "MYSELF"}
SAMPLE_RNG = 2026


@dataclass(frozen=True)
class Voice:
    name: str
    id: int
    title: str
    cloud: tuple
    general: tuple
    seeds: dict
    flavour: dict
    registry: str


@dataclass(frozen=True)
class Topic:
    name: str
    id: int
    title: str
    cloud: tuple
    rows: tuple


@dataclass(frozen=True)
class Job:
    voices: tuple   # one name for a pure voice, two for a hybrid
    topic: str
    seed: int

    @property
    def cell(self):
        a = self.voices[0]
        return (a, self.voices[-1], self.topic)


def load_corpus():
    d = tomllib.loads((HERE / "voices.toml").read_text(encoding="utf-8"))
    voices = {n: Voice(n, v["id"], v["title"], tuple(v["cloud"]), tuple(v["general"]), v["seeds"], v["flavour"],
                       v.get("registry", n))
              for n, v in d["voices"].items()}
    topics = {n: Topic(n, t["id"], t["title"], tuple(t["cloud"]), tuple(t.get("rows", ())))
              for n, t in d["topics"].items()}
    blends = [tuple(p) for p in d["blends"]["pairs"]]
    markers = re.compile("|".join(d["seasoning"]["markers"]), re.I)
    return voices, topics, blends, markers


def load_lexicon():
    d = json.loads((HERE / "lexicon.json").read_text(encoding="utf-8"))
    groups = dict(d["words"])
    groups["organic"] = d["organic"]
    return groups


def device_glyphs():
    """The marquee font's characters, read from the renderer itself."""
    src = DRAW_CPP.read_text(encoding="utf-8")
    font = src[src.index("kFont[]"):src.index("glyphFor")]
    return {c[-1] for c in re.findall(r"\{'(\\.|[^\\'])'", font)}


def rng_for(*parts):
    return random.Random(zlib.crc32("|".join(map(str, parts)).encode()))


def flavour_words(voices, lexicon, rng, k):
    weights = {}
    for v in voices:
        for g, w in v.flavour.items():
            if g in lexicon:
                weights[g] = weights.get(g, 0) + w
    max_len = min((v.flavour.get("max_len", 99) for v in voices), default=99)
    groups = list(weights)
    picked = []
    while len(picked) < k and groups:
        g = rng.choices(groups, [weights[x] for x in groups])[0]
        word = rng.choice(lexicon[g])
        if len(word) <= max_len and word not in picked:
            picked.append(word)
    return picked


def interleave(*seqs):
    out = []
    for i in range(max(map(len, seqs))):
        out += [s[i] for s in seqs if i < len(s)]
    return out


def page(job, voices, topics, lexicon):
    """The document the model continues. Same job, same page."""
    rng = rng_for(*job.voices, job.topic, job.seed)
    vs = [voices[n] for n in job.voices]
    topic = topics[job.topic]
    per = 8 // len(vs)
    clouds = [rng.sample(v.cloud, min(per, len(v.cloud))) for v in vs]
    cloud = interleave(*clouds) + rng.sample(topic.cloud, 6) + flavour_words(vs, lexicon, rng, FLAVOUR_PER_PAGE)
    rng.shuffle(cloud)
    if len(vs) == 1:
        v = vs[0]
        on_topic = list(v.seeds[job.topic])
        seeds = on_topic + rng.sample(v.general, SEEDS_PER_PAGE - len(on_topic))
        rng.shuffle(seeds)
        voice_line = f"Voice: {v.title}."
    else:
        tops = [rng.sample(v.seeds[job.topic], len(v.seeds[job.topic])) for v in vs]
        gens = [rng.sample(v.general, 1) for v in vs]
        seeds = interleave(*tops)[:SEEDS_PER_PAGE - 2] + interleave(*gens)
        voice_line = f"Voice: {vs[0].title}, crossed in the egg with {vs[1].title} (a mutant tadpole)."
    volume = ["I", "II", "III", "IV"][job.seed % 4]
    body = "\n".join(f"{i}. {s}" for i, s in enumerate(seeds, 1))
    return (
        "THE FIELD NOTEBOOK OF A POND SEER\n"
        "Being the sayings of Grungo, frog settler of the sinking station, taken down word for word\n"
        "by the Spore Wardens. He calls himself \"grungo\" and never \"I\". Each saying is short.\n\n"
        f"Volume {volume}. Chapter: {topic.title}.\n"
        f"{voice_line}\n"
        f"Words heard in the bubbles this week: {', '.join(cloud)}.\n\n"
        "Sayings, numbered as heard:\n\n"
        f"{body}\n{len(seeds) + 1}."
    )


def job_key(prompt, seed):
    blob = json.dumps([MODEL, OPTIONS, seed, prompt], sort_keys=True)
    return hashlib.sha1(blob.encode()).hexdigest()


def complete(prompt, seed):
    """Raw completion, no chat template. Waits out the proxy's GPU gate."""
    body = json.dumps({"model": MODEL, "prompt": prompt, "raw": True, "stream": False,
                       "options": {**OPTIONS, "seed": seed}}).encode()
    failures = 0
    while True:
        req = urllib.request.Request(f"{OLLAMA}/api/generate", body, {"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code != 503:
                raise
            wait = max(5, min(300, int(e.headers.get("Retry-After") or 30)))
            print(f"  gpu gate shut, retrying in {wait}s", flush=True)
            time.sleep(wait)
        except (urllib.error.URLError, TimeoutError) as e:
            failures += 1
            if failures > 8:
                raise
            print(f"  {e}; retry {failures}", flush=True)
            time.sleep(10 * failures)


def model_digest():
    with urllib.request.urlopen(f"{OLLAMA}/api/tags", timeout=30) as r:
        return next(m["digest"] for m in json.load(r)["models"] if m["name"] == MODEL)


def load_raw():
    if not RAW.exists():
        return {}
    rows = (json.loads(l) for l in RAW.read_text(encoding="utf-8").splitlines() if l.strip())
    return {r["key"]: r for r in rows}


def run_jobs(jobs, voices, topics, lexicon):
    raw = load_raw()
    todo = [(j, page(j, voices, topics, lexicon)) for j in jobs]
    todo = [(j, p, job_key(p, j.seed)) for j, p in todo]
    missing = [t for t in todo if t[2] not in raw]
    print(f"{len(todo)} pages, {len(missing)} to generate")
    if os.environ.get("DIALOGUE_OFFLINE"):
        print("  offline: using cached pages only")
        todo, missing = [t for t in todo if t[2] in raw], []
    digest = model_digest() if missing else None
    with RAW.open("a", encoding="utf-8", newline="\n") as out:
        for n, (j, p, k) in enumerate(missing, 1):
            r = complete(p, j.seed)
            row = {"key": k, "model": MODEL, "digest": digest, "options": OPTIONS, "seed": j.seed,
                   "voices": list(j.voices), "topic": j.topic, "completion": r["response"],
                   "done_reason": r.get("done_reason")}
            out.write(json.dumps(row) + "\n")
            out.flush()
            raw[k] = row
            print(f"  [{n}/{len(missing)}] {'+'.join(j.voices)} {j.topic} s{j.seed}", flush=True)
    return [(j, raw[k]) for j, _, k in todo]


def sayings(row):
    """The numbered items the model wrote, complete lines only."""
    lines = row["completion"].split("\n")
    if row.get("done_reason") == "length":
        lines = lines[:-1]
    out = [lines[0]] if lines else []
    for line in lines[1:]:
        m = re.match(r"\s*\d+\.\s*(.*)$", line)
        if not m:
            break
        out.append(m.group(1))
    return [s.strip() for s in out if s.strip()]


FOLD = str.maketrans({"‘": "'", "’": "'", "`": "'", "“": None, "”": None, '"': None,
                      "–": "-", "—": "-", ";": ",", "*": None, "(": None, ")": None,
                      "[": None, "]": None, "_": " ", "~": None})


def fit(t):
    """The longest run of leading whole sentences that fits the cap."""
    if len(t) <= MAX_CHARS:
        return t
    cuts = [m.end() for m in re.finditer(r"[.!?]+(?= )", t) if m.end() <= MAX_CHARS]
    return t[:cuts[-1]] if cuts else t


def clean(text, glyphs):
    """A device-ready line, or None when it cannot be one."""
    t = text.replace("…", "...").translate(FOLD).upper()
    t = fit(re.sub(r"\s+", " ", t).strip(" -,"))
    if not (MIN_CHARS <= len(t) <= MAX_CHARS) or any(c not in glyphs for c in t) or t[-1] in ":,-":
        return None
    words = re.findall(r"[A-Z0-9']+", t)
    if len(words) < 2 or FIRST_PERSON & set(words) or blocked(t):
        return None
    if re.search(r"(.)\1{5,}", t) or re.search(r"[.,!?:'-]{4,}", t.replace("...", "")):
        return None
    if sum(c.isalpha() for c in t) < 0.6 * len(t) or max(map(len, words)) > 14:
        return None
    return t


def norm(t):
    return re.sub(r"[^A-Z ]", "", t).strip()


def is_near(t, pool):
    n = norm(t)
    for other in pool:
        m = difflib.SequenceMatcher(None, n, other)
        if m.real_quick_ratio() >= NEAR_DUP and m.quick_ratio() >= NEAR_DUP and m.ratio() >= NEAR_DUP:
            return True
    return False


def select(results, voices, glyphs):
    """cell -> kept lines. Seeds are never kept (the model must say something new)."""
    seen_exact = set()
    seed_norms = {n: [norm(s.upper()) for s in (*v.general, *(x for xs in v.seeds.values() for x in xs))]
                  for n, v in voices.items()}
    cells = {}
    stats = {"candidates": 0, "kept": 0}
    for job, row in results:
        cap = CAP_PURE if len(job.voices) == 1 else CAP_BLEND
        kept = cells.setdefault(job.cell, [])
        for s in sayings(row):
            stats["candidates"] += 1
            t = clean(s, glyphs)
            if t is None or len(kept) >= cap or norm(t) in seen_exact:
                continue
            pool = [norm(k) for k in kept] + [x for n in job.voices for x in seed_norms[n]]
            if is_near(t, pool):
                continue
            kept.append(t)
            seen_exact.add(norm(t))
            stats["kept"] += 1
    return cells, stats


def c_string(t):
    return '"' + t.replace("\\", "\\\\").replace('"', '\\"') + '"'


def load_registry():
    """(thought rows as (name, topic, when), voice names) from the thoughts registry."""
    def text(name):
        local = REPO / DEFS / name
        if local.exists():
            return local.read_text(encoding="utf-8")
        return subprocess.run(["git", "-C", str(REPO), "show", f"{THOUGHTS_REF}:{DEFS}/{name}"],
                              check=True, capture_output=True, text=True, encoding="utf-8").stdout
    rows = re.findall(r"BLORB_THOUGHT\(\s*\d+,\s*(\w+),\s*(\w+),\s*(\w+),", text("thoughts.def"))
    voices = re.findall(r"BLORB_VOICE\(\s*\d+,\s*(\w+),", text("voices.def"))
    return rows, voices


def registry_voice(cell, voices):
    a, b, _ = cell
    return voices[a].registry if a == b else f"{voices[a].registry}_x_{voices[b].registry}"


def target_rows(topic, rows):
    """Thought rows a topic's lines are said for: its `rows`, else the topic's unconditional prophecies."""
    if topic.rows:
        return list(topic.rows)
    return [name for name, t, when in rows if t == topic.name and when == "anytime"]


def voiced_lines(cells, voices, topics, registry):
    """thought_lines.def rows, and the line count held back per voice the registry lacks."""
    rows, reg_voices = registry
    row_order = {name: i for i, (name, _, _) in enumerate(rows)}
    voice_order = {v: i for i, v in enumerate(reg_voices)}
    out, held = [], Counter()
    for cell, lines in cells.items():
        voice = registry_voice(cell, voices)
        targets = target_rows(topics[cell[2]], rows)
        unknown = [t for t in targets if t not in row_order]
        if unknown:
            sys.exit(f"voices.toml topic {cell[2]} names rows thoughts.def lacks: {unknown}")
        if voice not in voice_order or not targets:
            held[voice] += len(lines)
            continue
        for i, line in enumerate(lines):
            row = targets[i % len(targets)]
            out.append((row_order[row], voice_order[voice], row, voice, line))
    hand = tomllib.loads((HERE / "voices.toml").read_text(encoding="utf-8"))["hand"]["lines"]
    return [(-1, -1, row, voice, text) for row, voice, text in hand] + sorted(out), held


def write_thought_lines(out, held, digest):
    text_bytes = sum(len(r[-1]) + 1 for r in out)
    total = text_bytes + VOICED_LINE_BYTES * len(out)
    held_note = ", ".join(f"{v} {n}" for v, n in sorted(held.items())) or "none"
    w_row = max(len(r[2]) for r in out) + 1
    w_voice = max(len(r[3]) for r in out) + 1
    lines = [
        "// Voiced lines: what a voice says for a row of thoughts.def, in its own words.",
        "// When a (thought, voice) pair has rows here, he says one of them (drawn when",
        "// the line starts) instead of the row's text in that voice's frame. A pair",
        "// with none falls back to the frame.",
        "//",
        "//   thought  a row name in thoughts.def",
        "//   voice    a row name in voices.def",
        "//   text     as thoughts.def: the font's characters, at most one '#' (the",
        "//            row's number), at most kMaxVoicedText characters",
        "//",
        "// GENERATED by tools/dialogue/generate.py from tools/dialogue/voices.toml; edit",
        "// those and rerun (tools/dialogue/README.md). A small base model continued",
        f"// hand-written voice pages: {MODEL} ({digest[:12]}),",
        f"// options {json.dumps(OPTIONS)}, seeds 1-{max(PURE_SEEDS)} per voice page, 1-{max(BLEND_SEEDS)} per hybrid.",
        f"// {len(out)} lines: {text_bytes} bytes of text + {VOICED_LINE_BYTES} per VoicedLine = {total} bytes.",
        f"// Held back until voices.def has the voice: {held_note}.",
        "//",
        "// BLORB_LINE(thought, voice, text)",
        *(f"BLORB_LINE({(row + ',').ljust(w_row)} {(voice + ',').ljust(w_voice)} {c_string(t)})"
          for _, _, row, voice, t in out),
        "",
    ]
    DEF_OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return len(out), text_bytes, total


def write_samples(cells, voices, blends, markers, lexicon, stats, sizes, held, digest):
    rng = random.Random(SAMPLE_RNG)
    hand = set(re.findall(r"[a-z]+", (HERE / "voices.toml").read_text(encoding="utf-8").lower()))
    lex = {w.upper() for g in lexicon.values() for w in g if w not in hand}

    def lex_hits(line):
        return [w for w in re.findall(r"[A-Z]+", line) if w in lex or w.rstrip("S") in lex]

    pure = {n: [(t, l) for (a, b, t), ls in cells.items() if a == b == n for l in ls] for n in voices}
    allines = [(a, b, t, l) for (a, b, t), ls in cells.items() for l in ls]
    md = [
        "# Grungo dialogue samples",
        "",
        f"Generated by `tools/dialogue/generate.sh`. Model `{MODEL}` (`{digest}`), raw completion, "
        f"options `{json.dumps(OPTIONS)}`, seeds {list(PURE_SEEDS)} per pure page and "
        f"{list(BLEND_SEEDS)} per hybrid page.",
        "",
        f"{stats['candidates']} candidate lines, {sum(map(len, cells.values()))} kept. "
        f"{sizes[0]} reach `thought_lines.def`: {sizes[1]} bytes of text, {sizes[2]} bytes with the table. "
        f"Held back until `voices.def` has the voice: "
        f"{', '.join(f'{v} {n}' for v, n in sorted(held.items())) or 'none'}.",
        "",
        "| voice | lines kept |",
        "|---|---|",
        *(f"| {n} | {len(pure[n])} |" for n in sorted(voices, key=lambda n: voices[n].id)),
        f"| hybrids ({len(blends)} pairs) | {sum(len(ls) for (a, b, _), ls in cells.items() if a != b)} |",
        "",
    ]
    for n in sorted(voices, key=lambda n: voices[n].id):
        md += [f"## {n}", ""]
        md += [f"- `{l}` ({t})" for t, l in rng.sample(pure[n], min(8, len(pure[n])))]
        md += [""]
    md += ["## Hybrids", ""]
    for a, b in blends:
        ls = [(t, l) for (x, y, t), lls in cells.items() if (x, y) == (a, b) for l in lls]
        md += [f"**{a} x {b}**", ""] + [f"- `{l}` ({t})" for t, l in rng.sample(ls, min(4, len(ls)))] + [""]
    peter = [x for x in allines if markers.search(x[3])]
    md += ["## Peter seasoning that surfaced", ""]
    md += [f"- `{l}` ({a}{'' if a == b else ' x ' + b}, {t})" for a, b, t, l in rng.sample(peter, min(3, len(peter)))]
    spore = [x for x in allines if lex_hits(x[3])]
    md += ["", f"## Sporefall words that surfaced ({len(spore)} lines)", ""]
    md += [f"- `{l}` ({', '.join(lex_hits(l)).lower()}; {a}{'' if a == b else ' x ' + b}, {t})"
           for a, b, t, l in rng.sample(spore, min(6, len(spore)))]
    SAMPLES_OUT.write_text("\n".join(md) + "\n", encoding="utf-8", newline="\n")


def jobs_for(voices, topics, blends):
    pure = [Job((v,), t, s) for v in voices for t in topics for s in PURE_SEEDS]
    hybrid = [Job(pair, t, s) for pair in blends for t in topics for s in BLEND_SEEDS]
    return pure + hybrid


def main():
    voices, topics, blends, markers = load_corpus()
    lexicon = load_lexicon()
    glyphs = device_glyphs()
    limit = int(os.environ.get("DIALOGUE_LIMIT", "0"))
    jobs = jobs_for(voices, topics, blends)
    results = run_jobs(jobs[:limit] if limit else jobs, voices, topics, lexicon)
    digest = next((r["digest"] for _, r in results if r.get("digest")), "unknown")
    cells, stats = select(results, voices, glyphs)
    out, held = voiced_lines(cells, voices, topics, load_registry())
    sizes = write_thought_lines(out, held, digest)
    write_samples(cells, voices, blends, markers, lexicon, stats, sizes, held, digest)
    print(f"{stats['candidates']} candidates -> {sum(map(len, cells.values()))} kept, "
          f"{sizes[0]} voiced lines, {sizes[1]} text bytes, {sizes[2]} with the table")
    print(f"held back for voices not in voices.def: {dict(held)}")
    print(f"wrote {DEF_OUT.relative_to(REPO)} and {SAMPLES_OUT.relative_to(REPO)}")


if __name__ == "__main__":
    main()
