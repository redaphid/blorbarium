# Grungo's dialogue generator

This tool writes the sayings Grungo shows on the marquee. A small base
(non-instruct) language model continues pages of a fake field notebook, and a
filter keeps the lines the device can show and a gift can carry. The output is
one table, `lib/blorb/include/blorb/defs/dialogue.def`, keyed by voice, topic
and hybrid pair. `samples.md` has lines to judge.

## Run it

```sh
tools/dialogue/generate.sh                         # generate what is missing, filter, write the table
MARQUEE_PNG=sheet.png tools/dialogue/generate.sh   # also render 6 lines with the engine's marquee
REFRESH_LEXICON=1 tools/dialogue/generate.sh       # re-mine D:\Projects\sporefall-art first
```

The run is idempotent. Every completion is cached in `raw.jsonl` under a hash
of the model, options, seed and prompt, so a rerun asks the model only for
pages it has not seen. A changed seed line, cloud or lexicon changes those
prompts and only those prompts. Filtering and output always run again from the
cache, so filter changes need no GPU. `DIALOGUE_OFFLINE=1` builds the table
from cached pages only and skips the model entirely.

Requirements:

- Python 3.11 or later. `generate.py` uses only the standard library.
- Ollama behind `D:\Projects\ollama-proxy` on `localhost:11434`. Override the
  address with `OLLAMA_URL`. The proxy returns 503 while anything else uses the
  GPU, and the client waits for `Retry-After` and tries again for as long as it
  takes.
- `uv`, only for `REFRESH_LEXICON`.
- WSL distro `survivor` with `g++` and Pillow, only for `MARQUEE_PNG`.

## Model

The model is `llama3.2:3b-text-q8_0`, the pretrained Llama 3.2 3B with no
instruction tuning. Its Ollama template is `{{ .Prompt }}`. Requests go to
`/api/generate` with `raw: true`, so no chat template applies, and the model
predicts how the page goes on. The options are temperature 1.15, top-p 0.95,
top-k 100 and repeat penalty 1.1, with up to 200 tokens and a stop at a blank
line. Each pure page is sampled with seeds 1 to 8, and each hybrid page with
seeds 1 to 4. The seed also reshuffles the page's cloud and seed order. `raw.jsonl` records the model digest, the options
and the seed beside every completion.

## Prompts are documents

A prompt is a page that a base model wants to finish:

```text
THE FIELD NOTEBOOK OF A POND SEER
Being the sayings of Grungo, frog settler of the sinking station, taken down word for word
by the Spore Wardens. He calls himself "grungo" and never "I". Each saying is short.

Volume II. Chapter: on FOOD.
Voice: the MYSTIC.
Words heard in the bubbles this week: slurp, fly, spires, broth, verily, brrrup, ...

Sayings, numbered as heard:

1. lo. a crumb approaches from the east.
...
8. brrrup. the bubbles have spoken. they said blub.
9.
```

`voices.toml` holds everything hand-written:

- **Topics.** Ten topics: food, danger, sky, owner, doom, luck, pond, shaking,
  sleep and held. Each has a word cloud.
- **Voices.** Seven voices, each with a word cloud, five general seeds and
  three seeds per topic. The voices are mystic, paranoid_hoarder,
  cheerful_doom, terse, rambling and ominous, plus forecast, in which Grungo
  reads the future out like a shipping forecast ("bog sector: mist, becoming
  peter. good.").
- **Hybrids.** A hybrid page interleaves two voices' clouds and seed lines and
  names the cross ("the TERSE, crossed in the egg with the RAMBLER"), so a
  mutation can produce mixed speech. `[blends] pairs` lists the six pairs
  generated now.
- **Seasoning.** About one seed in ten carries the register of SOUL.md's Peter
  section (brother, stretched words, the Azerbaijan and teleportation bits,
  schemes). `[seasoning] markers` is used only to report which generated
  lines kept it.

Doom stays small and damp. SOUL.md's voice rules forbid menace, death,
drowning, illness and anyone being gone, so the doom topic, the cheerful_doom
voice and the ominous voice are about kettles, soggy socks and the station
sinking another cosy inch. The filter enforces this.

## Flavour lexicon

`lexicon.py` mines `D:\Projects\sporefall-art` (read-only) for unusual words
and writes `lexicon.json`. It reuses the method of
[futurama-string-generator](https://github.com/redaphid/futurama-string-generator).
That method tokenizes a distinctive corpus, part-of-speech tags it, and groups
the words into nouns (NN, NNS, NNP, NNPS), adjectives (JJ, JJR, JJS), verbs
(VBZ, VBD) and adverbs (RB). That repo needs no rarity filter, because a TV
script is already odd. An art pipeline is mostly jargon, so this step adds
three filters:

- A rarity filter. A word's wordfreq Zipf score must be 1.0 to 3.4.
- A coinage rule. A word the dictionary does not know (Zipf below 1.0), such
  as "mireweaver", must appear 3 or more times in the creature prompts or the
  lore.
- A jargon stoplist.

The step reads prose, prompts, Python string literals, JSON strings and file
names (split on camelCase, snake_case and kebab-case). It skips dot
directories and any file whose name suggests a secret.

Each voice's `flavour` table weights the groups it draws from. For example,
the mystic and ominous voices lean on the organic subset (mycelial,
luminescent, siltweaver) and the hoarder leans on nouns. Each page mixes 5
flavour words into its cloud.

## Filter

A generated line is kept only if all of these hold:

- It is a complete numbered item. A line cut off by the token limit is
  dropped.
- After it is uppercased and folded (curly quotes become `'`, dashes become
  `-`, `;` becomes `,`, and brackets, asterisks and double quotes are removed),
  every character is in the marquee font. `generate.py` reads the font from
  `kFont` in `lib/paint/src/draw.cpp`, which is A-Z, 0-9, space and
  `. , ! ? - ' :`.
- It is 6 to 32 characters long. The marquee scrolls, so 32 is a readability
  cap of about two screen widths, not a hardware limit. A longer line keeps
  its longest run of leading whole sentences that fits. The model copies the
  length of the seeds, and most of what it writes runs long.
- It has at least two words and does not end on `:`, `,` or `-`, so lone
  interjections and dangling fragments drop out.
- It has no first person ("I", "ME", "MY"). Grungo speaks in the third person.
- No word or stem from `blocklist.txt` matches. The list covers SOUL.md's Never
  list, sexual content, slurs, profanity and the private-life topics that
  SOUL.md keeps out.
- It is readable. No character repeats 6 or more times, no punctuation runs,
  at least 60% of the characters are letters, and no word is longer than 14
  characters.
- It is new. Exact duplicates are dropped across the whole table. Near
  duplicates (a difflib ratio of 0.78 or more) are dropped within a cell and
  against that voice's seeds, so a seed the model copies does not count.

Each pure voice and topic cell keeps up to 12 lines. Each hybrid cell keeps up
to 8.

## Table format

No `origin/thoughts` branch existed when this was written, so the table uses
a simple voice × topic → lines format in the repo's X-macro style:

```c
BLORB_VOICE(id, name)                      // 7 rows; ids stable, from voices.toml
BLORB_TOPIC(id, name)                      // 10 rows; ids stable
BLORB_LINE(voiceA, voiceB, topic, "TEXT")  // voiceA == voiceB: a pure voice
                                           // otherwise a hybrid, in [blends] order
```

All three macros default to empty and are undefined at the end, so a consumer
defines only the ones it needs:

```cpp
enum class Voice : uint8_t {
#define BLORB_VOICE(id, name) name = id,
#include "blorb/defs/dialogue.def"
};
enum class Topic : uint8_t {
#define BLORB_TOPIC(id, name) name = id,
#include "blorb/defs/dialogue.def"
};
struct Saying { Voice a, b; Topic topic; const char* text; };
constexpr Saying kSayings[] = {
#define BLORB_LINE(a, b, t, text) {Voice::a, Voice::b, Topic::t, text},
#include "blorb/defs/dialogue.def"
};
```

Rows are sorted by (voiceA, voiceB, topic), so each cell is a contiguous run.
If a thoughts registry defines its own voice and topic enums, it can define
`BLORB_LINE` against them by name and skip `BLORB_VOICE` and `BLORB_TOPIC`. A
genome crossed between two voices that have no generated hybrid cell can
alternate lines from the two pure cells. That is the same interleaving the
hybrid prompts use, done at runtime.

## Files

| file | role |
|---|---|
| `generate.sh` | The single entry point. |
| `voices.toml` | Hand-written voices, topics, seeds, blend pairs and seasoning markers. |
| `lexicon.py`, `lexicon.json` | The sporefall-art flavour lexicon and the step that builds it. |
| `blocklist.txt`, `blocklist.py` | The gift filter, shared by both steps. |
| `generate.py` | Pages, the proxy client, the filter, and the table and samples writers. |
| `raw.jsonl` | Every completion, with its model, digest, options and seed. |
| `samples.md` | 8 random lines per voice, hybrids, seasoning and sporefall-word lines. |
| `render_marquee.py`, `marquee_main.cpp` | Renders lines with the engine's own marquee code into a PNG. |
