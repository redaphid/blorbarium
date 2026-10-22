// The LLM judge: a strong Workers AI model scores each pack on a written
// rubric (no safety axis, by the owner's rule), and each same-species pair
// for how different the two pets feel. Scores are 1 to 5.
import catalog from "../../../web/src/catalog.json" with { type: "json" };
import { call } from "../../../web/src/pipeline.js";
import { parseJsonReply } from "../../../web/src/validate.js";

// The situations the judge reads, the same for every pack so scores compare.
export const JUDGED = ["peckish", "starving", "crunch", "fuzzy_bite", "heard_you", "ground_lies", "hooded",
  "warm_hands", "sky_down", "dream_pellets", "see_pellet", "knees", "moon", "anyone", "p_feed_me", "p_mud",
  "p_doom", "p_lucky", "p_pond_yes", "morning", "monday", "my_name", "rim_walk", "plans", "brave", "sunbeam",
  "star_crumbs", "snore", "verdict_yes", "verdict_no"];

export const RUBRIC = `Score each axis from 1 to 5.

VOICE (fit to the species notes): 5 = every line could only be this species, in the way the notes say it talks (its words, rhythm, loves and fears); 3 = mostly fits, some lines generic or off-voice; 1 = generic pet lines, or contradicts the notes.

SPECIFICITY (to this pet's genes): 5 = the bible and many lines clearly use this pet's decoded traits (e.g. a timid one jumps at nothing, a hybrid's borrowed power, an ancestor's lesson, loves being shaken, his appetite or sleep); 3 = the bible uses the traits but the lines rarely do; 1 = nothing would change if the genes were different.

HUMOUR (for kids aged 6 to 13, with a layer for parents): 5 = many lines would make a kid laugh or grin and some have a wink for a parent; jokes are concrete and land in under 30 characters; 3 = pleasant, a few real jokes; 1 = flat, confusing, or just descriptions.

Judge only the text given. Be strict: 5 is rare.`;

function packText(pet, pack) {
  const lines = JUDGED.filter((s) => pack.lines[s]?.length)
    .map((s) => `${s}: ${pack.lines[s].join(" | ")}`).join("\n");
  return `Species notes (canon):\n<<<\n${catalog.species[pet.species].notes}\n>>>\n\nThis pet's decoded genes:\n${pack.traits.text}\n\nCharacter bible written for him:\n${pack.bible}\n\nA sample of his lines (situation: lines):\n${lines}`;
}

export async function judgePack(ai, judgeModel, pet, pack) {
  const messages = [
    { role: "system", content: "You are a strict, fair judge of writing for a children's virtual-pet game. Reply with JSON only." },
    { role: "user", content: `${RUBRIC}\n\n${packText(pet, pack)}\n\nReply as JSON: {"voice": n, "specificity": n, "humour": n, "best_line": "...", "worst_line": "...", "notes": "one or two sentences"}` },
  ];
  for (let attempt = 0; attempt < 3; attempt++) {
    const reply = await call(ai, judgeModel, messages, { maxTokens: 4000, temperature: 0.2 });
    const j = parseJsonReply(reply);
    if (j && [j.voice, j.specificity, j.humour].every((x) => Number.isFinite(Number(x)))) {
      return { voice: Number(j.voice), specificity: Number(j.specificity), humour: Number(j.humour),
               best: j.best_line ?? "", worst: j.worst_line ?? "", notes: j.notes ?? "" };
    }
  }
  return null;
}

export async function judgePair(ai, judgeModel, petA, packA, petB, packB) {
  const sample = (pack) => JUDGED.slice(0, 18).filter((s) => pack.lines[s]?.length)
    .map((s) => `${s}: ${pack.lines[s].join(" | ")}`).join("\n");
  const messages = [
    { role: "system", content: "You are a strict, fair judge of writing for a children's virtual-pet game. Reply with JSON only." },
    { role: "user", content: `Two pets of the same species (${petA.species}), each with its own genes, bible and lines. Score DISTINCTNESS from 1 to 5: 5 = two clearly different characters a kid could tell apart from a few lines; 3 = same template with different details; 1 = interchangeable.\n\nPET A bible:\n${packA.bible}\nPET A lines:\n${sample(packA)}\n\nPET B bible:\n${packB.bible}\nPET B lines:\n${sample(packB)}\n\nReply as JSON: {"distinctness": n, "notes": "one sentence"}` },
  ];
  for (let attempt = 0; attempt < 3; attempt++) {
    const j = parseJsonReply(await call(ai, judgeModel, messages, { maxTokens: 3000, temperature: 0.2 }));
    if (j && Number.isFinite(Number(j.distinctness))) return { distinctness: Number(j.distinctness), notes: j.notes ?? "" };
  }
  return null;
}

// Run r2's rubric (the owner: "the most unhinged lines that still make sense").
export const RUBRIC_UNHINGED = `Score each axis from 1 to 5. Judge the LINES above all; the bible is context.

UNHINGED: 5 = most lines are wild, absurd, chaotic or startling, left turns you did not see coming; 3 = some surprising lines among sensible ones; 1 = tame, predictable, ordinary pet talk.

MAKES SENSE: 5 = every line lands at once: a reader gets the joke or the picture, it reads as words a creature would say, and it fits its situation; 3 = most land, some are word salad or miss their situation; 1 = mostly random words or nonsense.

VOICE: 5 = unmistakably this species, as the notes describe it; 1 = generic.

SPECIFICITY: 5 = many lines use this pet's own decoded genes and bible; 1 = nothing would change with other genes.

Score unhinged and makes-sense independently: a line can be both wild and perfectly clear. Be strict: 5 is rare.`;

export async function judgeUnhinged(ai, judgeModel, pet, pack) {
  const messages = [
    { role: "system", content: "You are a strict, fair judge of comic writing for a virtual-pet game. Reply with JSON only." },
    { role: "user", content: `${RUBRIC_UNHINGED}\n\n${packText(pet, pack)}\n\nAlso pick the 3 lines that are the most unhinged while still making complete sense, copied exactly.\n\nReply as JSON: {"unhinged": n, "makes_sense": n, "voice": n, "specificity": n, "wildest_coherent": ["...", "...", "..."], "notes": "one or two sentences"}` },
  ];
  for (let attempt = 0; attempt < 3; attempt++) {
    const j = parseJsonReply(await call(ai, judgeModel, messages, { maxTokens: 4000, temperature: 0.2 }));
    if (j && ["unhinged", "makes_sense", "voice", "specificity"].every((k) => Number.isFinite(Number(j[k])))) {
      return { unhinged: Number(j.unhinged), sense: Number(j.makes_sense), voice: Number(j.voice), specificity: Number(j.specificity),
               wildest: Array.isArray(j.wildest_coherent) ? j.wildest_coherent.map(String).slice(0, 3) : [], notes: j.notes ?? "" };
    }
  }
  return null;
}
