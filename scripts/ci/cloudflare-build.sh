#!/usr/bin/env bash
# The checks Workers Builds (Cloudflare's CI for the blorbarium Worker) runs on
# every push, as the custom build in the root wrangler.jsonc: `npx wrangler deploy`
# (main) and `npx wrangler preview` (other branches) run it before they upload,
# and a failure here stops the upload. There it is called with --workers-ci-only,
# which returns at once anywhere WORKERS_CI is not set (wrangler dev, a deploy by hand).
# Locally, from any directory, with the desk's one-at-a-time PlatformIO:
#   PIO=/home/redaphid/Worktrees/blorbarium/pio bash scripts/ci/cloudflare-build.sh
#
# What it runs, each step timed, all of them even after one fails:
#   web       node --test web/test/*.test.mjs     (the page's sender, chooser, offline list)
#   python    tests/test_*.py                      (flash_batch, ble_ota, personality, shot...)
#   paint     scripts/test/paint_bench.sh          (host frame hashes; against main's lib/paint when
#                                                   main can be fetched, for information only)
#   native    pio test -e native                   (the googletest host suites)
# CI_BADGE=1 also builds badge128 (ESP32-S3). Off by default: PlatformIO's
# ESP32 toolchain and framework are downloaded on every build (Workers Builds
# caches npm/yarn/pnpm/bun, not ~/.platformio).
set -uo pipefail
if [ "${1:-}" = --workers-ci-only ] && [ -z "${WORKERS_CI:-}" ]; then
  echo "cloudflare-build.sh: not in Workers Builds, no checks (run it without --workers-ci-only to check)"
  exit 0
fi
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
ci="${CI_CACHE:-$HOME/.cache/blorb-ci}"
mkdir -p "$ci" outputs
fail=0
summary=()

step() {   # step <name> <command...>
  local name="$1" start=$SECONDS result
  shift
  echo
  echo "=== $name"
  if "$@"; then result=ok; else result=FAILED; fail=1; fi
  summary+=("$(printf '%-8s %-6s %4d s' "$name" "$result" $((SECONDS - start)))")
  echo "=== $name: $result in $((SECONDS - start)) s"
}

tools() {   # Pillow and cryptography for the Python tests, and PlatformIO unless $PIO names one
  python3 -m venv "$ci/venv" || return 1
  local want=(pillow==12.2.0 cryptography==50.0.2)   # cryptography: the ble_ota signing tests
  [ -n "${PIO:-}" ] || want+=(platformio==6.2.0)
  "$ci/venv/bin/pip" install --quiet --disable-pip-version-check "${want[@]}"
}

web() { node --test web/test/*.test.mjs; }

python_tests() { "$ci/venv/bin/python" -m unittest discover -s tests -p "test_*.py"; }

paint() {
  local base="" ref=""
  [ "${WORKERS_CI_BRANCH:-$(git rev-parse --abbrev-ref HEAD)}" = main ] ||
    if git fetch -q --depth=1 origin main 2>/dev/null; then base=$(git rev-parse FETCH_HEAD); fi
  [ -n "$base" ] && ref=$base
  local out
  out="$(REF="$ref" bash scripts/test/paint_bench.sh 1)" || { echo "$out"; return 1; }
  echo "$out"
  [ -n "$ref" ] || return 0
  if [ "$(grep -o 'dish_hash=[0-9a-f]*' <<< "$out" | sort -u | wc -l)" = 1 ]; then
    echo "home frames: byte-identical to main"
  else
    echo "home frames: differ from main (expected only when this branch changes the drawing)"
  fi
}

native() { "$pio" test -e native; }

badge() { "$pio" run -e badge128; }

echo "blorbarium CI: ${WORKERS_CI_BRANCH:-$(git rev-parse --abbrev-ref HEAD)} at ${WORKERS_CI_COMMIT_SHA:-$(git rev-parse HEAD)}"
echo "node $(node --version), $(python3 --version), $(g++ --version | head -1), $(nproc) CPUs"
step tools tools
pio="${PIO:-$ci/venv/bin/pio}"
step web web
step python python_tests
step paint paint
step native native
[ "${CI_BADGE:-0}" = 1 ] && step badge128 badge

echo
echo "=== summary (total $SECONDS s)"
printf '%s\n' "${summary[@]}"
exit $fail
