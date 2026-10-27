// /fw/<board>/manifest.json and the image it names live in the R2 bucket bound
// as FW, put there by `tools/ota_release.py publish` (docs/ota.md). A deploy of
// the site, from git or by hand, never carries or changes a release.
const TYPES = { json: "application/json", bin: "application/octet-stream" };

export async function firmware(request, env, key) {
  if (request.method !== "GET" && request.method !== "HEAD") {
    return new Response("method not allowed", { status: 405, headers: { allow: "GET, HEAD" } });
  }
  if (!key || key.split("/").some((part) => part === "" || part === "." || part === "..")) {
    return new Response("not found", { status: 404 });
  }
  // The page and its service worker fetch with cache: "no-cache", so most
  // requests carry If-None-Match: R2 answers those without the body. A Range
  // (bytes=a-, bytes=a-b, bytes=-n) is answered 206 with just those bytes.
  const ranged = request.method === "GET" && request.headers.has("range");
  let object;
  try {
    object = request.method === "HEAD"
      ? await env.FW.head(key)
      : await env.FW.get(key, ranged ? { onlyIf: request.headers, range: request.headers } : { onlyIf: request.headers });
  } catch (e) {
    if (!ranged) throw e;
    return new Response("range not satisfiable", { status: 416 });   // R2 refuses a range it cannot read
  }
  if (object === null) return new Response("not found", { status: 404 });

  const headers = new Headers();
  object.writeHttpMetadata(headers);
  if (!headers.has("content-type")) headers.set("content-type", TYPES[key.split(".").pop()] ?? "application/octet-stream");
  headers.set("etag", object.httpEtag);
  headers.set("accept-ranges", "bytes");
  // As the assets are served: always revalidated, so a new release shows at once.
  headers.set("cache-control", "public, max-age=0, must-revalidate");
  if (request.method === "HEAD") {
    headers.set("content-length", String(object.size));
    return new Response(null, { headers });
  }
  if (!("body" in object)) return new Response(null, { status: 304, headers });
  // The whole of it read from R2 before the first byte goes out: a badge reads
  // an image at 10 to 20 KB/s for minutes, and a streamed R2 body once went
  // silent at exactly 1 MiB under it (a check failed, "stalled at 1048576
  // bytes"). A few MB is well inside a Worker's memory.
  const body = await object.arrayBuffer();
  headers.set("content-length", String(body.byteLength));
  if (!ranged || !object.range) return new Response(body, { headers });
  const offset = object.range.offset ?? (object.range.suffix !== undefined ? object.size - body.byteLength : 0);
  headers.set("content-range", `bytes ${offset}-${offset + body.byteLength - 1}/${object.size}`);
  return new Response(body, { status: 206, headers });
}
