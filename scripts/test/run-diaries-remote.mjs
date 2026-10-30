// node scripts/test/run-diaries-remote.mjs [day]
// Runs the 04:00 diary cron's work (runDiaries) once, from this machine, against
// the production D1 database and Workers AI through the Cloudflare REST API, with
// wrangler's own login. For testing the stats -> diary chain without a badge and
// without adding any trigger to the Worker. Prints no credentials.
import { readFileSync } from "node:fs";
import { homedir } from "node:os";
import { runDiaries } from "../../web/src/diary.js";

const ACCOUNT = process.env.CF_ACCOUNT_ID || "2f1143f469f762af4ef2336ec13f85b7";
const DB_ID = "75d6e526-cca9-443f-a9a9-6352a42ba41a";
const token = process.env.CLOUDFLARE_API_TOKEN ||
  /oauth_token\s*=\s*"([^"]+)"/.exec(readFileSync(`${homedir()}/.config/.wrangler/config/default.toml`, "utf8"))[1];
const api = async (path, body) => {
  const r = await fetch(`https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}${path}`, {
    method: "POST", headers: { authorization: `Bearer ${token}`, "content-type": "application/json" }, body: JSON.stringify(body) });
  const j = await r.json();
  if (!j.success) throw new Error(`${path}: ${JSON.stringify(j.errors)}`);
  return j.result;
};
const query = async (sql, params) => (await api(`/d1/database/${DB_ID}/query`, { sql, params }))[0];

const DB = {
  prepare(sql) {
    let params = [];
    const s = {
      bind(...p) { params = p; return s; },
      async all() { return { results: (await query(sql, params)).results }; },
      async first() { return (await query(sql, params)).results[0] ?? null; },
      async run() { const r = await query(sql, params); return { meta: r.meta }; },
    };
    return s;
  },
};
const AI = { async run(model, input) { return api(`/ai/run/${model}`, input); } };
const wrangler = readFileSync(new URL("../../wrangler.jsonc", import.meta.url), "utf8");
const vars = { LINES_MODEL: /"LINES_MODEL":\s*"([^"]+)"/.exec(wrangler)[1] };

const day = process.argv[2];
const n = await runDiaries({ DB, AI, ...vars }, day ? { day } : {});
console.log(`diary entries written: ${n}`);
