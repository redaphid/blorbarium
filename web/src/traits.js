// A pet's genes in words: the trait sheet the first AI call writes a character
// from. Every fact is relative to the pet's own species' starter at the same
// stage (baselines.json, from tools/trait_sheet.cpp), so a sheet says what makes
// THIS mossby himself, not what every mossby is.
import catalog from "./catalog.json" with { type: "json" };
import baselines from "./baselines.json" with { type: "json" };

const speciesRow = (n) => Object.values(catalog.tables.species).find((s) => s.name === n);
const powerOwner = (p) => Object.values(catalog.tables.species).find((s) => s.power === p);

const STIM = {
  knock: "a knock on his glass", double_knock: "a double knock", shake: "being shaken", picked_up: "being picked up",
  put_down: "being put down", dropped: "being dropped", flipped: "being flipped over", righted: "being set upright",
  cradle: "being cradled", lid_down: "being put face down", button_hold: "a long press of his button",
  fed: "a snack", fed_bad: "a rotten snack", owner_arrived: "his owner's phone arriving",
  owner_left: "his owner's phone leaving", self_foresaw: "having a vision", petted: "being petted from the phone",
  played: "being played with", spoken_to: "being spoken to", dusk: "dusk", dawn: "dawn", woke: "waking up",
  hatched: "hatching", duel_won: "winning a duel", duel_landed: "landing a bonk in a duel",
  duel_bonked: "being bonked in a duel", duel_fainted: "fainting in a duel", duel_befriended: "making a friend in a duel",
  duel_mourned: "hearing a friend's pet is gone", duel_spared: "being spared in a duel",
  duel_slew: "beating a friend's pet for good", hang: "being held upside down",
};
const CHEM = {
  fear: "scared", boredom: "bored", loneliness: "lonely", need_touch: "in need of a cuddle",
  discomfort: "uncomfortable", hunger: "hungry", sleepiness: "sleepy", pain: "sore", adrenaline: "jumpy",
  injury: "hurt", toxin: "queasy", melatonin: "drowsy", daze: "dazed", pluck: "fired up", mettle: "proud",
  dread: "full of dread", vision: "visionary", energy: "energetic", food: "full", warmth: "warm", life: "lively",
};
const STAGE_WORD = { baby: "a newly hatched baby", child: "a child", adult: "a grown-up", elder: "an elder" };

function degree(delta, step, words) {
  // words: [much less, less, same, more, much more]
  if (delta <= -2 * step) return words[0];
  if (delta <= -step) return words[1];
  if (delta >= 2 * step) return words[4];
  if (delta >= step) return words[3];
  return words[2];
}
const avg = (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);

export function traitSheet(raw, { generation = 1, name = "" } = {}) {
  const base = baselines[raw.species]?.[raw.stage] ?? baselines.grungo.adult;
  const sp = speciesRow(raw.species);
  const notes = [];
  const say = (k, s) => s && notes.push({ k, s });

  // ---- who he is ----
  say("identity", `${raw.species} (${sp.sketch}), ${STAGE_WORD[raw.stage] ?? raw.stage}, generation ${generation}` +
      (name ? `, named ${name.toUpperCase()}` : ""));
  say("element", `element ${raw.element}; in a duel he loves the ${raw.lovedStance} stance` +
      (raw.lovedStance !== raw.style ? ` (unusual: most ${raw.species}s love ${raw.style})` : ""));
  if (raw.power !== sp.power) {
    const other = powerOwner(raw.power);
    say("hybrid", `a hybrid: a ${raw.species} body carrying the ${raw.power} power` +
        (other ? ` that ${other.name}s (${other.sketch}) are born with` : ""));
  } else {
    say("power", `his power is ${raw.power}`);
  }

  // ---- temperament, from his chemistry ----
  if (raw.startleGain === 0) say("bold", "never hops when shaken: unflappable, or too dignified to jump");
  else {
    say("bold", degree(raw.startleGain - base.startleGain, 4,
      ["far more timid than most of his kind", "a bit more timid than most of his kind", null,
       "bolder than most of his kind", "far bolder than most of his kind"]));
  }
  const hunger = avg(raw.hungerFill), baseHunger = avg(base.hungerFill);
  if (hunger === null) say("appetite", "has no steady hunger of his own: he only gets peckish when his energy runs low, and forgets about food otherwise");
  else if (baseHunger !== null) {
    // A fill byte 12 higher fills half as fast: a smaller appetite.
    say("appetite", degree(baseHunger - hunger, 3,
      ["a tiny appetite", "a smaller appetite than most", null, "hungrier than most", "always, always hungry"]));
  }
  const mel = avg(raw.melatoninFill);
  if (mel === null) say("sleep", "no sleepy chemistry when it gets dark: he stays up as late as he likes");
  else {
    const owl = mel >= 131; // the half-hour line between a 15-minute and an hour's melatonin
    const baseOwl = avg(base.melatoninFill) >= 131;
    say("sleep", owl ? "a night owl: wide awake for the first hours of the night" : "early to bed: sleepy within minutes of dark");
    if (owl !== baseOwl) say("sleep2", `unusual for a ${raw.species}, who is ${baseOwl ? "a night owl" : "early to bed"}`);
  }
  const naps = avg(raw.sleepinessFill), baseNaps = avg(base.sleepinessFill);
  if (naps !== null && baseNaps !== null) {
    say("naps", degree(baseNaps - naps, 3, ["rarely naps", "naps a little less than most", null, "a keen napper", "naps all the time"]));
  }
  const t = raw.temperament, bt = base.temperament;
  say("learn", degree(t.learnRate - bt.learnRate, 60, ["very slow to learn", "a slow learner", null, "a quick learner", "learns almost instantly"]));
  say("forget", degree(t.forgetRate - bt.forgetRate, 25, ["never forgets a thing", "remembers well", null, "forgetful", "forgets almost everything"]));
  say("explore", degree(t.explore - bt.explore, 60, ["very cautious, sticks to what he knows", "a bit cautious", null, "extra curious", "wildly curious, tries everything"]));
  say("habituation", degree(t.habituation - bt.habituation, 60, ["never gets used to surprises", "slow to get used to things", null, "quick to get used to things", "nothing surprises him twice"]));
  if (t.dreamLen - bt.dreamLen >= 30 || bt.dreamEvery - t.dreamEvery >= 20) say("dreams", "a big dreamer: long, frequent dreams");
  else if (bt.dreamLen - t.dreamLen >= 15 || t.dreamEvery - bt.dreamEvery >= 30) say("dreams", "dreams rarely and briefly");

  // ---- what things do to him, where his genes differ from his kind's ----
  // A chemical with no name does nothing anyone could notice, so it says nothing.
  const feels = [];
  for (const [stim, chems] of Object.entries(raw.stimuli)) {
    const changes = [];
    for (const [chem, v] of Object.entries(chems)) {
      if (!CHEM[chem]) continue;
      const b = base.stimuli[stim]?.[chem] ?? 0;
      const d = v - b;
      if (Math.abs(d) < 80) continue;
      if (chem === "boredom" && stim === "shake" && v < 0 && b >= 0) { feels.unshift("secretly LOVES being shaken"); continue; }
      changes.push(`${(base.stimuli[stim] ? d : v) > 0 ? "more" : "less"} ${CHEM[chem]}`);
    }
    if (!changes.length) continue;
    if (!STIM[stim]) feels.push(`a mysterious sense nobody else in his family has: now and then something makes him ${changes.join(" and ")}`);
    else feels.push(`${STIM[stim]} makes him ${changes.join(" and ")} than most of his kind`);
  }
  for (const f of feels.slice(0, 5)) say("feels", f);

  // ---- looks ----
  const looks = [];
  const baseRegion = Object.fromEntries(base.palette.map((p) => [p.region, p]));
  const seen = {};
  for (const p of raw.palette) {
    seen[p.region] = (seen[p.region] ?? 0) + 1;
    const b = baseRegion[p.region];
    if (!b || seen[p.region] > (base.palette.filter((x) => x.region === p.region).length)) {
      looks.push(`an extra ${p.region} colour gene (a two-tone ${p.region})`);
      continue;
    }
    const hueDeg = Math.round(((p.hue - b.hue) * 360) / 256);
    const bits = [];
    if (Math.abs(hueDeg) >= 8) bits.push(`${Math.abs(hueDeg) >= 30 ? "a clearly different" : "a slightly different"} shade (${hueDeg > 0 ? "+" : "-"}${Math.abs(hueDeg)} degrees round the colour wheel)`);
    if (p.val - b.val >= 12) bits.push("brighter"); else if (b.val - p.val >= 12) bits.push("darker");
    if (p.sat - b.sat >= 12) bits.push("more vivid"); else if (b.sat - p.sat >= 12) bits.push("more washed-out");
    if (bits.length) looks.push(`${p.region}: ${bits.join(", ")} than the usual ${raw.species}`);
  }
  for (const m of raw.marks) {
    const b = base.marks.find((x) => x.layer === m.layer);
    if (!b) looks.push(m.layer === 2 ? "wears war paint (his line has beaten a friend's pet for good)" : `a rare extra marking (layer ${m.layer})`);
    else if (m.variant !== b.variant) looks.push(`${m.variant > b.variant ? "more" : "fewer"} markings than most`);
  }
  const sizeD = raw.size.base - base.size.base;
  if (sizeD >= 8) looks.push("bigger than most of his kind"); else if (sizeD <= -8) looks.push("smaller than most of his kind");
  for (const l of looks.slice(0, 5)) say("looks", l);

  // ---- life and lineage ----
  const m = raw.metabolism, bm = base.metabolism;
  if (m.rotMinutes && bm.rotMinutes && Math.abs(m.rotMinutes - bm.rotMinutes) >= 15)
    say("food", m.rotMinutes < bm.rotMinutes ? "his snacks go fuzzy fast" : "his snacks stay fresh for ages");
  if (m.pantrySize !== bm.pantrySize) say("pantry", `keeps ${m.pantrySize} snacks in his pantry (most keep ${bm.pantrySize})`);
  if (raw.wild - base.wild >= 8) say("wild", "comes from a wild, fast-changing line");
  if (raw.dormant - base.dormant >= 2) say("latent", `carries ${raw.dormant} sleeping genes: hidden traits that may wake in his children`);
  if (raw.genes - base.genes >= 3) say("dups", `${raw.genes - base.genes} extra genes from his ancestors' copying`);
  for (const h of raw.heirlooms) {
    const cue = h.cue.replace(/^recent_/, "just after ").replace(/_/g, " ");
    say("heirloom", `born knowing an ancestor's lesson: when ${cue}, ${h.action.replace(/_/g, " ")} to ${h.level < 0 ? "ease" : "stoke"} his ${h.drive.replace(/_/g, " ")}`);
  }
  if (raw.motto && raw.motto !== base.motto) say("motto", `family motto (written in his genes by his owner): "${raw.motto}"`);

  const facts = {
    species: raw.species, sketch: sp.sketch, stage: raw.stage, generation, name, element: raw.element,
    power: raw.power, hybrid: raw.power !== sp.power, lovedStance: raw.lovedStance, voice: raw.voice,
    heirlooms: raw.heirlooms, motto: raw.motto,
  };
  return { facts, notes, text: notes.map((n) => `- ${n.s}`).join("\n") };
}

// Prompt v2's flavour dice: a few concrete hooks drawn from the genome's hash,
// so two pets with near-identical genes still get different jokes. Every die
// is a fact about the pet, as fixed as his genes.
const DICE = {
  treasure: ["a bottle cap", "a single blue button", "a very round pebble", "a lost sock's toe", "a shiny bolt",
             "a dried pea", "a feather from nobody he knows", "a crumb shaped like the moon", "a tiny key", "a marble"],
  pet_peeve: ["loud chewing", "being called cute", "Mondays", "crumbs on the glass", "the colour beige",
              "being rushed", "sneezes", "wet socks", "people who hum off-key", "the word 'later'"],
  hobby: ["counting things", "inventing words", "practising a dance", "collecting echoes", "writing tiny poems",
          "staring contests", "planning a trip he will never take", "rating clouds out of ten",
          "keeping secrets", "narrating his own life"],
  flaw: ["thinks he is famous", "gets the giggles at bad times", "exaggerates everything",
         "is terrible at hide and seek", "cannot keep a secret", "overthinks snacks", "is dramatic about tiny things",
         "always thinks it is lunchtime", "brags about his ancestors", "pretends to know words he doesn't"],
};
export function flavourDice(hashHex) {
  const bytes = hashHex.match(/../g).map((h) => parseInt(h, 16));
  const out = {};
  Object.entries(DICE).forEach(([k, list], i) => { out[k] = list[(bytes[i * 2] << 8 | bytes[i * 2 + 1]) % list.length]; });
  return out;
}
