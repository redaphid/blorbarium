// Workers AI over the REST API, for the eval only: the Worker itself uses the
// AI binding (web/src/worker.js). The token is CLOUDFLARE_API_TOKEN, or else
// the OAuth token `wrangler login` saved; it is never printed.
import { readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

export const ACCOUNT_ID = process.env.CLOUDFLARE_ACCOUNT_ID || "2f1143f469f762af4ef2336ec13f85b7";

function token() {
  if (process.env.CLOUDFLARE_API_TOKEN) return process.env.CLOUDFLARE_API_TOKEN;
  const toml = readFileSync(join(homedir(), ".config/.wrangler/config/default.toml"), "utf8");
  const m = toml.match(/^oauth_token\s*=\s*"([^"]+)"/m);
  if (!m) throw new Error("no CLOUDFLARE_API_TOKEN and no wrangler OAuth token: run `npx wrangler whoami`");
  return m[1];
}

// The same shape as env.AI.run(model, input): resolves to the model's result.
export function restAI({ timeoutMs = 300000 } = {}) {
  const tok = token();
  return {
    async run(model, input) {
      const ctl = new AbortController();
      const timer = setTimeout(() => ctl.abort(), timeoutMs);
      try {
        const res = await fetch(`https://api.cloudflare.com/client/v4/accounts/${ACCOUNT_ID}/ai/run/${model}`, {
          method: "POST",
          headers: { authorization: `Bearer ${tok}`, "content-type": "application/json" },
          body: JSON.stringify(input),
          signal: ctl.signal,
        });
        const body = await res.json().catch(() => ({}));
        if (!res.ok || body.success === false) {
          const why = (body.errors || []).map((e) => e.message).join("; ") || res.statusText;
          throw new Error(`${model}: HTTP ${res.status}: ${why}`);
        }
        return body.result;
      } finally {
        clearTimeout(timer);
      }
    },
  };
}
