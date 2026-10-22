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
  // requests carry If-None-Match: R2 answers those without the body.
  const object = request.method === "HEAD"
    ? await env.FW.head(key)
    : await env.FW.get(key, { onlyIf: request.headers });
  if (object === null) return new Response("not found", { status: 404 });

  const headers = new Headers();
  object.writeHttpMetadata(headers);
  if (!headers.has("content-type")) headers.set("content-type", TYPES[key.split(".").pop()] ?? "application/octet-stream");
  headers.set("etag", object.httpEtag);
  // As the assets are served: always revalidated, so a new release shows at once.
  headers.set("cache-control", "public, max-age=0, must-revalidate");
  if (request.method === "HEAD") {
    headers.set("content-length", String(object.size));
    return new Response(null, { headers });
  }
  if (!("body" in object)) return new Response(null, { status: 304, headers });
  return new Response(object.body, { headers });
}
