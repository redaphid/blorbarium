// The pack as the badge stores it (docs/design/AI-LINES.md): one text file,
// signed with ECDSA P-256 over SHA-256, the scheme the badge already verifies
// for OTA images (lib/ota verifyP256), with its own key.
//
//   blorbpack1 <genome hash, first 16 hex> <pack version> <line count>
//   <situation name> <TAB> <LINE>
//   ...
//
// Every LINE has passed validate.js, so it holds only the font's characters
// and never a tab or a newline.

export const PACK_MAGIC = "blorbpack1";

export function packText({ genomeHash, version, lines }) {
  const rows = [];
  for (const name of Object.keys(lines).sort()) for (const l of lines[name]) rows.push(`${name}\t${l}`);
  return `${PACK_MAGIC} ${genomeHash.slice(0, 16)} ${version} ${rows.length}\n${rows.join("\n")}\n`;
}

const b64url = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const unb64 = (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));

// PACK_SIGNING_KEY: the PKCS#8 private key, base64 (a Worker secret). None set: unsigned.
export async function signPack(text, pkcs8b64) {
  if (!pkcs8b64) return null;
  const key = await crypto.subtle.importKey("pkcs8", unb64(pkcs8b64), { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
  // WebCrypto's ECDSA signature is raw r || s, as verifyP256 takes it.
  const sig = await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, key, new TextEncoder().encode(text));
  return b64url(sig);
}

export async function verifyPack(text, sigB64, spkiB64) {
  const key = await crypto.subtle.importKey("spki", unb64(spkiB64), { name: "ECDSA", namedCurve: "P-256" }, false, ["verify"]);
  return crypto.subtle.verify({ name: "ECDSA", hash: "SHA-256" }, key, unb64(sigB64), new TextEncoder().encode(text));
}
