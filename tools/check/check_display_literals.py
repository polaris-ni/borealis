#!/usr/bin/env python3
"""
check_display_literals.py - keep the on-screen vocabulary the single source of on-screen text.

Why this gate exists
--------------------
Borealis renders its UI from one vocabulary: `assets/i18n/messages_zh.json` and
`assets/i18n/messages_en.json`, compiled at build time by `gen_i18n` into the `AUM1` binary that
`src/ui/settings_i18n.cpp` loads whole (Aurora i18n v2, `SPEC.FEAT.PREF.05` aftermath). A phrase
that lives outside that vocabulary is invisible to the other tier: a `LocalizedString` built by
`tr()` carries an empty `text`, so a missing key renders blank - never a fallback, never the key.

Before v2 the table was a C++ file and this gate whitelisted it. The table is now JSON, so the
whitelist is gone: **no** C++ source may hold a quoted CJK phrase, and the two ARB files are the
only place on-screen text is allowed to live.

Rules
-----
- Every string or character literal containing a CJK ideograph (U+4E00..U+9FFF), in `src/**`
  and `include/**`, is a hard error. Layout symbols (an arrow, a U+00B7 separator, a glyph) are
  deliberately NOT in scope: they are marked `CJK-LITERAL: <category>` where they occur and are
  not translatable phrases.
- Only the literals are scanned: comments are stripped first, because comments are allowed to
  be Chinese (AGENTS.md 4.3 rule 14).
- The two ARB files must exist, must be non-empty, and must cover exactly the same key set
  (metadata keys `@<key>.placeholders` excepted). A phrase added to one tier only is the same
  blank-on-screen defect the C++ scan catches, and it is invisible to the scan above.
- Scanning 0 files, or failing to locate the entry vocabulary, is a hard error - a check that
  inspects nothing must fail, never pass vacuously.

Self-proof of non-vacuity (AGENTS.md 4.2 rule 22)
-------------------------------------------------
Measured on this repo, in this order, each mutation reverted afterwards:
- inserting `AURORA_LOG_INFO("x", "自检短语");` in `src/main.cpp` -> 1 offender reported;
- deleting a key from `messages_en.json` -> ARB stage reports it;
- emptying `src/` from the scan list -> "no source file scanned" fails.

Usage
-----
  python3 tools/check/check_display_literals.py [--root <borealis_root>]
"""
import argparse
import json
import os
import re
import sys

VOCABULARY_REL = ("assets", "i18n")
VOCABULARY_FILES = ("messages_zh.json", "messages_en.json")
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
    """Yield every scannable source path under the root."""
    for top in SCANNED_DIRS:
        base = os.path.join(root, top)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames.sort()
            for name in sorted(filenames):
                if name.endswith(SCANNED_SUFFIXES):
                    yield os.path.join(dirpath, name)


def vocabulary_keys(root):
    """Return (key_sets, problems): the ARB key sets per tier, one entry per file.

    Metadata keys (`@<key>.placeholders`) are excluded from the cross-tier comparison: they carry
    argument names, not text, and only the base file needs them.
    """
    key_sets = {}
    problems = []
    for name in VOCABULARY_FILES:
        path = os.path.join(root, *VOCABULARY_REL, name)
        if not os.path.isfile(path):
            problems.append("the vocabulary file is missing: %s" % os.path.relpath(path, root))
            continue
        try:
            with open(path, "r", encoding="utf-8") as handle:
                document = json.load(handle)
        except (OSError, ValueError) as error:
            problems.append("the vocabulary file %s does not parse: %s" % (name, error))
            continue
        messages = document.get("messages", {})
        keys = {key for key, value in messages.items()
                if not key.startswith("@") and isinstance(value, str) and value.strip()}
        if not keys:
            problems.append("the vocabulary file %s holds no non-empty message" % name)
        key_sets[name] = keys
    return key_sets, problems


def check_vocabulary(root):
    """Cross-tier coverage: a key registered in one ARB must be registered in both."""
    key_sets, problems = vocabulary_keys(root)
    if len(key_sets) != len(VOCABULARY_FILES):
        return problems
    reference_name, reference = next(iter(sorted(key_sets.items())))
    for name, keys in sorted(key_sets.items()):
        if name == reference_name:
            continue
        for key in sorted(reference - keys):
            problems.append("%s lacks the key %s that %s registers" % (name, key, reference_name))
        for key in sorted(keys - reference):
            problems.append("%s registers the key %s that %s never heard of" % (name, key, reference_name))
    return problems


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=None, help="repository root (default: derived from this script)")
    args = parser.parse_args(argv)

    root = args.root or repo_root_of(__file__)
    if root is None:
        print("FAIL: cannot locate the repository root (needs CMakeLists.txt and AGENTS.md together)")
        return 1

    problems = check_vocabulary(root)

    files = list(iter_scanned_files(root))
    if not files:
        problems.append("no source file scanned under %s - a check that inspects nothing must not pass"
                        % ", ".join(os.path.join(root, top) for top in SCANNED_DIRS))

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

    if offenders or problems:
        if problems:
            print("FAIL: the on-screen vocabulary (%s) is not whole:" % "/".join(
                os.path.join(*VOCABULARY_REL, name) for name in VOCABULARY_FILES))
            for problem in problems:
                print("  %s" % problem)
        if offenders:
            print("FAIL: %d on-screen CJK literal(s) live outside the vocabulary:" % len(offenders))
            for rel, line, literal in offenders:
                print("  %s:%d: %s" % (rel, line, literal))
            print("Add the phrase to both ARB tiers and consume it through settings_text()/settings_label();")
            print("if it is a layout symbol rather than a phrase, mark it CJK-LITERAL: <category> and leave it here.")
        return 1

    print("PASS: %d source file(s) hold no quoted CJK phrase, and the two ARB tiers cover the same %d key(s)"
          % (len(files), len(next(iter(vocabulary_keys(root)[0].values())))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
