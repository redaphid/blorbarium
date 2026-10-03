"""Mine D:\\Projects\\sporefall-art (read-only) for a flavour lexicon of unusual words.

The method is futurama-string-generator's (github.com/redaphid/futurama-string-generator):
tokenize a distinctive corpus, part-of-speech tag it, and group the words into
Nouns (NN, NNS, NNP, NNPS), Adjectives (JJ, JJR, JJS), Verbs (VBZ, VBD) and
Adverbs (RB). That repo has no rarity filter because a TV script is already odd;
an art pipeline is mostly jargon, so this adds one: wordfreq's Zipf frequency
keeps rare real words, and coinages (Zipf ~0) only if they recur in prose.

    uv run --with nltk==3.9.1 --with wordfreq==3.1.1 python tools/dialogue/lexicon.py [SRC]
"""
import ast
import warnings
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import nltk
from wordfreq import zipf_frequency

from blocklist import blocked

HERE = Path(__file__).resolve().parent
SRC = Path(sys.argv[1] if len(sys.argv) > 1 else r"D:\Projects\sporefall-art")
OUT = HERE / "lexicon.json"
NLTK_DIR = HERE / ".cache" / "nltk"

SKIP_DIRS = {".git", ".venv", "venv", "node_modules", "__pycache__", "tmp", ".pytest_cache", ".ruff_cache"}
SKIP_NAME = re.compile(r"secret|token|credential|password|\.env|apikey|api_key", re.I)
PROSE = {".md", ".txt"}
# Coinages count only here: the creature prompts and the lore, not docs about the pipeline.
LORE = re.compile(r"^(prompts|_archive)[\\/]|lore", re.I)
MAX_BYTES = 3_000_000

# futurama's grouping, verbatim in spirit (json-tag-transformer.js).
GROUPS = {
    "nouns": {"NN", "NNS", "NNP", "NNPS"},
    "adjectives": {"JJ", "JJR", "JJS"},
    "verbs": {"VBZ", "VBD"},
    "adverbs": {"RB"},
}
PER_GROUP = {"nouns": 220, "adjectives": 110, "verbs": 50, "adverbs": 30}

ZIPF_RARE_MAX = 3.4   # "the" is 7.7, "lantern" 3.4, "chitin" 2.0
ZIPF_REAL_MIN = 1.0
COINAGE_MIN_COUNT = 3

ORGANIC = re.compile(
    r"spor|myc|fung|moss|lichen|peat|bog|alga|gill|slime|mold|mould|root|vine|fern|frond|bloom|"
    r"pod|tendril|chitin|membran|ooze|sap|resin|larva|coral|kelp|mucus|marrow|petal|seed|silt|"
    r"marsh|swamp|reed|frog|toad|newt|glow|lumin|biolum|mire|fen|husk|shell|spine|scale|wart|"
    r"cyan|teal|murk|damp|loam|humus|bulb|stalk|cap|sprout|frill|lobe|feeler|antenna|eel|leech"
)

# Pipeline and programming words that are rare in English but not flavour.
JARGON = set("""
comfy comfyui lora loras vae latent latents sampler samplers denoise upscale upscaler workflow
workflows checkpoint checkpoints safetensors ckpt blender bpy render renders rendered rendering
sprite sprites spritesheet png jpg jpeg webp json toml yaml html css javascript python script
scripts repo readme todo wip mannequin ipadapter controlnet clip unet sdxl flux wan hunyuan
trellis mesh meshes glb gltf fbx rig rigged uv uvs normals shader shaders pixel pixels px
prompt prompts negative cfg steps scheduler karras euler dpm txt img vid mp4 gif api cli gpu
vram cuda torch pytorch numpy subprocess argparse stdout stderr param params config configs
enqueue dequeue queue queued worktree branch commit diff stack frame frames fps keyframe
keyframes alpha rgba rgb hex dataset datasets tensor tensors lowres hires bbox crop crops
inpaint inpainting outpaint img2img txt2img thumbnail thumbnails metadata filename filenames
dir dirs tmp utils util init main def args kwargs async await bool int str dict tuple len
http https url urls localhost port server timestamp utc iso codex claude anthropic openai
gemini ollama agent agents subagent session sessions handoff roadmap changelog
subagents concat coord deflicker downscale keyer mtime outbox preact refiner reindex reskin retries
sigmas symlinks traceback venv artcheck deliverable keyable liveness repad resumable timestamped
unparseable unpatch unstamped dimetric isometric greyscale desaturated readable dithered preflight
pixelize pixelizes hashed mismatched conds dest patcher additively recursively vacuously identically
temporally legitimately horizontally depixel groundplane saveimage sendmessage setsid ffprobe gguf
comfyerror symlink framerate decodes turnarounds vepects wetink tanless tans resonand concurrently sequentially natively adversarial
mixamo qwen hypnodroid frac fuser centroid posescale refscale lightx hifps upscaled sendmessage
""".split())


def ensure_tagger():
    NLTK_DIR.mkdir(parents=True, exist_ok=True)
    nltk.data.path.insert(0, str(NLTK_DIR))
    for pkg in ("averaged_perceptron_tagger_eng",):
        try:
            nltk.data.find(f"taggers/{pkg}")
        except LookupError:
            nltk.download(pkg, download_dir=str(NLTK_DIR), quiet=True)


def split_name(name: str) -> str:
    stem = re.sub(r"\.[A-Za-z0-9]+$", "", name)
    stem = re.sub(r"([a-z])([A-Z])", r"\1 \2", stem)
    return re.sub(r"[_\-.\d]+", " ", stem)


def py_strings(text: str):
    """String literals that read like prose (prompts, captions), not identifiers."""
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            tree = ast.parse(text)
    except SyntaxError:
        return
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and len(node.value.split()) >= 4:
            yield node.value


def json_strings(obj):
    if isinstance(obj, str):
        if len(obj.split()) >= 3:
            yield obj
    elif isinstance(obj, dict):
        for v in obj.values():
            yield from json_strings(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from json_strings(v)


def corpus(src: Path):
    """Yields (kind, text): kind is 'lore', 'prose' or 'name'."""
    for path in src.rglob("*"):
        rel = path.relative_to(src)
        if any(p in SKIP_DIRS or p.startswith(".") for p in rel.parts) or SKIP_NAME.search(path.name):
            continue
        yield "name", split_name(path.name)
        ext = path.suffix.lower()
        try:
            if not path.is_file() or path.stat().st_size > MAX_BYTES:
                continue
            if ext in PROSE:
                kind = "lore" if LORE.search(str(rel)) else "prose"
                yield kind, path.read_text(encoding="utf-8", errors="ignore")
            elif ext == ".py":
                for s in py_strings(path.read_text(encoding="utf-8", errors="ignore")):
                    yield "prose", s
            elif ext == ".json":
                for s in json_strings(json.loads(path.read_text(encoding="utf-8", errors="ignore"))):
                    yield "prose", s
        except (OSError, ValueError):
            continue


def strip_markup(text: str) -> str:
    text = re.sub(r"```.*?```", " ", text, flags=re.S)
    text = re.sub(r"`[^`]*`", " ", text)
    text = re.sub(r"https?://\S+|[A-Za-z]:\\\S+|\S*/\S*/\S*", " ", text)
    return text


def main():
    ensure_tagger()
    tags = defaultdict(Counter)
    lore_count = Counter()
    for kind, text in corpus(SRC):
        if kind != "name":
            text = strip_markup(text)
        for sentence in re.split(r"(?<=[.!?])\s+|\n+", text):
            words = re.findall(r"[A-Za-z]+(?:'[a-z]+)?", sentence)
            if not words:
                continue
            for word, tag in nltk.pos_tag(words):
                w = word.lower()
                if word.isupper() and len(word) > 1 and kind != "name":
                    continue  # acronyms
                tags[w][tag] += 1
                if kind == "lore":
                    lore_count[w] += 1

    groups = defaultdict(list)
    for w, tc in tags.items():
        if not (4 <= len(w) <= 12) or not w.isalpha() or w in JARGON:
            continue
        if blocked(w):
            continue
        z = zipf_frequency(w, "en")
        if z > ZIPF_RARE_MAX:
            continue
        if z < ZIPF_REAL_MIN and lore_count[w] < COINAGE_MIN_COUNT:
            continue
        tag = tc.most_common(1)[0][0]
        group = next((g for g, s in GROUPS.items() if tag in s), None)
        if group is None:
            continue
        n = sum(tc.values())
        score = math.log1p(n) * (ZIPF_RARE_MAX + 0.5 - max(z, ZIPF_REAL_MIN))
        groups[group].append((score, w))

    words = {g: sorted(w for _, w in sorted(groups[g], reverse=True)[:PER_GROUP[g]]) for g in GROUPS}
    organic = sorted(w for ws in words.values() for w in ws if ORGANIC.search(w))
    OUT.write_text(json.dumps({
        "source": str(SRC),
        "method": "futurama-string-generator POS grouping + wordfreq Zipf rarity "
                  f"({ZIPF_REAL_MIN}..{ZIPF_RARE_MAX}; coinages need {COINAGE_MIN_COUNT}+ uses in prompts or lore)",
        "words": words,
        "organic": organic,
    }, indent=1) + "\n", encoding="utf-8")
    print(f"lexicon: {sum(map(len, words.values()))} words ({len(organic)} organic) -> {OUT}")


if __name__ == "__main__":
    main()
