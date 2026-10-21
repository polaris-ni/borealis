#!/usr/bin/env python3
"""
check_display_literals.py - keep the entry table the single source of on-screen text.

Why this gate exists
--------------------
Borealis renders its UI from one table: `src/ui/settings_i18n.cpp` holds both locale tiers
(`kStringsZh` and `kStringsEn`), and every on-screen phrase is looked up from it at startup.
`SPEC.FEAT.PREF.05` shipped the second tier, and with it the failure mode changed shape: a
phrase that lives outside the table is invisible to the English tier, so an English UI shows
Chinese - or, when the lookup misses, shows nothing at all (a `LocalizedString` built by
`tr()` carries an empty `text`, so a missing key renders blank, never a fallback).

Measured while closing that requirement: quoted CJK phrases outside the table went from 6
down to 0. This gate locks that number, because the regression is trivially easy to reintroduce
- one dialog title typed inline is enough, and no other test sees it: `main.cpp` is not linked
into the test runner, and the viewport's dialog is only now asserted by key name.

Rules
-----
- Every string or character literal containing a CJK ideograph (U+4E00..U+9FFF), in `src/**`
  and `include/**`, must live in the entry table file. Anywhere else it is a hard error.
- Only the literals are scanned: comments are stripped first, because comments are allowed to
  be Chinese (AGENTS.md 4.3 rule 14).
- Layout symbols (an arrow, a U+00B7 separator, a glyph) are deliberately NOT in scope. They
  are marked `CJK-LITERAL: <category>` where they occur and are not translatable phrases.
- Scanning 0 files, or failing to locate the entry table, is a hard error - a check that
  inspects nothing must fail, never pass vacuously.

Usage
-----
  python3 tools/check/check_display_literals.py [--root <borealis_root>]
"""
import argparse
import os
import re
import sys

ENTRY_TABLE_REL = os.path.join("src", "ui", "settings_i18n.cpp")
SCANNED_DIRS = ("src", "include")
SCANNED_SUFFIXES = (".cpp", ".h", ".hpp")

# CJK Unified Ideographs: the range the translatable phrases are written in.
CJK_IDEOGRAPH_RE = re.compile("[\\u4e00-\\u9fff]")
QUOTE_CHARS = ('"', "'")
# Narrow, UTF-8, UTF-16 and UTF-32 string literals plus character literals, with escapes kept
# inside the match so that a quote in "..." cannot end the scan early.
STRING_LITERAL_RE = re.compile(rb'(?:u8|U|u|L)?"(?:\\.|[^"\\\n])*"' rb"|'(?:\\.|[^'\\\n])+'")


def blank_comments(text):
    """Blank out comments, character by character, keeping newlines so line numbers survive.

    A regex for this would have to choose an order and get it wrong: stripping "//" first hides
    any literal after a URL on the same line (the "//" of "https://..." reads as a comment start),
    while stripping literals first lets a "//" inside a comment open a string that never closes.
    One pass over the characters, tracking whether we are inside a literal, is the only form that
    is correct in both directions - and a gate whose job is to not miss a phrase must not be
    maskable by the punctuation of an unrelated line.
    """
    out = []
    index = 0
    size = len(text)
    while index < size:
        char = text[index]
        nxt = text[index + 1] if index + 1 < size else ""
        if char in QUOTE_CHARS:
            quote = char
            out.append(char)
            index += 1
            while index < size:
                if text[index] == "\\":
                    out.append(text[index:index + 2])
                    index += 2
                    continue
                out.append(text[index])
                if text[index] == quote or text[index] == "\n":
                    index += 1
                    break
                index += 1
            continue
        if char == "/" and nxt == "/":
            while index < size and text[index] != "\n":
                index += 1
            continue
        if char == "/" and nxt == "*":
            end = text.find("*/", index + 2)
            stop = size if end < 0 else end + 2
            for skipped in text[index:stop]:
                if skipped == "\n":
                    out.append("\n")
            index = stop
            continue
        out.append(char)
        index += 1
    return "".join(out)


def repo_root_of(path):
    """Walk up to the repo root: the ancestor holding both CMakeLists.txt and AGENTS.md."""
    d = os.path.dirname(os.path.abspath(path))
    previous = None
    while d != previous:
        if os.path.isfile(os.path.join(d, "CMakeLists.txt")) and os.path.isfile(os.path.join(d, "AGENTS.md")):
            return d
        previous, d = d, os.path.dirname(d)
    return None


def literals_with_cjk(stripped_text):
    """Return (line_number, literal) for every literal in the text holding a CJK ideograph.

    Matched over UTF-8 bytes, so the offsets are byte offsets; line numbers therefore count
    newlines in the byte buffer rather than in the str, where they would drift on any line
    that already holds non-ASCII text.
    """
    encoded = stripped_text.encode("utf-8")
    hits = []
    for match in STRING_LITERAL_RE.finditer(encoded):
        chunk = match.group(0)
        decoded = chunk.decode("utf-8", errors="replace")
        if not CJK_IDEOGRAPH_RE.search(decoded):
            continue
        hits.append((encoded.count(b"\n", 0, match.start()) + 1, decoded))
    return hits


def iter_scanned_files(root):
    """Yield every scannable source path under the root, entry table included."""
    for top in SCANNED_DIRS:
        base = os.path.join(root, top)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames.sort()
            for name in sorted(filenames):
                if name.endswith(SCANNED_SUFFIXES):
                    yield os.path.join(dirpath, name)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=None, help="repository root (default: derived from this script)")
    args = parser.parse_args(argv)

    root = args.root or repo_root_of(__file__)
    if root is None:
        print("FAIL: cannot locate the repository root (needs CMakeLists.txt and AGENTS.md together)")
        return 1

    table_path = os.path.join(root, ENTRY_TABLE_REL)
    if not os.path.isfile(table_path):
        print("FAIL: the entry table is missing at %s - the gate has nothing to whitelist"
              % os.path.relpath(table_path, root))
        return 1

    files = [p for p in iter_scanned_files(root) if os.path.abspath(p) != os.path.abspath(table_path)]
    if not files:
        print("FAIL: no source file scanned under %s - a check that inspects nothing must not pass"
              % ", ".join(os.path.join(root, top) for top in SCANNED_DIRS))
        return 1

    offenders = []
    for path in files:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                text = blank_comments(handle.read())
        except (OSError, UnicodeDecodeError) as error:
            print("FAIL: cannot read %s: %s" % (os.path.relpath(path, root), error))
            return 1
        for line, literal in literals_with_cjk(text):
            offenders.append((os.path.relpath(path, root), line, literal))

    if offenders:
        print("FAIL: %d on-screen CJK literal(s) live outside the entry table (%s):"
              % (len(offenders), ENTRY_TABLE_REL.replace(os.sep, "/")))
        for rel, line, literal in offenders:
            print("  %s:%d: %s" % (rel, line, literal))
        print("Add the phrase to both tiers of the table and consume it through settings_label()/settings_text();")
        print("if it is a layout symbol rather than a phrase, mark it CJK-LITERAL: <category> and leave it here.")
        return 1

    print("PASS: %d source file(s) outside the entry table hold no quoted CJK phrase" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
