#!/usr/bin/env bash
# The build command for Workers Builds (Cloudflare's CI for the blorbarium
# Worker, which serves web/ as blorb.hypnodroid.com). Every push and pull
# request runs it before the deploy or preview command; a failure here stops
# the upload. The Worker's build settings (Settings > Build):
#   root directory:  web
#   build command:   bash ../scripts/ci/cloudflare-build.sh
#   main:            npx -y wrangler@4.149.0 versions upload   (a version, no traffic: the live
#                    site's fw/*.bin is not in git, so a deploy from git would drop it)
#   other branches:  npx -y wrangler@4.149.0 versions upload   (a version with a preview URL)
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

tools() {   # Pillow for the Python tests, and PlatformIO unless $PIO names one
  python3 -m venv "$ci/venv" || return 1
  local want=(pillow==12.2.0)
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
