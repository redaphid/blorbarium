// The badge's zone, for its clock (lib/wifiota/net_clock.h): a badge that only
// ever meets Wi-Fi learns the time from a reply's Date header and its local
// offset from X-Blorb-Tz, which is Cloudflare's guess at where the request came
// from (request.cf.timezone, an IANA name) as minutes east of UTC right now.
export const TZ_HEADER = "x-blorb-tz";

// Minutes east of UTC that IANA zone `tz` is at `at`; null when unknown.
export function zoneMinutes(tz, at = new Date()) {
  if (!tz || typeof tz !== "string") return null;
  let name;
  try {
    name = new Intl.DateTimeFormat("en-US", { timeZone: tz, timeZoneName: "longOffset" })
      .formatToParts(at).find((p) => p.type === "timeZoneName")?.value;
  } catch {
    return null;   // not a zone this runtime knows
  }
  if (name === "GMT") return 0;
  const m = /^GMT([+-])(\d{2}):(\d{2})$/.exec(name ?? "");
  if (!m) return null;
  const minutes = Number(m[2]) * 60 + Number(m[3]);
  return m[1] === "-" ? -minutes : minutes;
}

// The response with X-Blorb-Tz added when the request's zone is known.
export function withZone(request, response) {
  const minutes = zoneMinutes(request.cf?.timezone);
  if (minutes === null) return response;
  const out = new Response(response.body, response);   // a fetched response's headers may be immutable
  out.headers.set(TZ_HEADER, String(minutes));
  return out;
}
