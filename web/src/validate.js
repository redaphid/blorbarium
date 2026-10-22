// The mechanical checks every generated line passes before a pack holds it,
// and nothing else (the owner's rule for #37): the structure is right, every
// situation is covered, a line fits the screen (kMaxThoughtText characters),
// uses only characters the font draws, and repeats neither another line of
// this pet nor a built-in line. What fails is re-asked for; what still fails
// falls back to the built-in lines on the badge.
import catalog from "./catalog.json" with { type: "json" };

export const MAX_TEXT = catalog.maxText;
export const FONT = new Set(catalog.font);
export const MIN_LINES = 2;
export const MAX_LINES = 4;
// Typing produces these; the screen has a plain twin for each (tools/personality.py FRIENDLY).
const FRIENDLY = { "’": "'", "‘": "'", "…": "...", "–": "-", "—": "-", "“": "'", "”": "'", "‑": "-", " ": " " };

export const SITUATIONS = catalog.situations;
const BY_NAME = Object.fromEntries(SITUATIONS.map((s) => [s.name, s]));

// Case and typographic quotes are the font's business, not the model's: fixed, not failed.
export function normalise(text) {
  let t = String(text ?? "");
  for (const [k, v] of Object.entries(FRIENDLY)) t = t.split(k).join(v);
  return t.toUpperCase().replace(/\s+/g, " ").trim();
}

export function builtInLines(species) {
  const set = new Set();
  for (const s of SITUATIONS) set.add(normalise(s.shared));
  for (const lines of Object.values(catalog.species[species]?.lines ?? {})) for (const l of lines) set.add(normalise(l));
  return set;
}

// One line against one situation. Returns null if it passes, else why not.
export function lineProblem(text, situation) {
  if (typeof text !== "string") return "not a string";
  if (!text.length) return "empty";
  if (text.length > MAX_TEXT) return `${text.length} characters, the screen holds ${MAX_TEXT}`;
  const mark = situation.mark;
  const bad = [...new Set([...text].filter((c) => !FONT.has(c) && c !== mark))];
  if (bad.length) {
    const marks = bad.filter((c) => "#@*".includes(c));
    if (marks.length) return `uses ${marks.join(" ")}, which this situation does not give`;
    return `the screen cannot draw ${bad.map((c) => JSON.stringify(c)).join(" ")}`;
  }
  if (mark && text.split(mark).length > 2) return `more than one ${mark}`;
  return null;
}

// Checks a model's {situation: [lines]} for the situations asked. `kept` is
// what earlier attempts already accepted (dedupe runs across them). Returns the
// accepted lines per situation, every rejection with its reason, and which
// situations still need lines.
export function checkLines(parsed, asked, { species, kept = {} } = {}) {
  const builtIn = builtInLines(species);
  const seen = new Set(Object.values(kept).flat());
  const accepted = {}, rejected = [], structural = [];
  const lines = parsed && typeof parsed === "object" ? (parsed.lines && typeof parsed.lines === "object" ? parsed.lines : parsed) : null;
  if (!lines) return { accepted, rejected, structural: ["the reply is not a JSON object of situation: [lines]"], missing: [...asked] };
  // A key is the situation's name; a stray mark, bracket or case around it is forgiven.
  const byKey = {};
  for (const key of Object.keys(lines)) {
    const name = String(key).toLowerCase().replace(/\s*[\[(][^\])]*[\])]\s*$/, "").trim();
    if (asked.includes(name)) byKey[name] = lines[key];
    else structural.push(`"${key}" is not one of the situations asked for`);
  }
  for (const name of asked) {
    const sit = BY_NAME[name];
    let got = byKey[name];
    if (typeof got === "string") got = [got];
    if (!Array.isArray(got)) {
      if (got !== undefined) structural.push(`"${name}" is not a list of lines`);
      continue;
    }
    const ok = [...(kept[name] ?? [])];
    for (const rawLine of got) {
      const text = normalise(rawLine);
      const why = lineProblem(text, sit) ??
        (builtIn.has(text) ? "repeats a built-in line" : seen.has(text) ? "repeats another of his lines" : null);
      if (why) { rejected.push({ situation: name, text, why }); continue; }
      if (ok.length >= MAX_LINES) continue;
      seen.add(text);
      ok.push(text);
    }
    if (ok.length > (kept[name]?.length ?? 0)) accepted[name] = ok.slice(kept[name]?.length ?? 0);
  }
  const missing = asked.filter((n) => (kept[n]?.length ?? 0) + (accepted[n]?.length ?? 0) < MIN_LINES);
  return { accepted, rejected, structural, missing };
}

// The model's reply as JSON: fences and chatter around the object are tolerated.
export function parseJsonReply(text) {
  if (text && typeof text === "object") return text;
  const s = String(text ?? "");
  const start = s.indexOf("{"), end = s.lastIndexOf("}");
  if (start < 0 || end <= start) return null;
  try {
    return JSON.parse(s.slice(start, end + 1));
  } catch {
    return null;
  }
}
