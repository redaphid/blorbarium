#!/bin/bash
# Everything a perf-climb commit must hold before it merges, one after another
# (one compile at a time on this laptop: PIO is its locking wrapper): the home
# frames byte for byte against main (dish_hash; the duel's frames move with the
# duel's own work, so they are held by the duel goldens instead), the native
# suite, the duel goldens, and the three badge builds.
#   bash scripts/test/perf_gate.sh [ref=origin/main]
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
pio="${PIO:-/home/redaphid/Worktrees/blorbarium/pio}"; [ -x "$pio" ] || pio=$HOME/.platformio/penv/bin/pio
export PIO="$pio"   # duel_film.sh and the benches build through it too
ref="${1:-origin/main}"
cd "$root"
fail=0
step() { echo "== $1"; }
step "home frames vs $ref (dish_hash)"
bench="$(REF="$ref" bash scripts/test/paint_bench.sh 1)"
homes="$(grep -o 'dish_hash=[0-9a-f]*' <<< "$bench" | sort -u)"
echo "$homes"; grep -o '[^ ]*hash=[0-9a-f]*$' <<< "$bench" | sed 's/^/  (all frames) /'
[ "$(echo "$homes" | wc -l)" = 1 ] || { echo "FAIL: the home frames differ"; fail=1; }
step "native suite"
"$pio" test -e native > outputs/perf_gate_native.log 2>&1 || {
  # Another core on this laptop can swap tool-scons mid-run (ERRORED suites): run those again.
  again="$(grep -o 'native:test_[a-z_]* \[ERRORED\]' outputs/perf_gate_native.log | sed 's/native:\(test_[a-z_]*\).*/-f \1/' | tr '\n' ' ')"
  if [ -n "$again" ] && ! grep -q '\[FAILED\]' outputs/perf_gate_native.log; then
    # shellcheck disable=SC2086
    "$pio" test -e native $again >> outputs/perf_gate_native.log 2>&1 || fail=1
  else
    fail=1
  fi
}
grep "test cases" outputs/perf_gate_native.log
step "duel goldens"
bash tools/duel_film.sh > outputs/perf_gate_duel.log 2>&1 || fail=1
tail -3 outputs/perf_gate_duel.log
for env in badge128 badge128_pm badge128_flip; do
  step "$env"
  "$pio" run -e "$env" > "outputs/perf_gate_$env.log" 2>&1 || fail=1
  grep "RAM:\|Flash:\|SUCCESS\|FAILED" "outputs/perf_gate_$env.log" | tail -3
done
echo "== $( [ $fail = 0 ] && echo ALL HOLD || echo SOMETHING FAILED ) at $(git rev-parse --short HEAD)"
exit $fail
