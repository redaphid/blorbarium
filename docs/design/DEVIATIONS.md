# Deviations from DESIGN.md

Each entry names what the build does differently from the design, and why.

## 1. `usage_check.cpp` lives in `tools/`, not `test/`

PlatformIO compiles every source file in the `test/` root into every test
suite. The usage sketch declares test doubles it never defines, so in `test/`
it breaks the link of every suite. It moved to `tools/usage_check.cpp`, and
`tools/syntax_check.sh` still compiles it with `-fsyntax-only`.
