#!/bin/bash
# The native suites without PlatformIO: the system g++ and googletest
# (apt install libgtest-dev libssl-dev; gcovr for cov), for a machine that
# cannot reach the PlatformIO registry. wsl_test.sh stays the gate; this is
# the same [env:native] flags, built by hand.
#   bash tools/host_test.sh [plain|san|cov] [suite-substring]
# cov builds at -O0 with --coverage and prints lib/'s line coverage. Under
# -O0 test_dish's stack budget fails by design (the frames are -O0's), and
# test_first_hours wants `ulimit -s unlimited`.
set -uo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
mode=${1:-plain}
filter=${2:-}
export out=${HOST_TEST_OUT:-$repo/.pio/host-$mode}
mkdir -p "$out/obj"
cd "$repo"
flags="-std=c++17 -Wall -Wextra -Werror=narrowing -O2 -Itest/support -Ipets -Ipets/grungo"
for d in lib/*/include lib/*/src; do flags="$flags -I$d"; done
case "$mode" in
  plain) ;;
  san) flags="$flags -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all" ;;
  cov) flags="$flags -O0 --coverage"; find "$out" -name '*.gcda' -delete ;;
  *) echo "usage: $0 [plain|san|cov] [suite]" >&2; exit 2 ;;
esac
export flags
# Any header newer than an object rebuilds it: coarse, and always right.
newest=$(find lib test/support pets -name '*.h' -printf '%T@\n' | sort -n | tail -1)
export newest=${newest%.*}

lib_object() {
  local o="$out/obj/$(echo "$1" | tr / _).o"
  if [ ! -f "$o" ] || [ "$1" -nt "$o" ] || [ "$(stat -c %Y "$o")" -lt "$newest" ]; then
    g++ $flags -c "$1" -o "$o" 2>"$o.log" || { echo "LIBFAIL $1"; grep error "$o.log" | head -3; }
  fi
}
suite() {
  local t bin
  t=$(basename "$1"); bin="$out/$t"
  if ! g++ $flags -I"$1" "$1/test_main.cpp" $objs -lgtest -lgtest_main -lpthread -lcrypto -o "$bin" 2>"$bin.build.log"; then
    echo "BUILDFAIL $t"; grep error "$bin.build.log" | head -5; return
  fi
  if "$bin" --gtest_brief=1 >"$bin.log" 2>&1; then echo "PASS $t"; else
    echo "FAIL $t"; grep -E 'Failure|FAILED' "$bin.log" | head -10; fi
}
export -f lib_object suite
jobs=$(nproc)
ls lib/*/src/*.cpp | xargs -P"$jobs" -I{} bash -c 'lib_object {}'
export objs="$(ls "$out"/obj/*.o | tr '\n' ' ')"
ls -d test/test_*/ | sed 's|/$||' | grep -- "$filter" | xargs -P"$jobs" -I{} bash -c 'suite {}' | tee "$out/summary.txt"
pass=$(grep -c '^PASS' "$out/summary.txt")
fail=$(grep -cE '^(FAIL|BUILDFAIL)' "$out/summary.txt")
echo "suites: $pass passed, $fail failed"
if [ "$mode" = cov ]; then
  gcovr --root . -f lib/ --gcov-ignore-parse-errors=all --txt "$out/coverage.txt" "$out" >/dev/null && tail -2 "$out/coverage.txt" | head -1
fi
[ "$fail" = 0 ]
