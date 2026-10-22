// blorb.hypnodroid.com's Worker entry (wrangler.jsonc "main", at the repo root).
// The site's files are static assets (web/public), matched before this code
// runs, so it sees /api/* and /admin (run_worker_first), /fw/ (which
// web/public/.assetsignore keeps out of the assets), and misses.
//
// Only the default handler is exported here: the runtime takes every named
// export of the entry module for an entrypoint, so the routes, crons and
// their helpers live in app.js (and the firmware route in firmware.js).
import app from "./app.js";

export default app;
