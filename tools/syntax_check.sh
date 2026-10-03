#!/bin/bash
# Compiles every header on its own, then the usage sketch, with the flags the
# design promises. Run in WSL survivor:
#   MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/syntax_check.sh
cd "$(dirname "$0")/.." || exit 1
flags=(-std=c++17 -fsyntax-only -Wall -Wextra -Werror=narrowing -Ilib/blorb/include -Ilib/paint/include)
fail=0
shopt -s nullglob
for h in lib/blorb/include/blorb/*.h lib/paint/include/paint/*.h; do
  inc=${h#lib/*/include/}
  if out=$(echo "#include \"$inc\"" | g++ "${flags[@]}" -x c++ - 2>&1) && [ -z "$out" ]; then
    echo "ok   $h"
  else
    echo "FAIL $h"; echo "$out" | head -30; fail=1
  fi
done
if out=$(g++ "${flags[@]}" tools/usage_check.cpp 2>&1) && [ -z "$out" ]; then
  echo "ok   tools/usage_check.cpp"
else
  echo "FAIL tools/usage_check.cpp"; echo "$out" | head -40; fail=1
fi
g++ --version | head -1
exit $fail
