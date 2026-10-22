// The eval's automatic metrics (no model involved): diversity within one
// pet's lines, overlap between two pets' lines, and the run's mechanics.
const words = (s) => s.toUpperCase().replace(/[^A-Z0-9' ]+/g, " ").split(/\s+/).filter(Boolean);
const ngrams = (ws, n) => {
  const out = [];
  for (let i = 0; i + n <= ws.length; i++) out.push(ws.slice(i, i + n).join(" "));
  return out;
};

export const allLines = (pack) => Object.values(pack.lines ?? {}).flat();

// distinct-2: unique word bigrams over all bigrams, across one pet's lines.
// 1.0 = no bigram ever repeats; low = he keeps saying the same thing.
export function distinct2(lines) {
  const grams = lines.flatMap((l) => ngrams(words(l), 2));
  return grams.length ? new Set(grams).size / grams.length : 0;
}

// The share of lines whose opening two words open at least two other lines too.
export function repeatedOpenings(lines) {
  const open = lines.map((l) => words(l).slice(0, 2).join(" "));
  const n = {};
  for (const o of open) n[o] = (n[o] ?? 0) + 1;
  return lines.length ? open.filter((o) => n[o] >= 3).length / lines.length : 0;
}

// Jaccard overlap of two pets' word-bigram sets: 0 = nothing shared.
export function overlap(a, b) {
  const A = new Set(a.flatMap((l) => ngrams(words(l), 2)));
  const B = new Set(b.flatMap((l) => ngrams(words(l), 2)));
  let inter = 0;
  for (const g of A) if (B.has(g)) inter++;
  const union = A.size + B.size - inter;
  return union ? inter / union : 0;
}

export const cosine = (a, b) => {
  let dot = 0, na = 0, nb = 0;
  for (let i = 0; i < a.length; i++) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
  return dot / Math.sqrt(na * nb);
};

export function mechanics(pack, situationCount) {
  const p = pack.provenance;
  const lines = allLines(pack);
  const firstReplies = p.log.filter((l) => l.step === "lines" && l.attempt === 0 && !l.why); // a transport error is no reply
  const jsonOk = firstReplies.filter((l) => !(l.structural ?? []).some((s) => /not a JSON object/.test(s))).length;
  return {
    jsonFirstPass: firstReplies.length ? jsonOk / firstReplies.length : 0,
    passFirst: p.firstPass.returned ? p.firstPass.valid / p.firstPass.returned : 0,
    coverage: 1 - pack.fallback.length / situationCount,
    fallback: pack.fallback.length,
    retries: p.retries + Math.max(0, p.bibleAttempts - 1),
    lines: lines.length,
    perSituation: lines.length / Math.max(1, situationCount - pack.fallback.length),
    meanLen: lines.length ? lines.reduce((a, l) => a + l.length, 0) / lines.length : 0,
    distinct2: distinct2(lines),
    repeatedOpenings: repeatedOpenings(lines),
    ms: p.ms,
    usd: p.meter.usd,
    transient: p.meter.transient ?? 0,
    calls: p.meter.calls,
    rejectReasons: countReasons(p.rejected),
  };
}

function countReasons(rejected) {
  const out = {};
  for (const r of rejected) {
    const k = /characters/.test(r.why) ? "too long" : /cannot draw/.test(r.why) ? "charset" : /does not give|more than one/.test(r.why)
      ? "mark" : /built-in/.test(r.why) ? "dup built-in" : /another/.test(r.why) ? "dup own" : r.why;
    out[k] = (out[k] ?? 0) + 1;
  }
  return out;
}
