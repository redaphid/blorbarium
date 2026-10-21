#!/bin/bash
# Everything a perf-climb commit must hold before it merges, one after another
# (this laptop OOMs at 8 parallel compiles): the paint bench byte for byte
# against main, the native suite, the duel goldens, and the three badge builds.
#   bash scripts/test/perf_gate.sh [ref=origin/main]
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
pio="${PIO:-$HOME/.platformio/penv/bin/pio}"
ref="${1:-origin/main}"
cd "$root"
fail=0
step() { echo "== $1"; }
step "paint bench vs $ref"
mine="$(REF="$ref" bash scripts/test/paint_bench.sh 1 | grep -o 'hash=[0-9a-f]*' | sort -u)"
echo "$mine"
[ "$(echo "$mine" | wc -l)" = 1 ] || { echo "FAIL: the hashes differ"; fail=1; }
step "native suite"
PLATFORMIO_RUN_JOBS=3 "$pio" test -e native > outputs/perf_gate_native.log 2>&1 || {
  # Another core on this laptop can swap tool-scons mid-run (ERRORED suites): run those again.
  again="$(grep -o 'native:test_[a-z_]* \[ERRORED\]' outputs/perf_gate_native.log | sed 's/native:\(test_[a-z_]*\).*/-f \1/' | tr '\n' ' ')"
  if [ -n "$again" ] && ! grep -q '\[FAILED\]' outputs/perf_gate_native.log; then
    # shellcheck disable=SC2086
    PLATFORMIO_RUN_JOBS=3 "$pio" test -e native $again >> outputs/perf_gate_native.log 2>&1 || fail=1
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
  "$pio" run -e "$env" -j 3 > "outputs/perf_gate_$env.log" 2>&1 || fail=1
  grep "RAM:\|Flash:\|SUCCESS\|FAILED" "outputs/perf_gate_$env.log" | tail -3
done
echo "== $( [ $fail = 0 ] && echo ALL HOLD || echo SOMETHING FAILED ) at $(git rev-parse --short HEAD)"
exit $fail
