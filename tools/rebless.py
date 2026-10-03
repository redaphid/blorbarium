"""Rewrites the pinned literals that a verbose native run reports as moved.

    python3 tools/rebless.py <log> [repo]

<log> is the output of `pio test -v` (tools/wsl_test.sh -v). For every gtest
equality failure whose expected side is a literal it can own, the literal in
the test source becomes the value the run produced:

  EXPECT_EQ(actual, "a string" "in pieces");   the pieces become the new string
  EXPECT_EQ(actual, kName);                    `kName = 0x...u` becomes the new hex

Anything else is reported and left alone, and the exit status says so, so a
behaviour failure is never blessed by accident. Prints each rewrite.
"""
import re
import sys

FAILURE = re.compile(r"^(test/[\w/]+\.cpp):(\d+): Failure$")
WRAP = 100


def blocks(log):
    """(file, line, [lines of the failure message]) for each gtest failure."""
    lines = log.splitlines()
    for i, line in enumerate(lines):
        m = FAILURE.match(line.strip())
        if not m:
            continue
        body = []
        for nxt in lines[i + 1:]:
            if FAILURE.match(nxt.strip()) or nxt.startswith("[  FAILED") or nxt.startswith("[       OK"):
                break
            body.append(nxt)
        yield m.group(1), int(m.group(2)), body


def actual_value(body):
    """The first argument's value: the line after the first `Which is:` or the expression itself."""
    if not body or body[0].strip() != "Expected equality of these values:":
        return None
    for line in body[1:]:
        s = line.strip()
        if s.startswith("Which is: "):
            return s[len("Which is: "):]
    return None


def statement(src, line):
    """The EXPECT_EQ/ASSERT_EQ call starting on `line` (1-based): (start, end) offsets of its argument list."""
    starts = [0]
    for m in re.finditer("\n", src):
        starts.append(m.end())
    at = starts[line - 1]
    m = re.compile(r"(EXPECT|ASSERT)_EQ\(").search(src, at)
    if not m or src.count("\n", at, m.start()) > 0:
        return None
    depth, i, in_str = 1, m.end(), False
    while i < len(src) and depth:
        c = src[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        i += 1
    return m.end(), i - 1


def split_args(text):
    depth, in_str, out, cur, i = 0, False, [], "", 0
    while i < len(text):
        c = text[i]
        cur += c
        if in_str:
            if c == "\\":
                cur += text[i + 1]
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            out.append(cur[:-1])
            cur = ""
        i += 1
    out.append(cur)
    return out


def literal_pieces(value, indent):
    """A C string literal (with its quotes) re-wrapped into pieces at spaces, as the tests write them."""
    inner = value[1:-1]
    pieces, cur = [], ""
    for word in re.split(r"(?<= )", inner):
        if cur and len(cur) + len(word) > WRAP:
            pieces.append(cur)
            cur = ""
        cur += word
    pieces.append(cur)
    return ("\n" + indent).join('"%s"' % p for p in pieces)


def rebless(log_text, root):
    unhandled = 0
    edits = {}
    for path, line, body in blocks(log_text):
        value = actual_value(body)
        full = root + "/" + path
        src = edits.get(full) or open(full).read()
        span = statement(src, line) if value else None
        if not span:
            print("left alone (not a pinned equality): %s:%d" % (path, line))
            unhandled += 1
            continue
        args = split_args(src[span[0]:span[1]])
        if len(args) != 2:
            print("left alone (not two arguments): %s:%d" % (path, line))
            unhandled += 1
            continue
        expected = args[1]
        stripped = expected.strip()
        if re.fullmatch(r'("([^"\\]|\\.)*"\s*)+', stripped) and value.startswith('"'):
            indent = re.match(r"\s*", expected.split("\n")[-1]).group(0) if "\n" in expected else " " * 12
            new_arg = expected[: len(expected) - len(expected.lstrip())] + literal_pieces(value, indent)
            src = src[: span[0]] + args[0] + "," + new_arg + src[span[1]:]
            print("blessed %s:%d" % (path, line))
        elif re.fullmatch(r"k\w+", stripped) and re.fullmatch(r"\d+", value):
            pat = re.compile(r"(\b%s\s*=\s*)0x[0-9a-fA-F]+u\b" % stripped)
            if not pat.search(src):
                print("left alone (no hex constant %s): %s:%d" % (stripped, path, line))
                unhandled += 1
                continue
            src = pat.sub(lambda m: "%s0x%08xu" % (m.group(1), int(value)), src, count=1)
            print("blessed %s:%d (%s = 0x%08x)" % (path, line, stripped, int(value)))
        else:
            print("left alone (expected side is not a literal): %s:%d" % (path, line))
            unhandled += 1
            continue
        edits[full] = src
    for full, src in edits.items():
        open(full, "w", newline="\n").write(src)
    return len(edits), unhandled


if __name__ == "__main__":
    log = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    changed, unhandled = rebless(log, sys.argv[2] if len(sys.argv) > 2 else ".")
    print("%d file(s) rewritten, %d failure(s) left alone" % (changed, unhandled))
    sys.exit(1 if unhandled else 0)
