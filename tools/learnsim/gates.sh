#!/bin/bash
# Runs every regression gate for an engine change, in WSL survivor:
#   wsl -d survivor --exec bash tools/learnsim/gates.sh
# The stack-budget check, both native suites, and the e2e learning scenarios.
# Prints one PASS/FAIL line per gate and exits non-zero if any failed.
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
status=0
run() {
  local name=$1; shift
  if "$@" > "/tmp/gate_$name.log" 2>&1; then echo "PASS $name"; else echo "FAIL $name (/tmp/gate_$name.log)"; status=1; fi
  grep -E "test cases|Tests? (failed|passed)|SUMMARY|=+ [0-9]+ (test cases|passed)" "/tmp/gate_$name.log" | tail -3
}
run native bash "$root/tools/wsl_test.sh"
PIO_ENV=native_san run native_san bash "$root/tools/wsl_test.sh"
run e2e bash "$root/tools/e2e_learning.sh"
exit $status
