#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""P5's shipped list from the programs marked as favourites on the FM-1 (docs/PROPHET.md "The shipped list from the
unit's favourites").

  python3 tools/fm1_install.py --favorites > build/scratch/favorites.txt      # reads the unit, writes nothing to it
  python3 tools/p5_shipped_from_favorites.py build/scratch/favorites.txt      # the new list, printed
  python3 tools/p5_shipped_from_favorites.py build/scratch/favorites.txt --write   # .. into assets/prophet5-factory/shipped.txt
  python3 tools/p5_shipped_from_favorites.py FAVS --out FILE [--current FILE] [--by-number]

FAVS: the text `fm1_install.py --favorites` prints ("-": standard input), or any file in shipped.txt's form: a
Sequential program number 1..200 a line, "#" starts a comment.

THE ORDER of the list written. A marked program that is in the present list (--current, default
assets/prophet5-factory/shipped.txt) keeps its place among the others, its comment and its group heading: the present
list is grouped by hand (pads, brass, keys ..) and that order is kept. The marked programs that are NOT in the present
list follow under a heading of their own, by Sequential number, each with its name. A group left without a program
loses its heading. --by-number: no groups, every marked program by Sequential number.

After --write: ./build.sh, then python3 tools/p5_goldens.py, then the suite (docs/PROPHET.md "The shipped list").
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_prophet_factory as g                     # noqa: E402  (the bank's names, the list's reader)


def marked(text):
    """the program numbers of a favourites text / a shipped-list text, in the text's order, each once"""
    ids = []
    for no, line in enumerate(text.splitlines(), 1):
        word = line.split("#", 1)[0].split()
        if not word:
            continue
        if len(word) != 1 or not word[0].isdigit() or not 1 <= int(word[0]) <= 200:
            raise ValueError(f"line {no}: {line.strip()!r}: one program number 1..200 a line (comments after #)")
        if int(word[0]) not in ids:
            ids.append(int(word[0]))
    return ids


def build(favs, current_text, by_number=False):
    """-> the new list's text"""
    name = dict(enumerate(g.names(g.records()), 1))
    want = set(favs)
    if not want:
        raise ValueError("no P5 program is marked (INIT alone is FELUCCA_P5_FACTORY=0, not an empty list)")
    out = ["# P5's shipped factory programs (docs/PROPHET.md \"The shipped list\"; tools/gen_prophet_factory.py reads this file).",
           "#",
           "# One Sequential program number (1..200, the program's place in prophet5-v1.03.syx) a line, in the order of the",
           "# pool: the first line is PRESETS 01. \"#\" starts a comment. A program's number is its identity.",
           "#",
           f"# {len(want)} programs: the ones marked as favourites on the owner's FM-1 (tools/fm1_install.py --favorites,",
           "# tools/p5_shipped_from_favorites.py). After a change: ./build.sh, python3 tools/p5_goldens.py, the suite.",
           ""]
    if by_number:
        return "\n".join(out + [f"{i:<6d} # {name[i]}" for i in sorted(want)]) + "\n"
    kept, pending = set(), None
    for line in current_text.splitlines():
        word = line.split("#", 1)[0].split()
        if not word:
            if line.startswith("# ----"):         # a group's heading: written with its first program that stays
                pending = re.sub(r"\s*\(\d[^)]*\)\s*$", "", line)
            continue
        if len(word) == 1 and word[0].isdigit() and int(word[0]) in want and int(word[0]) not in kept:
            if pending:
                out.append(pending)
                pending = None
            out.append(line.rstrip())
            kept.add(int(word[0]))
    new = sorted(want - kept)
    if new:
        out.append("# ---- marked on the unit, not in the list before (by Sequential number)")
        out += [f"{i:<6d} # {name[i]}" for i in new]
    return "\n".join(out) + "\n"


def main(a):
    if not a or a[0] in ("-h", "--help"):
        sys.exit(__doc__)
    src, out, cur, by_number, k = a[0], None, g.SHIPPED, False, 1
    while k < len(a):
        if a[k] == "--write":
            out = g.SHIPPED
        elif a[k] == "--by-number":
            by_number = True
        elif a[k] in ("--out", "--current") and k + 1 < len(a):
            if a[k] == "--out":
                out = Path(a[k + 1])
            else:
                cur = Path(a[k + 1])
            k += 1
        else:
            sys.exit(__doc__)
        k += 1
    try:
        favs = marked(sys.stdin.read() if src == "-" else Path(src).read_text())
        text = build(favs, Path(cur).read_text() if Path(cur).exists() else "", by_number)
        g_ids = marked(text)
        if sorted(g_ids) != sorted(set(favs)):
            raise ValueError("internal: the list written does not hold exactly the marked programs")
    except (ValueError, OSError) as e:
        sys.exit(f"p5_shipped_from_favorites.py: {e}")
    if out is None:
        sys.stdout.write(text)
    else:
        Path(out).write_text(text)
        print(f"{out}: {len(g_ids)} programs", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
