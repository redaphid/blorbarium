# AI lines eval (run r1, 2026-10-21/22)

The question for #37 was which Workers AI model and prompt should write each pet's character and lines. The answer is **`@cf/moonshotai/kimi-k2.6` with prompt v3**, at about $0.06 and 1.5 to 2 minutes per pet. Prompt v3 has the owner's brief: no tone limits, and dark, sad, cheeky and weird are welcome. If cost matters more than voice, the fallback is `@cf/google/gemma-4-26b-a4b-it` at $0.003 per pet, 20 times cheaper and scored about one point lower on voice and humour.

The sample sheet for spot checks is `docs/ai-lines/samples.html`. It shows the results table, three pets side by side under Kimi and Gemma, and every pet under Kimi as genes, then traits, then bible, then lines.

## What was run

- **Pets.** 20 fixture genomes, two per species, from `tools/trait_sheet.cpp` (`web/test/fixtures/genomes.json`). One of each pair is a near-starter, one generation from the species' starter. The other has drifted 6 to 16 generations; some of those carry ancestors' lessons, and two are hybrids (a gilly with zap, a tinker with foresee).
- **Models.** Six open-weight Workers AI models: kimi-k2.6 (thinking off), deepseek-v4.1-flash (reasoning off), gemma-4-26b-a4b (thinking off), mistral-small-3.1-24b, llama-3.3-70b-fp8-fast, and gpt-oss-120b (reasoning low). All ran in JSON mode with the same pipeline as the Worker (`web/src/pipeline.js`).
- **Prompts.**
  - v1 is plain: species notes and genes in, then the bible plus the situations in.
  - v2 adds the genome dice, gene callbacks and a taste of the species' own lines.
  - v3 is v2 with the owner's wider brief. It ran on the two leading models only.
- **Automatic metrics.**
  - Coverage: situations with at least one usable line, out of 132.
  - First-pass valid: lines that passed the mechanical checks with no retry.
  - Retries per pet.
  - Characters per line.
  - distinct-2: unique word pairs within one pet's lines.
  - Same-species overlap: shared word pairs between the two pets of a species.
  - Same-species similarity: bge-m3 embedding cosine between those two pets.
  - Cost and time per pet.
- **Judges.** Each judge scored voice (fit to the species notes), specificity to the genes, and humour (for ages 6 to 13 with a layer for parents), 1 to 5 each. For each same-species pair it also scored distinctness. There is no safety axis, by the owner's rule.
  - qwen3.8-27b (thinking off) scored every pack. It is not a candidate.
  - deepseek-v4-pro (reasoning off) cross-checked the two leading models and scored 13 to 18 of each 20 before timing out on the rest.
  - Claude was not used, because there was no Anthropic key in the environment.

## Results

| model | prompt | coverage | first-pass valid | retries/pet | chars/line | distinct-2 | same-species overlap | qwen V·S·H·D | deepseek-pro V·S·H·D | $/pet | s/pet |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **kimi-k2.6** | **v3** | 100% | 67% | 4.2 | 26.2 | 0.84 | 0.06 | 4.1 · 4.9 · 3.9 · 4.8 | 4.0 · 4.5 · 4.5 · 4.9 | 0.062 | (outage) |
| kimi-k2.6 | v2 | 100% | 68% | 4.3 | 26.7 | 0.85 | 0.06 | 4.4 · 5.0 · 4.1 · 4.6 | 3.7 · 4.5 · 4.8 · 5.0 | 0.063 | 108 |
| kimi-k2.6 | v1 | 100% | 80% | 2.9 | 25.8 | 0.90 | 0.07 | 4.1 · 4.8 · 4.1 · 3.7 | – | 0.047 | 83 |
| deepseek-v4.1-flash | v2 | 99% | 68% | 5.3 | 26.4 | 0.78 | 0.14 | 4.5 · 4.6 · 4.0 · 3.4 | – | 0.021 | 35 |
| deepseek-v4.1-flash | v1 | 100% | 73% | 3.9 | 25.8 | 0.81 | 0.15 | 4.0 · 4.4 · 3.9 · 2.7 | – | 0.017 | 32 |
| gemma-4-26b | v3 | 100% | 93% | 2.9 | 21.7 | 0.76 | 0.12 | 3.5 · 4.5 · 3.3 · 4.0 | 3.5 · 3.2 · 3.8 · 4.0 | 0.003 | (outage) |
| gemma-4-26b | v2 | 100% | 97% | 3.2 | 22.2 | 0.76 | 0.12 | 3.7 · 4.2 · 3.8 · 3.8 | 3.7 · 3.2 · 4.1 · 4.5 | 0.003 | 42 |
| gemma-4-26b | v1 | 100% | 98% | 2.5 | 21.0 | 0.80 | 0.12 | 3.2 · 4.0 · 3.6 · 2.4 | – | 0.003 | 40 |
| gpt-oss-120b | v2 | 93% | 77% | 6.4 | 24.9 | 0.88 | 0.09 | 3.5 · 3.8 · 3.1 · 2.6 | – | 0.029 | 135 |
| gpt-oss-120b | v1 | 100% | 90% | 5.1 | 23.7 | 0.90 | 0.10 | 3.0 · 3.6 · 2.6 · 2.1 | – | 0.019 | 81 |
| mistral-small-3.1 | v2 | 93% | 64% | 7.3 | 25.8 | 0.89 | 0.04 | 2.0 · 2.8 · 2.1 · 4.0 | – | 0.022 | 119 |
| mistral-small-3.1 | v1 | 95% | 79% | 6.2 | 24.8 | 0.88 | 0.05 | 2.0 · 2.9 · 2.3 · 3.1 | – | 0.017 | 99 |
| llama-3.3-70b | v2 | 100% | 96% | 4.1 | 10.4 | 0.87 | 0.11 | 2.2 · 2.4 · 2.0 · 3.0 | – | 0.011 | 30 |
| llama-3.3-70b | v1 | 100% | 98% | 2.9 | 10.6 | 0.87 | 0.21 | 2.0 · 2.1 · 1.8 · 2.2 | – | 0.010 | 29 |

The judge columns read V = voice, S = genes, H = humour, D = distinctness between the two pets of a species.

## Reading it

- **Kimi K2.6 is the best writer by both judges.**
  - It uses the genes most (specificity 4.5 to 5).
  - It gives the most distinct pets of one species: distinctness 4.6 to 5, and only 6% shared word pairs between the two.
  - Both judges rank its humour first or second.
  - Its lines fill the screen: about 26 of 30 characters.
- **Kimi's weak point is length discipline.** Only about two thirds of its lines fit 30 characters on the first try, which costs about 4 retries per pet. The retries recover every situation: coverage is 100% on every Kimi run.
- **v2 and v3 tie within judge noise; v3 is the default.** Against v1, they buy gene callbacks and distinctness: same-species distinctness rose from 3.7 to about 4.7 for Kimi and from 2.4 to about 3.9 for Gemma. They cost some first-pass validity. v3 is now the default because it is the owner's brief.
- **Gemma 4 26B is the budget pick.** Its lines are almost always valid first time (93 to 98%), at $0.003 per pet and about 40 s. They are shorter (about 22 characters), and its voice and humour score lower; the deepseek judge rated its specificity 3.2.
- **DeepSeek V4.1 Flash is close to Kimi on the qwen judge** at a third of the price. But its two pets of a species are much less distinct: distinctness 2.7 to 3.4, and 14 to 15% shared word pairs. It also repeats built-in lines often (387 to 859 rejections).
- **Ruled out:**
  - llama-3.3-70b writes 10-character lines ("ZAP OW"), too short to carry a joke.
  - mistral-small-3.1 and gpt-oss-120b leave situations uncovered (93 to 95%) and score low.

## Mechanical checks, per model

These are lines rejected across 20 pets, before the retries recovered them.

| model | too long | charset | marks | repeats a built-in line | repeats his own |
|---|---|---|---|---|---|
| kimi-k2.6 v3 | 3070 | 28 | 153 | 74 | 47 |
| gemma-4-26b v3 | 46 | 48 | 34 | 6 | 69 |
| deepseek-v4.1-flash v2 | 3061 | 2 | 39 | 387 | 202 |
| gpt-oss-120b v2 | 680 | 48 | 14 | 387 | 86 |
| mistral-small-3.1 v2 | 3588 | 71 | 44 | 7 | 5 |
| llama-3.3-70b v2 | 0 | 2 | 6 | 0 | 240 |

## Caveats

- 20 pets is small: a difference of 0.3 on a 1-to-5 axis is noise.
- The qwen judge is smaller than Kimi. The deepseek judge agreed on the ranking, but it is the same family as one candidate and scored only 13 to 18 pets per row.
- The v3 rows' seconds per pet include a network outage during that run, so v1 and v2 times stand in for v3. The pipeline now treats network errors as transient and retries them.
- Kimi's 30-character misses could probably be halved by telling it to count. That is a candidate v4, and it is mechanical.

## Re-running

    bash scripts/test/ai_lines/run_eval.sh r2            # every model, v1 and v2, both judges (background; .jobs/)
    node scripts/test/ai_lines_eval.mjs --run r2 --models kimi,gemma --prompts v3
    node scripts/test/ai_lines/sheet.mjs --run r2 --pick kimi-k2.6:v3 --vs gemma-4-26b-a4b-it:v3

Every pack, judge score and the summary of a run are kept under `outputs/ai_lines_eval/<run>/`, which is gitignored. A re-run reuses what is already there.
