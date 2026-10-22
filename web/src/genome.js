// A genome's raw trait sheet, decoded from its bytes as the engine decodes
// them (lib/blorb/src/genes.cpp). tools/trait_sheet.cpp is the reference:
// web/test/traits.test.mjs holds this file to its output, field for field,
// on the eval's fixture genomes. Raw means bytes and engine units; the words
// are traits.js's job.
import catalog from "./catalog.json" with { type: "json" };

const T = catalog.tables;
const HEADER = 8;
const MAX_GENOME = 8 * 1024;
const ONE = 1 << 24; // Fx::kOne, Q8.24
const DRIVE_BASE = 1; // ids.h: drive n is chemical 1 + n
const STAGES = ["baby", "child", "adult", "elder"];
const STANCES = ["pounce", "guard", "trick"];
const FLAG = { dormant: 1 << 3, heirloom: 1 << 5 };
// Body lengths of the known kinds (genes.h; layouts are frozen: a changed
// layout is a new type byte). A known kind whose len disagrees is never expressed.
const BODY_LEN = {
  chem: 3, reaction: 9, emitter: 6, receptor: 6, stimulus: 8, instinct: 7, expression: 8, temperament: 7,
  knack: 3, palette: 6, size: 3, mark: 4, species: 2, power: 2, mutation: 8, egg: 3, metabolism: 4, note: 24,
};
const CHEM = { hunger: DRIVE_BASE + 0, sleepiness: DRIVE_BASE + 1, adrenaline: 20, melatonin: 23 };
const LOCUS = { always: 0, light: 6, startle: 133 };
const REGION_COUNT = Object.keys(T.regions).length;
const SPECIES_COUNT = Object.keys(T.species).length;
const POWER_COUNT = Object.keys(T.powers).length;

export const stageIndex = (s) => {
  const i = typeof s === "number" ? s : STAGES.indexOf(String(s).toLowerCase());
  return i >= 0 && i < 4 ? i : 2;
};

export function hexToBytes(hex) {
  const clean = String(hex || "").trim().toLowerCase();
  if (!/^([0-9a-f]{2})+$/.test(clean)) throw new Error("genome: not hex");
  const out = new Uint8Array(clean.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(clean.slice(2 * i, 2 * i + 2), 16);
  return out;
}

// Genome::parse: a length chain that overruns, a duplicate uid or the size cap is damage.
export function parseGenome(bytes) {
  if (bytes.length > MAX_GENOME) throw new Error("genome: over the 8 KB cap");
  const genes = [];
  const uids = new Set();
  for (let at = 0; at < bytes.length; ) {
    if (at + HEADER > bytes.length) throw new Error("genome: a header runs past the end");
    const len = bytes[at + 1];
    if (at + HEADER + len > bytes.length) throw new Error("genome: a body runs past the end");
    const uid = bytes[at + 6] | (bytes[at + 7] << 8);
    if (uids.has(uid)) throw new Error(`genome: uid ${uid} twice`);
    uids.add(uid);
    const type = bytes[at];
    genes.push({ type, kind: T.geneKinds[type] ?? null, len, flags: bytes[at + 2], stage: bytes[at + 3],
                 featGate: bytes[at + 4], uid, body: bytes.subarray(at + HEADER, at + HEADER + len) });
    at += HEADER + len;
  }
  if (!genes.length) throw new Error("genome: empty");
  return genes;
}

const known = (g) => g.kind !== null && BODY_LEN[g.kind] === g.len;
// genome.h expressedAt
const expressedAt = (g, stage, feats) =>
  !(g.flags & FLAG.dormant) && g.stage === stage &&
  (g.featGate === 0 || (g.featGate <= 32 && ((feats >>> (g.featGate - 1)) & 1) === 1));

// Stage by stage, genome order within: the order expressStage runs them.
function* inExpressionOrder(genes, stage, feats) {
  for (let s = 0; s <= stage; s++) for (const g of genes) if (known(g) && expressedAt(g, s, feats)) yield g;
}

const permilleUnit = (b) => Math.trunc((Math.floor((b * ONE) / 255) * 1000) / ONE); // Fx::unitByte
const permilleSigned = (b) => Math.trunc(((b - 128) * 1000) / 128); // Fx::signedByte
const name = (table, id, fallback) => table[String(id)] ?? fallback;
function chemName(c) {
  const drive = T.drives[String(c - DRIVE_BASE)];
  if (c >= DRIVE_BASE && drive) return drive;
  return T.chemicals[String(c)] ?? null;
}
function locusName(l) {
  if (T.loci[String(l)]) return T.loci[String(l)];
  if (l >= 64 && l < 128 && T.stimuli[String(l - 64)]) return `recent_${T.stimuli[String(l - 64)]}`;
  return `locus_${l}`;
}

// genes.h knackOrderOf, reduced to the stance it ranks first.
function lovedStance(k) {
  const v = [k[0], k[1], k[2]];
  for (let s = 0; s < 3; s++) {
    let rank = 0;
    for (let t = 0; t < 3; t++) if (t !== s && (v[t] < v[s] || (v[t] === v[s] && t > s))) rank++;
    if (rank === 2) return STANCES[s];
  }
  return STANCES[0];
}
const knackFor = (style) => {
  const v = [0, 0, 0];
  v[style] = 192; v[(style + 2) % 3] = 128; v[(style + 1) % 3] = 64;
  return v;
};

export function rawSheet(bytes, { stage = 2, feats = 0 } = {}) {
  const genes = parseGenome(bytes);
  const st = stageIndex(stage);
  feats = feats >>> 0;
  const expressed = [...inExpressionOrder(genes, st, feats)];

  // speciesTraitsOf: the first expressed of each, over every stage expressed so far, in genome order.
  const firstOf = (kind) => genes.find((g) => g.kind === kind && known(g) &&
    [0, 1, 2, 3].some((s) => s <= st && expressedAt(g, s, feats)));
  const sg = firstOf("species"), pg = firstOf("power"), kg = firstOf("knack");
  const spId = sg && sg.body[0] < SPECIES_COUNT ? sg.body[0] : 0;
  const sp = T.species[String(spId)];
  const styleIdx = STANCES.indexOf(sp.style);
  const power = pg ? name(T.powers, pg.body[0] < POWER_COUNT ? pg.body[0] : 0) : sp.power;
  const out = {
    species: sp.name, power, lovedStance: lovedStance(kg ? kg.body : knackFor(styleIdx)),
    element: sp.element, style: sp.style, voice: sp.voice, stage: STAGES[st],
  };

  let startle = 0;
  const hunger = [], melatonin = [], sleepiness = [];
  let hungerHalfLife = -1;
  let temper = null, size = { base: 0, growth: 0, squash: 0 };
  let meta = { pantrySize: 0, refillHours: 0, rotMinutes: 0, biteSize: 0 }, incubate = 0;
  const stimuli = {}, palette = [], marks = [], heirlooms = [];
  for (const g of expressed) {
    const b = g.body;
    switch (g.kind) {
      case "receptor":
        if (b[0] === CHEM.adrenaline && b[1] === LOCUS.startle) startle += b[4] - 128;
        break;
      case "emitter":
        if (b[1] === CHEM.hunger && b[0] === LOCUS.always) hunger.push(b[3]);
        if (b[1] === CHEM.melatonin && b[0] === LOCUS.light) melatonin.push(b[3]);
        if (b[1] === CHEM.sleepiness) sleepiness.push(b[3]);
        break;
      case "chem":
        if (b[0] === CHEM.hunger) hungerHalfLife = b[1];
        break;
      case "temperament":
        temper = b;
        break;
      case "stimulus": {
        const sname = name(T.stimuli, b[0], `stim_${b[0]}`);
        for (let i = 0; i < 3; i++) {
          const c = b[2 + i];
          if (c === 0) continue;
          const cname = chemName(c) ?? `chem_${c}`;
          stimuli[sname] ??= {};
          stimuli[sname][cname] = (stimuli[sname][cname] ?? 0) + permilleSigned(b[5 + i]);
        }
        break;
      }
      case "palette":
        palette.push({ region: T.regions[String(b[0] % REGION_COUNT)], hue: b[1] - 128, sat: b[2], val: b[3],
                       bound: b[4] !== 0 ? (chemName(b[4]) ?? "?") : null });
        break;
      case "mark":
        marks.push({ layer: b[0], variant: b[1], tint: b[2] });
        break;
      case "size":
        size = { base: b[0], growth: b[1], squash: b[2] };
        break;
      case "metabolism":
        meta = { pantrySize: b[0], refillHours: b[1], rotMinutes: b[2], biteSize: b[3] };
        break;
      case "egg":
        incubate = b[0];
        break;
      case "instinct":
        if (g.flags & FLAG.heirloom) {
          heirlooms.push({ cue: locusName(b[0]), action: name(T.actions, b[3], "?"),
                           drive: name(T.drives, b[4], "?"), level: b[5] - 128 });
        }
        break;
    }
  }
  const t = temper ?? [0, 0, 0, 0, 0, 0, 0];
  let wild = 0, motto = "", dormant = 0;
  for (const g of genes) {
    if (!known(g)) continue;
    if (g.flags & FLAG.dormant) dormant++;
    if (g.kind === "mutation") wild = g.body[5];
    if (g.kind === "note" && !motto) {
      for (const c of g.body) {
        if (!c) break;
        motto += c >= 0x20 && c < 0x7f ? String.fromCharCode(c) : "?";
      }
    }
  }
  return {
    ...out,
    startleGain: startle, hungerFill: hunger, hungerHalfLife, melatoninFill: melatonin, sleepinessFill: sleepiness,
    temperament: temper
      ? { learnRate: permilleUnit(t[0]), forgetRate: permilleUnit(t[1]), explore: permilleUnit(t[2]),
          habituation: permilleUnit(t[3]), dreamEvery: t[4] * 10, dreamLen: t[5] * 10 }
      : { learnRate: 0, forgetRate: 0, explore: 0, habituation: 0, dreamEvery: 0, dreamLen: 0 },
    stimuli, palette, marks, size, metabolism: meta, incubateMinutes: incubate, wild, motto, dormant,
    genes: genes.length, heirlooms,
  };
}

// The cache key: what the genome IS, whatever the transport did to its hex.
export async function genomeHash(bytes) {
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return [...d].map((x) => x.toString(16).padStart(2, "0")).join("");
}
