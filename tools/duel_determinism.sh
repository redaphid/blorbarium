#!/bin/bash
# Two devices duel only if they resolve the same battle the same way, and one
# of them may be a host build (the simulator, the tests) and the other an S3.
# So the golden battles test_duel_resolve pins are evaluated here by each
# compiler itself, in a static_assert over the constexpr core: if the S3's
# arithmetic differed anywhere, its build of this file would fail. Then one
# golden's hash is planted wrong and both compilers must refuse it, so the
# check is known to bite. Run in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/duel_determinism.sh
set -u
repo="$(cd "$(dirname "$0")/.." && pwd)"
xt=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$repo/test/test_duel_resolve/goldens.h" "$work/"
cat > "$work/check.cpp" <<'EOF'
#include "duel_resolve.cpp"
#include "goldens.h"
constexpr int broken =
    duelgolden::firstBroken([](const auto&... x) { return blorb::duel_detail::resolveCore(x...); });
static_assert(broken == -1, "a golden battle resolves differently on this compiler");
EOF

compile() {   # compile <label> <compiler and its flags...>
  local label="$1"; shift
  "$@" -fsyntax-only -I"$work" -I"$repo/lib/blorb/include" -I"$repo/lib/blorb/src" -x c++ "$work/check.cpp" 2>&1
}
fail=0
for c in host s3; do
  if [ "$c" = s3 ] && [ ! -x "$xt" ]; then echo "FAIL the S3 compiler is missing: $xt"; fail=1; continue; fi
  if [ "$c" = host ]; then cmd=(g++ -std=c++17); else cmd=("$xt" -std=gnu++17); fi
  n=$(grep -c '^     .*0x[0-9a-f]*u},$' "$work/goldens.h")
  if out=$(compile "$c" "${cmd[@]}"); then echo "ok   $n golden battles resolve to their pinned hashes on the $c compiler"
  else echo "FAIL $c compiler"; echo "$out" | grep -E "error|reduces" | head -5; fail=1; fi
done

cp "$work/goldens.h" "$work/goldens.h.orig"
sed -i '0,/0x[0-9a-f]\{8\}u},$/s//0x00000000u},/' "$work/goldens.h"
if cmp -s "$work/goldens.h" "$work/goldens.h.orig"; then echo "FAIL the plant matched nothing"; fail=1; fi
for c in host s3; do
  if [ "$c" = host ]; then cmd=(g++ -std=c++17); else cmd=("$xt" -std=gnu++17); fi
  [ "$c" = s3 ] && [ ! -x "$xt" ] && continue
  if out=$(compile "$c" "${cmd[@]}"); then echo "FAIL a planted wrong hash compiled on the $c compiler"; fail=1
  elif grep -q "a golden battle resolves differently on this compiler" <<<"$out"; then
    echo "ok   a planted wrong hash is refused on the $c compiler"
  else echo "FAIL the plant was refused for another reason on $c"; echo "$out" | grep error | head -3; fail=1; fi
done
exit $fail
