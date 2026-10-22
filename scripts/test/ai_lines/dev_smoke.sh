#!/usr/bin/env bash
# The Worker for real, locally: `wrangler dev` (local D1, real Workers AI),
# one POST of a fixture genome, the reply's shape, then the dev server stops.
# Nothing is deployed. Usage: bash scripts/test/ai_lines/dev_smoke.sh [fixture id]
set -uo pipefail
cd "$(dirname "$0")/../../.."   # the Worker config is at the repo root
id=${1:-mothwick-b}
port=8799
mkdir -p .jobs
# Workers AI runs remotely even in dev, through a proxy on the account's
# workers.dev, which sits behind Cloudflare Access: wrangler takes a service
# token from these (mapped from the CF_ACCESS_* names if that is what is set).
export CLOUDFLARE_ACCESS_CLIENT_ID=${CLOUDFLARE_ACCESS_CLIENT_ID:-${CF_ACCESS_CLIENT_ID:-}}
export CLOUDFLARE_ACCESS_CLIENT_SECRET=${CLOUDFLARE_ACCESS_CLIENT_SECRET:-${CF_ACCESS_CLIENT_SECRET:-}}
npx -y wrangler@4.151.3 d1 migrations apply DB --local > .jobs/dev_smoke_migrate.log 2>&1
( exec npx -y wrangler@4.151.3 dev --port "$port" > .jobs/dev_smoke.log 2>&1 ) &
pid=$!
trap 'kill "$pid" 2>/dev/null' EXIT
for _ in $(seq 60); do curl -s -o /dev/null "http://localhost:$port/" && break; sleep 1; done
genome=$(python3 -c "import json,sys; print(next(f['genome'] for f in json.load(open('web/test/fixtures/genomes.json')) if f['id']=='$id'))")
start=$(date +%s)
curl -s -m 400 -X POST "http://localhost:$port/api/pet-lines" -H 'content-type: application/json' \
  -d "{\"mac\":\"02:00:00:00:00:01\",\"genome\":\"$genome\",\"stage\":\"adult\",\"generation\":12,\"name\":\"Wick\"}" > .jobs/dev_smoke.json
echo "took $(( $(date +%s) - start )) s"
python3 -c "
import json; r=json.load(open('.jobs/dev_smoke.json'))
print({k: r[k] for k in ('version','model','prompt','cached','genome_hash') if k in r} or r)
print('bible:', r.get('bible','')[:300])
print('situations:', len(r.get('lines',{})), 'fallback:', r.get('fallback'))
for k in list(r.get('lines',{}))[:6]: print(' ', k, r['lines'][k])
print(r.get('text','').splitlines()[0] if r.get('text') else '')
"
again=$(curl -s -m 60 -X POST "http://localhost:$port/api/pet-lines" -H 'content-type: application/json' \
  -d "{\"mac\":\"02:00:00:00:00:01\",\"genome\":\"$genome\"}" | python3 -c "import json,sys; print(json.load(sys.stdin).get('cached'))")
echo "second request cached: $again"
