import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import personality as P  # noqa: E402

ENGINE = P.load_engine()
SHARED = (P.PERSONALITIES / "_shared.txt").read_text(encoding="utf-8")


def problems_for(species_text, name="grungo"):
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "_shared.txt").write_text(SHARED, encoding="utf-8")
        (root / f"{name}.txt").write_text(species_text, encoding="utf-8")
        problems = P.Problems()
        book = P.read_book(ENGINE, problems, root)
        return problems, book


class Generated(unittest.TestCase):
    def test_the_committed_tables_are_what_build_writes(self):
        engine, book, problems = P.checked()
        self.assertEqual(problems, [])
        for path, text in P.outputs(engine, book).items():
            self.assertEqual(path.read_text(), text, f"{path.name} is stale: run python3 tools/personality.py build")


class Situations(unittest.TestCase):
    def test_the_skill_explains_every_situation_and_which_give_a_number(self):
        doc = (P.ROOT / ".claude/skills/pet-personality/references/situations.md").read_text(encoding="utf-8")
        listed = {}
        for line in doc.splitlines():
            cells = [c.strip() for c in line.split("|")]
            if len(cells) > 3 and cells[1] and cells[1] not in ("situation", "---"):
                listed[cells[1]] = cells[2].startswith("**#**")
        whens = {r.when for r in ENGINE.rows}
        self.assertEqual(set(listed), whens, "situations.md and thoughts.def disagree on the situations")
        self.assertEqual({w for w, n in listed.items() if n}, ENGINE.numbered & whens)


class Check(unittest.TestCase):
    def test_small_letters_become_capitals(self):
        problems, book = problems_for("=== LINES ===\npeckish\n  i foresee flies\n")
        self.assertEqual(problems, [])
        self.assertEqual(book.own["grungo"]["peckish"], ["I FORESEE FLIES"])

    def test_a_character_the_screen_lacks_names_file_and_line(self):
        problems, _ = problems_for('=== LINES ===\npeckish\n  SAY "HI"\n')
        self.assertEqual(len(problems), 1)
        self.assertTrue(problems[0].startswith("grungo.txt:3: the screen cannot draw '\"'"), problems[0])

    def test_a_line_another_pet_already_says_is_refused(self):
        problems, _ = problems_for("=== LINES ===\nmoon\n  THE MOON IS A BIG CRUMB\n")
        self.assertEqual(len(problems), 1)
        self.assertIn("grungo.txt:3: \"THE MOON IS A BIG CRUMB\" is already said in _shared.txt", problems[0])
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "_shared.txt").write_text(SHARED, encoding="utf-8")
            (root / "grungo.txt").write_text("=== LINES ===\npeckish\n  FLIES\n", encoding="utf-8")
            (root / "mossby.txt").write_text("=== LINES ===\nstarving\n  FLIES\n", encoding="utf-8")
            problems = P.Problems()
            P.read_book(ENGINE, problems, root)
        self.assertEqual(problems, ["mossby.txt:3: \"FLIES\" is already said in grungo.txt; give him his own"])

    def test_too_long_says_by_how_much(self):
        problems, _ = problems_for("=== LINES ===\npeckish\n  " + "A" * 34 + "\n")
        self.assertIn("too long by 4", problems[0])

    def test_an_unknown_situation_suggests_the_closest(self):
        problems, _ = problems_for("=== LINES ===\npekish\n  HI\n")
        self.assertIn("Did you mean 'peckish'", problems[0])

    def test_a_number_where_the_situation_gives_none(self):
        problems, _ = problems_for("=== LINES ===\npeckish\n  # FLIES\n  I WANT # FLIES\n")
        self.assertEqual(len(problems), 1, "a line starting with # is a note")
        self.assertIn("gives none", problems[0])
        problems, _ = problems_for("=== LINES ===\nno_hat\n  GEN # AND HATLESS\n")
        self.assertEqual(problems, [])

    def test_a_situation_shared_by_several_lines_asks_which(self):
        problems, _ = problems_for("=== LINES ===\nhungry\n  HI\n")
        self.assertIn("peckish", problems[0])
        problems, book = problems_for("=== LINES ===\nchewing\n  NOM\n")
        self.assertEqual(book.own["grungo"], {"crunch": ["NOM"]}, "a situation one line uses names it")


class Say(unittest.TestCase):
    def test_his_own_line_is_his_alone(self):
        _, book, _ = P.checked()
        row = ENGINE.row("peckish")
        grungo = P.variants(ENGINE, book, row, "mystic", "grungo")
        mossby = P.variants(ENGINE, book, row, "mystic", "mossby")
        self.assertTrue(any("I FORESEE FLIES" in s for s in grungo))
        self.assertFalse(any("I FORESEE FLIES" in s for s in mossby))


if __name__ == "__main__":
    unittest.main()
