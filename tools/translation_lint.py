#!/usr/bin/env python3
"""Translation lint (plan 0028 D15, ADR-0027): checks the <translation> texts of Qt Linguist .ts files.

Usage: python3 -I tools/translation_lint.py FILE.ts [FILE.ts ...]

A translation (each <numerusform> of a plural message) is reported when it has
  - more lone '&' than its source (a stray mnemonic; '&&' and entities such as '&amp;' are fine),
  - more '<' than its source (markup that the source does not have),
  - a combining mark U+0300..U+036F (Czech letters must be precomposed: "š", not "s" + U+030C),
  - an invisible character U+200B..U+200F, U+2060, U+FEFF or U+00AD,
  - another set of placeholders (%1, %n, %Ln, %L1, ...) than its source.
Empty and unfinished-empty translations are skipped. Exit status 1 when anything was reported.
Only the standard library is used; the files are parsed as XML (no entity expansion beyond XML's).
"""

import re
import sys
import xml.etree.ElementTree as ET

PLACEHOLDER = re.compile(r"%L?(?:\d{1,2}|n)")
INVISIBLE = {"\u200b", "\u200c", "\u200d", "\u200e", "\u200f", "\u2060", "\ufeff", "\u00ad"}


def lone_ampersands(text: str) -> int:
    count = 0
    i = 0
    while i < len(text):
        if text[i] == "&":
            if i + 1 < len(text) and text[i + 1] == "&":
                i += 2
                continue
            entity = re.match(r"&(#\d+|#x[0-9a-fA-F]+|[a-zA-Z]+);", text[i:])
            if entity:
                i += len(entity.group(0))
                continue
            count += 1
        i += 1
    return count


def problems(source: str, translation: str) -> list:
    found = []
    if lone_ampersands(translation) > lone_ampersands(source):
        found.append("a lone '&' the source does not have")
    if translation.count("<") > source.count("<"):
        found.append("a '<' the source does not have")
    marks = [c for c in translation if 0x300 <= ord(c) <= 0x36F]
    if marks:
        found.append("combining mark(s) " + ", ".join(f"U+{ord(c):04X}" for c in marks))
    hidden = [c for c in translation if c in INVISIBLE]
    if hidden:
        found.append("invisible character(s) " + ", ".join(f"U+{ord(c):04X}" for c in hidden))
    if set(PLACEHOLDER.findall(translation)) != set(PLACEHOLDER.findall(source)):
        found.append(
            f"placeholders {sorted(set(PLACEHOLDER.findall(translation)))} instead of "
            f"{sorted(set(PLACEHOLDER.findall(source)))}")
    return found


def lint(path: str) -> int:
    reported = 0
    checked = 0
    root = ET.parse(path).getroot()
    for context in root.iter("context"):
        name = context.findtext("name", default="")
        for message in context.iter("message"):
            source = message.findtext("source", default="")
            translation = message.find("translation")
            if translation is None:
                continue
            forms = translation.findall("numerusform")
            texts = [form.text or "" for form in forms] if forms else [translation.text or ""]
            for text in texts:
                if not text:
                    continue
                checked += 1
                # Plural forms may use %n where the source uses %Ln (and the reverse).
                plural_source = source
                if forms:
                    plural_source = source.replace("%Ln", "%n")
                    text = text.replace("%Ln", "%n")
                for problem in problems(plural_source, text):
                    reported += 1
                    print(f"{path}: [{name}] {source!r}: {problem}: {text!r}")
    print(f"translation_lint: {path}: {checked} translations checked, {reported} problem(s)")
    return reported


def main(argv: list) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    total = sum(lint(path) for path in argv[1:])
    if total:
        print(f"translation_lint: {total} problem(s)")
        return 1
    print("translation_lint: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
