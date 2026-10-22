# blorb.hypnodroid.com

The page that updates and renames a dish over Web Bluetooth. One Worker,
`blorbarium`, configured by `wrangler.jsonc` at the repo root:

- `web/public/`: the site, served as static assets. `.assetsignore` leaves `fw/` out.
- `web/src/worker.js`: runs only for what no asset answers. It serves
  `/fw/<board>/manifest.json` and the image it names from the R2 bucket
  `blorbarium-fw` (binding `FW`).
- `web/test/`: `npm test` (node), and the Playwright page tests (`docs/ota.md`, "Tests").

Everything below runs from the repo root, after `npm ci` (wrangler is pinned in `package.json`).

## Develop

```
uv run --with cryptography python tools/ota_release.py publish web/public/fw/lcd128/manifest.json --local
npx wrangler dev
```

`publish --local` puts a release in the local bucket `wrangler dev` serves
(`.wrangler/state`), so `/fw/` answers there. The image must be beside the
manifest first: `tools/ota_release.py release` writes it, or copy the live one
from https://blorb.hypnodroid.com/fw/lcd128/.

## Deploy

Workers Builds deploys on every push (Worker > Settings > Build, connected to
this repo, root `/`, no build command): `npx wrangler deploy` on main,
`npx wrangler preview` on other branches. Both first run the custom build in
`wrangler.jsonc`, `scripts/ci/cloudflare-build.sh` (the web, Python, paint
bench and native checks), and a failing check stops the upload. By hand,
`npx wrangler deploy` does the same, without the checks (they run only where
`WORKERS_CI` is set; `npm run check` runs them here).

A deploy carries the site only. Firmware is published separately, with
`tools/ota_release.py publish` (`docs/ota.md`, "Releasing"), so no deploy, from
git or by hand, can publish a manifest without its image.
