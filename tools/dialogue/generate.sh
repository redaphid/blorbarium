#!/usr/bin/env bash
# Grungo's dialogue, end to end. Safe to rerun: completions are cached in
# raw.jsonl, so only pages never seen before reach the model.
#   tools/dialogue/generate.sh
#   REFRESH_LEXICON=1 tools/dialogue/generate.sh    # re-mine sporefall-art first (changes every prompt)
#   MARQUEE_PNG=out.png tools/dialogue/generate.sh  # also render 6 lines on the round screen (needs WSL g++)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ -n "${REFRESH_LEXICON:-}" ]; then
  uv run --no-project --with nltk==3.9.1 --with wordfreq==3.1.1 \
    python "$here/lexicon.py" "${SPOREFALL_ART:-D:/Projects/sporefall-art}"
fi
python "$here/generate.py"
if [ -n "${MARQUEE_PNG:-}" ]; then
  python "$here/render_marquee.py" "$MARQUEE_PNG" 6
fi
