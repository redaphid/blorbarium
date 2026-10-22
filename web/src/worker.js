// The Worker's entry (wrangler.jsonc "main"). Only the default handler is
// exported here: the runtime takes every named export of the entry module for
// an entrypoint, so the routes, crons and their helpers live in app.js.
import app from "./app.js";

export default app;
