// node --test web/test/zone.test.mjs
// X-Blorb-Tz: the zone a badge's Wi-Fi reply carries, for its clock.
import { test } from "node:test";
import assert from "node:assert/strict";
import { zoneMinutes, withZone, TZ_HEADER } from "../src/zone.js";

test("an IANA zone is its offset at that moment, daylight saving included", () => {
  assert.equal(zoneMinutes("America/Los_Angeles", new Date("2026-07-01T12:00:00Z")), -420);
  assert.equal(zoneMinutes("America/Los_Angeles", new Date("2026-12-01T12:00:00Z")), -480);
  assert.equal(zoneMinutes("Asia/Kolkata", new Date("2026-07-01T12:00:00Z")), 330);
  assert.equal(zoneMinutes("Europe/London", new Date("2026-12-01T12:00:00Z")), 0);
  assert.equal(zoneMinutes("UTC"), 0);
});

test("no zone, or one nobody knows, is none", () => {
  assert.equal(zoneMinutes(undefined), null);
  assert.equal(zoneMinutes(""), null);
  assert.equal(zoneMinutes("Not/AZone"), null);
});

test("the header is added only when Cloudflare knew where the request came from", async () => {
  const known = withZone({ cf: { timezone: "Asia/Kolkata" } }, new Response("{}", { status: 202 }));
  assert.equal(known.headers.get(TZ_HEADER), "330");
  assert.equal(known.status, 202);
  assert.equal(await known.text(), "{}");
  const unknown = withZone({}, new Response("x"));
  assert.equal(unknown.headers.get(TZ_HEADER), null);
});
