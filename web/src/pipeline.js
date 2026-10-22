// #37's two calls: genes -> character bible -> lines per situation, with the
// mechanical checks and bounded retries in between. `ai` is anything with
// run(model, input) shaped like the Workers AI binding: env.AI in the Worker,
// the REST runner in the eval. Pure apart from that, so a test drives it with
// a fake.
import { PROMPTS, retryMessages } from "./prompts.js";
import { SITUATIONS, checkLines, parseJsonReply, normalise, MAX_TEXT, lineProblem } from "./validate.js";
import { traitSheet, flavourDice } from "./traits.js";

export const DEFAULT_MODEL = "@cf/moonshotai/kimi-k2.6";
export const DEFAULT_PROMPT = "v2";
export const MAX_RETRIES = 2; // re-asks per batch after the first attempt
const BATCHES = 4;

// Per-model request options (thinking off where a model allows it: these are
// short creative replies, and thinking tokens cost time and money) and prices
// in USD per million tokens (the Workers AI catalog, 2026-10-21).
export const MODELS = {
  "@cf/moonshotai/kimi-k2.6": { opts: { reasoning_effort: "none" }, price: [0.95, 4.0] },
  "@cf/deepseek-ai/deepseek-v4.1-flash-1017": { opts: { reasoning_effort: "none" }, price: [0.3, 1.2] },
  "@cf/google/gemma-4-26b-a4b-it": { opts: { chat_template_kwargs: { enable_thinking: false } }, price: [0.1, 0.3] },
  "@cf/mistralai/mistral-small-3.1-24b-instruct": { opts: {}, price: [0.351, 0.555] },
  "@cf/meta/llama-3.3-70b-instruct-fp8-fast": { opts: {}, price: [0.293, 2.253] },
  "@cf/openai/gpt-oss-120b": { opts: { reasoning_effort: "low" }, thinkTokens: 2000, price: [0.35, 0.75] },
  "@cf/qwen/qwen3.8-27b": { opts: { chat_template_kwargs: { enable_thinking: false } }, price: [0.45, 3.2] },
  "@cf/deepseek-ai/deepseek-v4-pro-0813": { opts: { reasoning_effort: "low" }, thinkTokens: 2000, price: [1.32, 3.96] },
  "@cf/zai-org/glm-5.3": { opts: {}, thinkTokens: 6000, price: [1.4, 4.4] },
  "@cf/zai-org/glm-5.3-flash": { opts: {}, thinkTokens: 6000, price: [0.15, 0.5] },
};

export function replyText(result) {
  if (result == null) return "";
  if (typeof result === "string") return result;
  const msg = result.choices?.[0]?.message;
  if (msg && msg.content != null && msg.content !== "") return msg.content;
  if (result.response != null) return typeof result.response === "string" ? result.response : JSON.stringify(result.response);
  return "";
}

// One model call, timed and costed.
const TRANSIENT_TRIES = 5;
export async function call(ai, model, messages, { maxTokens = 2500, temperature = 0.9, meter, backoffMs = 2000 } = {}) {
  const spec = MODELS[model] ?? { opts: {}, price: [0, 0] };
  const t0 = Date.now();
  // JSON mode: every candidate accepts it, and it halves the malformed replies.
  // A model that thinks first spends tokens before the reply, so it gets more.
  const input = { messages, max_tokens: maxTokens + (spec.thinkTokens ?? 0), temperature,
                  response_format: { type: "json_object" }, ...spec.opts };
  let result;
  // A busy model (429 "capacity exceeded") or a 5xx is the platform, not the
  // model's answer: wait and send the same request again, a few times.
  for (let attempt = 0; ; attempt++) {
    try {
      result = await ai.run(model, input);
      break;
    } catch (e) {
      const msg = String(e?.message ?? e);
      if (attempt >= TRANSIENT_TRIES - 1 || !/429|capacity|overloaded|HTTP 5\d\d|timed? ?out|abort|3040|internal/i.test(msg)) throw e;
      if (meter) meter.transient = (meter.transient ?? 0) + 1;
      await new Promise((r) => setTimeout(r, backoffMs * 2 ** attempt * (0.5 + Math.random())));
    }
  }
  const usage = result?.usage ?? {};
  const inTok = usage.prompt_tokens ?? 0, outTok = usage.completion_tokens ?? 0;
  if (meter) {
    meter.calls++;
    meter.ms += Date.now() - t0;
    meter.inTok += inTok;
    meter.outTok += outTok;
    meter.usd += (inTok * spec.price[0] + outTok * spec.price[1]) / 1e6;
  }
  return replyText(result);
}

export const newMeter = () => ({ calls: 0, ms: 0, inTok: 0, outTok: 0, usd: 0 });

// Call 1. A bible that is not JSON with a non-trivial "bible" string is re-asked.
export async function writeBible(ai, input, { model, prompt, meter, log }) {
  let messages = PROMPTS[prompt].bible(input);
  for (let attempt = 0; attempt <= MAX_RETRIES; attempt++) {
    const reply = await call(ai, model, messages, { maxTokens: 900, meter });
    const parsed = parseJsonReply(reply);
    const bible = typeof parsed?.bible === "string" ? parsed.bible.trim() : "";
    if (bible.split(/\s+/).length >= 40) {
      const catchphrases = (Array.isArray(parsed.catchphrases) ? parsed.catchphrases : [])
        .map(normalise).filter((c) => !lineProblem(c, { mark: null })).slice(0, 3);
      log?.push({ step: "bible", attempt, ok: true });
      return { bible, catchphrases, attempts: attempt + 1 };
    }
    log?.push({ step: "bible", attempt, ok: false, why: parsed ? "no bible of 40+ words" : "not JSON" });
    messages = [...messages, { role: "assistant", content: reply || "(empty)" },
      { role: "user", content: `That was not usable (${parsed ? "the bible must be about 120 words" : "not valid JSON"}). Reply as JSON only: {"bible": "...", "catchphrases": ["..."]}` }];
  }
  throw new Error("no usable character bible after retries");
}

// Call 2, in batches run side by side; each batch re-asks only what it still lacks.
async function writeBatch(ai, input, batch, { model, prompt, meter, log }) {
  const names = batch.map((s) => s.name);
  let messages = PROMPTS[prompt].lines(input, batch);
  const kept = {};
  const stats = { returned: 0, firstPassValid: 0, retries: 0, rejected: [] };
  let needed = names;
  for (let attempt = 0; attempt <= MAX_RETRIES && needed.length; attempt++) {
    if (attempt > 0) stats.retries++;
    let reply = "";
    try {
      reply = await call(ai, model, messages, { maxTokens: 400 + 70 * needed.length, meter });
    } catch (e) {
      log?.push({ step: "lines", attempt, ok: false, why: String(e.message ?? e) });
      continue;
    }
    const parsed = parseJsonReply(reply);
    const res = checkLines(parsed, needed, { species: input.species, kept });
    const got = Object.values(res.accepted).reduce((a, l) => a + l.length, 0);
    const returned = got + res.rejected.length;
    if (attempt === 0) { stats.returned = returned; stats.firstPassValid = got; }
    stats.rejected.push(...res.rejected.map((r) => ({ ...r, attempt })));
    for (const [k, v] of Object.entries(res.accepted)) kept[k] = [...(kept[k] ?? []), ...v];
    log?.push({ step: "lines", attempt, ok: !res.missing.length, accepted: got, rejected: res.rejected.length,
                structural: res.structural, missing: res.missing.length });
    needed = res.missing;
    if (needed.length) messages = retryMessages(messages, reply, res.rejected, needed, res.structural);
  }
  return { kept, stats };
}

// The whole pack for one pet. Never throws for a line problem: a situation
// with no usable line after the retries is left out (the badge falls back to
// its built-in lines there) and listed in `fallback`.
export async function generatePack(ai, pet, { model = DEFAULT_MODEL, prompt = DEFAULT_PROMPT, situations = SITUATIONS } = {}) {
  const meter = newMeter();
  const log = [];
  const t0 = Date.now();
  const traits = pet.traits ?? traitSheet(pet.raw, { generation: pet.generation, name: pet.name });
  const input = { species: pet.raw.species, traits, dice: flavourDice(pet.hash), name: pet.name ?? "" };
  // An edited personality (the phone #43, the dashboard #41) skips call 1: the
  // owner's words are the bible, and his catchphrases are kept if given.
  const { bible, catchphrases, attempts: bibleAttempts } = pet.bible
    ? { bible: String(pet.bible).trim(), catchphrases: pet.catchphrases ?? [], attempts: 0 }
    : await writeBible(ai, input, { model, prompt, meter, log });
  input.bible = bible;
  input.catchphrases = catchphrases;
  const size = Math.ceil(situations.length / BATCHES);
  const batches = [];
  for (let i = 0; i < situations.length; i += size) batches.push(situations.slice(i, i + size));
  const results = await Promise.all(batches.map((b) => writeBatch(ai, input, b, { model, prompt, meter, log })));
  const lines = {};
  for (const r of results) Object.assign(lines, r.kept);
  const fallback = situations.map((s) => s.name).filter((n) => !lines[n]?.length);
  const short = situations.map((s) => s.name).filter((n) => lines[n]?.length === 1);
  const sum = (k) => results.reduce((a, r) => a + r.stats[k], 0);
  return {
    bible, catchphrases, lines, fallback,
    provenance: {
      model, prompt, maxText: MAX_TEXT, bibleAttempts, retries: sum("retries"),
      firstPass: { returned: sum("returned"), valid: sum("firstPassValid") },
      rejected: results.flatMap((r) => r.stats.rejected), short, ms: Date.now() - t0, meter, log,
      dice: input.dice,
    },
    traits,
  };
}
