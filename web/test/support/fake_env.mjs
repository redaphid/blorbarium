// A Worker env for node --test: D1 as the real schema in node's own SQLite,
// and a scripted Workers AI that answers the pipeline's two prompts.
import { DatabaseSync } from "node:sqlite";
import { readFileSync, readdirSync } from "node:fs";

class Stmt {
  constructor(db, sql, args = []) { this.db = db; this.sql = sql; this.args = args; }
  bind(...args) { return new Stmt(this.db, this.sql, args); }
  // D1 numbers its parameters ?1, ?2: node:sqlite takes them as an object keyed by number.
  params() {
    if (!/\?\d/.test(this.sql)) return this.args;
    return [Object.fromEntries(this.args.map((v, i) => [String(i + 1), v === undefined ? null : v]))];
  }
  async first(col) {
    const row = this.db.prepare(this.sql).get(...this.params());
    return row === undefined ? null : col ? row[col] : { ...row };
  }
  async all() { return { results: this.db.prepare(this.sql).all(...this.params()).map((r) => ({ ...r })), success: true }; }
  async run() {
    const r = this.db.prepare(this.sql).run(...this.params());
    return { success: true, meta: { last_row_id: Number(r.lastInsertRowid), changes: Number(r.changes) } };
  }
}

export function fakeD1() {
  const db = new DatabaseSync(":memory:");
  const dir = new URL("../../migrations/", import.meta.url);
  for (const f of readdirSync(dir).filter((f) => f.endsWith(".sql")).sort()) db.exec(readFileSync(new URL(f, dir), "utf8"));
  return { prepare: (sql) => new Stmt(db, sql), raw: db };
}

// Answers like a model: a bible for call 1, and for call 2 two lines per
// situation it was asked for. `mischief` makes some lines break the rules on
// the first attempt, so the retry path runs.
export function fakeAI({ mischief = false, failWith = null } = {}) {
  const calls = [];
  let n = 0;
  return {
    calls,
    async run(model, input) {
      calls.push({ model, input });
      if (failWith) throw new Error(failWith);
      const last = input.messages[input.messages.length - 1].content;
      if (/today's diary entry/.test(last)) {
        const fed = /- (\S*feeds?\S*): (\d+)/.exec(last);
        return { choices: [{ message: { content: JSON.stringify({
          entry: `Dear diary. ${fed ? `I was fed ${fed[2]} times. Delicious.` : "A quiet day."} I counted the dish twice.` }) } }] };
      }
      if (/character bible/.test(last) && !/lines for each situation/.test(last)) {
        return { choices: [{ message: { content: JSON.stringify({
          bible: "A small grumpy test pet who counts everything twice and hates puddles. ".repeat(4),
          catchphrases: ["COUNT AGAIN.", "NOT THE PUDDLE"] }) } }], usage: { prompt_tokens: 100, completion_tokens: 50 } };
      }
      const names = /keyed by exactly these names: (.*)\./.exec(last)?.[1]?.split(", ")
        ?? [...last.matchAll(/^- (\w+): /gm)].map((m) => m[1]);
      const retry = /keyed by exactly these names/.test(last);
      const lines = {};
      for (const name of names) {
        n++;
        const bad = mischief && !retry && n % 3 === 0;
        lines[name] = bad ? ["THIS LINE IS FAR TOO LONG FOR THE ROUND SCREEN", "NO {BRACES}"]
                          : [`${name.toUpperCase().replace(/_/g, " ").slice(0, 18)} ONE ${n}`, `TWO ${name.length} ${n}`];
      }
      return { choices: [{ message: { content: JSON.stringify({ lines }) } }], usage: { prompt_tokens: 1000, completion_tokens: 400 } };
    },
  };
}

export function fakeEnv(opts = {}) {
  return { DB: fakeD1(), AI: fakeAI(opts), ASSETS: { fetch: async () => new Response("asset") }, ADMIN_TOKEN: "sekrit",
           MAX_GENERATIONS_PER_DAY: "6", ...(opts.env ?? {}) };
}
