#!/usr/bin/env python3
# TEMPORARY (plan 0023): job logs need a GitHub sign-in, annotations do not. On a failed job this
# prints the output of the failed tests and the failed build edges as ::notice annotations.
#   ci-failure-notes.py <build dir> [<log file>...]
import pathlib
import re
import sys

MAX_NOTES = 10
MAX_LINES = 80


def note(title, lines):
    text = "\n".join(lines[-MAX_LINES:])
    text = text.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    title = title.replace(",", ";").replace(":", " ")
    print(f"::notice title={title}::{text}")


def ctest_notes(build):
    tmp = build / "Testing" / "Temporary"
    failed = tmp / "LastTestsFailed.log"
    last = tmp / "LastTest.log"
    if not failed.exists() or not last.exists():
        return 0
    names = [l.split(":", 1)[1].strip() for l in failed.read_text(errors="replace").splitlines() if ":" in l]
    log = last.read_text(errors="replace")
    count = 0
    for name in names[:MAX_NOTES]:
        m = re.search(r"\d+/\d+ Testing: " + re.escape(name) + r"\n(.*?)(?=\n\d+/\d+ Testing: |\Z)", log, re.S)
        if m:
            note("test " + name, m.group(1).splitlines())
            count += 1
    return count


def build_notes(path, budget, tail):
    lines = path.read_text(errors="replace").splitlines()
    blocks, current = [], None
    for line in lines:
        if line.startswith("FAILED:") or line.startswith("CMake Error"):
            current = [line]
            blocks.append(current)
        elif current is not None:
            if re.match(r"^\[\d+/\d+\]", line) or line.startswith("ninja: build stopped"):
                current = None
            else:
                current.append(line)
    for block in blocks[:budget]:
        note(path.name + " " + block[0][:80], block[:MAX_LINES])
    if not blocks:
        if tail:
            note(path.name + " (tail)", lines)
            return 1
        return 0
    return min(len(blocks), budget)


def main():
    build = pathlib.Path(sys.argv[1])
    tests = ctest_notes(build)
    budget = MAX_NOTES - tests
    logs = [pathlib.Path(arg) for arg in sys.argv[2:] if pathlib.Path(arg).exists()]
    for path in logs:
        if budget > 0:
            # The tail only of the newest log (the failed step) and only when no test failed.
            budget -= build_notes(path, budget, tests == 0 and path == logs[-1])


main()
