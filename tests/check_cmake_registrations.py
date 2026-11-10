#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard split CMake test registrations against duplicates and shared-file appends."""

from collections import defaultdict
from pathlib import Path
import re
import sys
from tempfile import TemporaryDirectory

MARKER = "# New standalone test registrations belong in registrations/test_<name>.cmake,"
COMMAND = re.compile(
    r'^\s*(add_executable|add_library|add_test)\s*\(\s*(?:NAME\s+)?("[^"]+"|[^\s)]+)',
    re.IGNORECASE | re.MULTILINE,
)


def commands(text):
    for match in COMMAND.finditer(text):
        kind, name = match.groups()
        yield ("test" if kind.lower() == "add_test" else "target", name.strip('"'))


def check_tree(root):
    tests = root / "tests"
    parent = tests / "CMakeLists.txt"
    registration_dir = tests / "registrations"
    errors = []
    if not parent.is_file():
        return [f"Missing {parent}"]
    source = parent.read_text(encoding="utf-8")
    if MARKER not in source:
        errors.append("Missing standalone-registration convention in tests/CMakeLists.txt")
    else:
        tail = source.split(MARKER, 1)[1]
        if COMMAND.search(tail):
            errors.append("New targets/tests must live in registrations/test_<name>.cmake, not after the shared-file marker")

    if not (
        "file(GLOB test_registration_files CONFIGURE_DEPENDS" in source
        and "list(SORT test_registration_files)" in source
        and "include(" in source
    ):
        errors.append("Missing deterministic CONFIGURE_DEPENDS registration discovery")

    fragments = sorted(registration_dir.glob("*.cmake"))
    if not fragments:
        errors.append("No test-registration fragments found")
    seen = defaultdict(list)
    for path in [parent, *fragments]:
        text = path.read_text(encoding="utf-8")
        discovered = list(commands(text))
        for kind, name in discovered:
            seen[(kind, name)].append(path.relative_to(root).as_posix())
        if path == parent:
            continue
        if not re.fullmatch(r"test_[a-z0-9_]+\.cmake", path.name):
            errors.append(f"{path.name}: registration fragments must be test_<name>.cmake")
            continue
        expected = path.stem
        if discovered.count(("target", expected)) != 1:
            errors.append(f"{path.name}: expected exactly one add_executable({expected} ...) registration")
        if discovered.count(("test", expected)) != 1:
            errors.append(f"{path.name}: expected exactly one add_test(NAME {expected} ...) registration")

    for (kind, name), locations in sorted(seen.items()):
        if len(locations) > 1:
            errors.append(f"Duplicate {kind} {name}: {', '.join(locations)}")
    return errors


def self_test():
    with TemporaryDirectory() as directory:
        root = Path(directory)
        tests = root / "tests"
        fragments = tests / "registrations"
        fragments.mkdir(parents=True)
        parent = tests / "CMakeLists.txt"
        original = (
            MARKER + "\n"
            + 'file(GLOB test_registration_files CONFIGURE_DEPENDS "registrations/test_*.cmake")\n'
            + "list(SORT test_registration_files)\n"
            + "foreach(registration_file IN LISTS test_registration_files)\n"
            + '    include("registrations/test_example.cmake")\n'
            + "endforeach()\n"
        )
        parent.write_text(original, encoding="utf-8")
        fragment = fragments / "test_example.cmake"
        fragment.write_text(
            "add_executable(test_example test_example.cpp)\n"
            "add_test(NAME test_example COMMAND test_example)\n",
            encoding="utf-8",
        )
        assert not check_tree(root), check_tree(root)
        parent.write_text(
            "add_executable(test_example test_example.cpp)\n" + original,
            encoding="utf-8",
        )
        assert any("Duplicate target test_example" in e for e in check_tree(root))
        parent.write_text(
            original + "add_executable(test_accidental test_accidental.cpp)\n",
            encoding="utf-8",
        )
        assert any("must live in registrations" in e for e in check_tree(root))
        parent.write_text(original, encoding="utf-8")
        fragment.rename(fragments / "test_wrong.cmake")
        assert any("expected exactly one" in e for e in check_tree(root))
    print("CMake registration guard self-tests passed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) == 1:
        failures = check_tree(Path(__file__).resolve().parents[1])
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        if failures:
            sys.exit(1)
        print("CMake test registrations are unique and modular")
    else:
        print("usage: check_cmake_registrations.py [--self-test]", file=sys.stderr)
        sys.exit(2)
