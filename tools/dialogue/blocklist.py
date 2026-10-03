"""The gift filter shared by lexicon.py and generate.py (rules in blocklist.txt)."""
import re
from pathlib import Path


def _load():
    words, stems = [], []
    for line in (Path(__file__).with_name("blocklist.txt")).read_text(encoding="utf-8").splitlines():
        line = line.strip().lower()
        if line and not line.startswith("#"):
            (stems if line.startswith("~") else words).append(re.escape(line.lstrip("~")))
    whole = r"\b(?:" + "|".join(words) + r")(?:s|es|ed|ing|er|ers|y)?\b"
    return re.compile(whole + (r"|" + "|".join(stems) if stems else ""), re.I)


BLOCKED = _load()


def blocked(text: str) -> bool:
    return BLOCKED.search(text) is not None
