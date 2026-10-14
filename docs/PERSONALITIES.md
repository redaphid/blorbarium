# Writing the pets' personalities

Each species has a plain text file in `personalities/`: `grungo.txt`,
`mossby.txt`, `puffkin.txt`, `gilly.txt`, `mothwick.txt`, `mudge.txt`,
`fizz.txt`, `hushwing.txt`, `tinker.txt`. Open one in any editor.

## On the plane

Write about the character above the `=== LINES ===` line, in any form you like:
who he is, how he talks, what he loves and fears, his catchphrases, a story.
Nothing there reaches the screen. It is what Claude reads later.

If a line comes to you, write it below the marker, indented under the line
name it replaces (the names are in `_shared.txt`):

```
=== LINES ===
peckish
  I FORESEE FLIES. IN MY MOUTH.
  FLIES. FLIES. FLIES.
```

A pet with several lines for one name picks one each time. A line has at most
30 characters, from `A-Z 0-9`, space and `. , ! ? - ' :`. Small letters are fine.
To change the line every pet shares, edit it in `_shared.txt`.

Python is already on this laptop and the tool needs nothing downloaded, so
these work with the network off:

```
python3 tools/personality.py check          what is wrong, file and line
python3 tools/personality.py say grungo     every line grungo can say, as the screen says it
python3 tools/personality.py say all
```

What each situation means: `.claude/skills/pet-personality/references/situations.md`.

## Back online

Ask Claude to "apply my personality notes" (or "make grungo funnier"). It
turns your notes into lines, checks and previews them, runs the tests, and
shows you a before and after table for each species before it commits.
The lines reach a pet with its next firmware flash.
