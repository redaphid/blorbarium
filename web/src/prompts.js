// The two prompts of #37: genes -> a character bible, then bible + genes ->
// lines per situation. Versions are kept side by side so the eval
// (scripts/test/ai_lines_eval.mjs) can compare them and a stored pack says
// which made it. The audience and voice guidance is creative direction only:
// what is checked is mechanical (validate.js).
import catalog from "./catalog.json" with { type: "json" };
import { MAX_TEXT, MIN_LINES, MAX_LINES } from "./validate.js";

const FONT_RULE = `Only capital letters A-Z, digits 0-9, spaces and these marks: . , ! ? - ' : (no other punctuation, no quotes, no emoji).`;

function speciesNotes(species) {
  return catalog.species[species]?.notes ?? "";
}

// A few of the species' own lines as a taste of the voice, never to copy.
function styleSamples(species, n = 8) {
  const own = catalog.species[species]?.lines ?? {};
  const all = Object.values(own).flat();
  const step = Math.max(1, Math.floor(all.length / n));
  return all.filter((_, i) => i % step === 0).slice(0, n);
}

function situationList(batch) {
  return batch.map((s) => {
    const mark = s.mark === "#" ? " (may use one # for the number)" : s.mark === "@" ? " (may use one @ for his name)" : "";
    return `- ${s.name}: ${s.meaning}${mark} Example: ${s.shared}`;
  }).join("\n");
}

const AUDIENCE = `The pets live on badges worn by kids aged 6 to 13. Write to make a kid laugh out loud, with a second layer a parent will catch. Silly, specific and surprising beats wise or generic.`;

// v3 (after the r1 eval): the owner wants no tone policing. Dark, ominous,
// sad, cheeky and weird are all welcome; the species notes set the voice.
const AUDIENCE_V3 = `The pets live on badges worn by kids aged 6 to 13 and their parents. Make them laugh, wince or go "ooh": a joke for the kid with a second layer for the parent. Dark, ominous, sad, cheeky, gross and weird are all welcome wherever the species is like that. Specific and surprising beats generic.`;

function v2With(audience) {
  return {
    bible(input) {
      const dice = Object.entries(input.dice).map(([k, v]) => `- ${k.replace(/_/g, " ")}: ${v}`).join("\n");
      return [
        { role: "system", content: `You are the character writer for blorbarium, tiny pets that live on a round badge screen. ${audience} Reply with JSON only.` },
        { role: "user", content:
`The species' character notes, from the game's writer. Everything here is canon:
<<<
${speciesNotes(input.species)}
>>>

This one pet's genes, decoded by the game (how he differs from the rest of his species):
${input.traits.text}

Fixed facts about this pet (drawn from his genome; use at least two):
${dice}

Write his character bible, about 120 words: who he is, his quirks, what he loves and fears, how he talks (rhythm, pet words, the kind of joke he makes). Turn the gene facts into personality: a timid one jumps at nothing, a hybrid brags about or puzzles over his borrowed power, a pet who loves being shaken asks for it, an ancestor's lesson is family lore. He must still sound like his species, but like no other pet of it.

Also give 3 catchphrases of at most ${MAX_TEXT} characters.

Reply as JSON: {"bible": "...", "catchphrases": ["...", "...", "..."]}` },
      ];
    },
    lines(input, batch) {
      return [
        { role: "system", content: `You write the short lines a blorbarium pet says on his round badge screen. ${audience} Reply with JSON only.` },
        { role: "user", content:
`His character bible:
${input.bible}

His catchphrases: ${(input.catchphrases ?? []).join(" / ")}

His genes, decoded:
${input.traits.text}

How his species usually sounds (a taste of the voice; do not reuse these):
${styleSamples(input.species).map((l) => `  ${l}`).join("\n")}

Write ${MIN_LINES} to ${MAX_LINES} lines for each situation below: what HE says in that moment. Make them his: call back to his bible, his quirks and his genes in many of them. Vary the shapes (a question, a boast, a tiny story, a sound effect), and end each line on its joke. The badge shows a line exactly as written.

Hard rules for every line (a line that breaks one is thrown away):
- At most ${MAX_TEXT} characters, counting spaces. Short is good.
- ${FONT_RULE}
- # and @ only where a situation says it may use them (# shows its number, @ his name).
- Not a copy of the example line, and no two lines the same.

Situations (name: when he says it; the old generic example):
${situationList(batch)}

Reply as JSON: {"lines": {"situation_name": ["LINE", "LINE"], ...}} with every situation above, each keyed by its name exactly as written.` },
      ];
    },
  };
}

export const PROMPTS = {
  v1: {
    bible(input) {
      return [
        { role: "system", content: `You write characters for blorbarium, tiny pets that live on a round screen. ${AUDIENCE} Reply with JSON only.` },
        { role: "user", content:
`The species' character notes, from the game's writer:
<<<
${speciesNotes(input.species)}
>>>

This one pet's genes, decoded (how he differs from the rest of his species):
${input.traits.text}

Write this pet's character bible: about 120 words on who he is, his quirks, what he loves and fears, and how he talks (rhythm, favourite words, the kind of jokes he makes). Stay true to the species notes, and make the genes matter.

Reply as JSON: {"bible": "...", "catchphrases": ["...", "..."]}` },
      ];
    },
    lines(input, batch) {
      return [
        { role: "system", content: `You write the short lines a blorbarium pet says on his round screen. ${AUDIENCE} Reply with JSON only.` },
        { role: "user", content:
`This pet's character bible:
${input.bible}

His genes, decoded:
${input.traits.text}

Write ${MIN_LINES} to ${MAX_LINES} lines for each situation below. Each line is something he says in that moment, in his own voice.
Rules for every line:
- At most ${MAX_TEXT} characters, counting spaces.
- ${FONT_RULE}
- # and @ only where a situation says it may use them (# shows its number, @ his name).
- Not a copy of the example line.

Situations (name: when he says it; example):
${situationList(batch)}

Reply as JSON: {"lines": {"situation_name": ["LINE", "LINE"], ...}} with every situation above, each keyed by its name exactly as written.` },
      ];
    },
  },
  v2: v2With(AUDIENCE),
  v3: v2With(AUDIENCE_V3),
};

// A retry: the same conversation, plus what was wrong, for the situations still short.
export function retryMessages(messages, reply, failures, stillNeeded, structural = []) {
  return [
    ...messages,
    { role: "assistant", content: typeof reply === "string" ? reply : JSON.stringify(reply) },
    { role: "user", content:
`Some lines could not be used:
${[...structural.slice(0, 10).map((s) => `- ${s}`), ...failures.slice(0, 40).map((f) => `- ${f.situation}: "${f.text}" ${f.why}`)].join("\n") || "- (the reply was missing situations or was not the JSON asked for)"}

Write new lines for these situations only, keyed by exactly these names: ${stillNeeded.join(", ")}.
Same rules: at most ${MAX_TEXT} characters, only A-Z 0-9 space . , ! ? - ' : (plus # or @ only where marked), no repeats.
Reply as JSON: {"lines": {"situation_name": ["LINE", "LINE"], ...}}` },
  ];
}
