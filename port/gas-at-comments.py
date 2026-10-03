#!/usr/bin/env python3
"""Runs the GNU assembler on game assembly written for 32-bit ARM.

The game's data and macros use '@' for comments, which only the 32-bit ARM
assembler understands. The AArch64 assembler reads '@' as junk. This wrapper
inlines every .include (so included macro files are covered too), cuts '@'
comments outside strings, keeps the macro counter '\\@', and pipes the result
to the real assembler. Line markers keep error messages pointing at the
original files.

Usage: gas-at-comments.py AS [as options] [input.s | -]
"""
import os
import re
import subprocess
import sys

INCLUDE_RE = re.compile(r'^\s*\.include\s+"([^"]+)"\s*(?:@.*)?$')
VALUE_OPTS = {"-o", "-I", "--defsym", "-MD"}


def strip_comment(line):
    in_string = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c == "\\" and i + 1 < len(line) and line[i + 1] == "@":
            i += 2
            continue
        elif c == "@":
            return line[:i].rstrip() + "\n"
        i += 1
    return line


def find_include(name, include_dirs):
    if os.path.exists(name):
        return name
    for d in include_dirs:
        path = os.path.join(d, name)
        if os.path.exists(path):
            return path
    return name


def expand(text, filename, include_dirs, out, depth=0):
    if depth > 32:
        sys.exit(f"{filename}: .include nested too deeply")
    out.append(f'# 1 "{filename}"\n')
    for lineno, line in enumerate(text.splitlines(keepends=True), 1):
        m = INCLUDE_RE.match(line)
        if m:
            path = find_include(m.group(1), include_dirs)
            try:
                with open(path, encoding="utf-8", errors="surrogateescape") as f:
                    sub = f.read()
            except OSError:
                out.append(line)
                continue
            expand(sub, path, include_dirs, out, depth + 1)
            out.append(f'# {lineno + 1} "{filename}"\n')
            continue
        out.append(strip_comment(line))


def main():
    argv = sys.argv[1:]
    assembler = argv[0]
    args = argv[1:]
    passthrough = []
    include_dirs = []
    source = None
    i = 0
    while i < len(args):
        a = args[i]
        if a in VALUE_OPTS and i + 1 < len(args):
            passthrough += [a, args[i + 1]]
            if a == "-I":
                include_dirs.append(args[i + 1])
            i += 2
            continue
        if a.startswith("-I") and len(a) > 2:
            include_dirs.append(a[2:])
        if a == "-" or not a.startswith("-"):
            source = a
        else:
            passthrough.append(a)
        i += 1

    if source is None or source == "-":
        text = sys.stdin.buffer.read().decode("utf-8", "surrogateescape")
        name = "<stdin>"
    else:
        with open(source, encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        name = source

    out = []
    expand(text, name, include_dirs, out)
    data = "".join(out).encode("utf-8", "surrogateescape")
    result = subprocess.run([assembler] + passthrough + ["-"], input=data)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
