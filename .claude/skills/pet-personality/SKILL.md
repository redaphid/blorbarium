---
name: pet-personality
description: Turn the owner's character notes in personalities/*.txt into the lines the blorbarium pets say on screen, check them, preview them in each pet's voice, and prove the build still passes. Use when asked to touch up the pets' personalities, turn my notes into lines, make grungo (or any species) funnier, give a pet new things to say, rewrite what the pets say, or apply the personality notes written offline.
---

# Pet personality

The owner writes characters; you turn them into lines. Each species has a
file, `personalities/<species>.txt`, with a free-form character sheet above
the `=== LINES ===` marker and the species' own lines below it.
`personalities/_shared.txt` holds the one line every pet shares for each
situation. `tools/personality.py` (standard library only) checks them,
previews them, and writes the C tables.

The pets go to children aged 6 to 13. Every line must suit them.

## The rules a line obeys

- Only `A-Z 0-9`, space and `. , ! ? - ' :`. Small letters are made capitals for you.
- At most 30 characters. The checker says by how much a line is over.
- A `#` shows a number, and only where the situation gives one
  (`references/situations.md` marks those). A line starting with `#` is a note.
- Lines sit indented under a line name from `_shared.txt` (`peckish`, `p_mud`),
  or under a situation that only one line uses.
- A species' lines for a line name replace the shared line for that species.
  Several lines under one name: he draws one each time.
- His voice wraps every line (the sheet's first lines show how). A `terse`
  voice (mudge) keeps only the first sentence, so lead with the punch.

## What you never change

The `id`, `when`, `priority` and `cooldownS` columns of
`lib/blorb/include/blorb/defs/thoughts.def`, and the generated files
(`species_lines.def`, `test/test_thoughts/said_lines.inc`, and the text column
of `thoughts.def`, which `build` writes from `_shared.txt`). A new situation or
a new line name is a code change: ask the owner before adding a row, and
never renumber or reuse an id.

## Workflow

1. Save the before: `python3 tools/personality.py say <species> > <scratchpad>/before-<species>.txt`
   for each species you will touch (`say all` for every species).
2. Read `personalities/<species>.txt` in full, and `_shared.txt` for the line
   names. Read `references/situations.md` when you need what a situation means.
3. Write lines in the character the sheet describes, in the owner's words where
   he gave them. Lines he wrote himself under `=== LINES ===` are his: keep
   them, fix only what the checker rejects, and tell him what you changed. Cover
   the situations his notes care about; you need not fill every one.
4. `python3 tools/personality.py check` until it prints `ok`. Every problem
   names `file:line`.
5. `python3 tools/personality.py say <species>` and read it as the screen
   will say it. `*` marks his own lines.
6. `python3 tools/personality.py build`.
7. Tests, judged on exit status, not on piped output:
   - `MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/Projects/<checkout>/tools/wsl_test.sh test_thoughts`
   - the goldens: `MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/Projects/<checkout>/tools/sim_film.sh`.
     A changed shared line can change a golden. Look at the changed PNG and say
     why it changed before re-blessing it.
8. Show the owner a before and after table per species: line name, the line
   before, the line after, as `say` prints them. Ask before committing.
9. Commit `personalities/` with the generated files, then `git push -u origin HEAD`.

## Seeing it on a board

Lines reach a pet with the next firmware flash, which is the owner's call.
