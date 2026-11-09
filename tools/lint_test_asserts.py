#!/usr/bin/env python3
"""Fail if `assert(` appears inside a define_test body (or a test section).

Tests use `require(...)` (core/test.hpp), which is evaluated in every build:
the Release build runs the suite too, and NDEBUG removes an assert's whole
expression, side effects included.  See CLAUDE.md "Assertions".

The test section of a file is everything after its `#include "test.hpp"`
when that include is not in the file's leading include block (svg.cpp's
layout: production code, then `#include "test.hpp"`, then helpers and
define_test bodies), every define_test body wherever it is, and the whole of
a test-support header (*_test.hpp, test_support.hpp).  Not covered: a test
helper function outside a body in a file that includes test.hpp at the top;
keep such helpers inside the body as lambdas, or use require( there too.

  lint_test_asserts.py ROOT            check; exit 1 listing offenders
  lint_test_asserts.py ROOT --fix      rewrite assert( / CHECK( to require(
  lint_test_asserts.py ROOT --stamp F  check, and touch F on success
"""
import pathlib, re, sys

EXTS = {'.cpp', '.hpp', '.mm', '.h'}
TOKEN = re.compile(r'(?<![\w])(assert|CHECK)\s*\(')


def code_mask(s):
    """True at each index that is code (not a comment, string or char)."""
    mask = bytearray(len(s))
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if s.startswith('//', i):
            j = s.find('\n', i)
            i = n if j < 0 else j
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == 'R' and s.startswith('R"', i) and (i == 0 or not (s[i-1].isalnum() or s[i-1] == '_')):
            k = s.find('(', i + 2)
            delim = s[i+2:k]
            j = s.find(')' + delim + '"', k)
            i = n if j < 0 else j + len(delim) + 2
        elif c == '"' or (c == "'" and not (i and (s[i-1].isalnum()))):  # ' after a digit is a separator
            q = c
            j = i + 1
            while j < n and s[j] != q:
                j += 2 if s[j] == '\\' else 1
            i = j + 1
        else:
            mask[i] = 1
            i += 1
    return mask


def test_ranges(s, mask, name=""):
    if name.endswith("_test.hpp") or name == "test_support.hpp":
        return [(0, len(s))]
    ranges = []
    inc = re.search(r'#include\s+"(?:[\w./]*/)?test\.hpp"', s)
    if inc:
        # in the leading include block? (only includes/comments/blank/#if before it)
        head = s[:inc.start()]
        lead = all(not l.strip() or l.lstrip().startswith(('#', '//', '/*', '*'))
                   for l in head.split('\n'))
        if not lead:
            ranges.append((inc.end(), len(s)))
    for m in re.finditer(r'\bdefine_test\s*\(', s):
        if not mask[m.start()]:
            continue
        i = m.end()
        while i < len(s) and not (s[i] == '{' and mask[i]):
            i += 1
        d, j = 0, i
        while j < len(s):
            if mask[j]:
                if s[j] == '{': d += 1
                elif s[j] == '}':
                    d -= 1
                    if d == 0: break
            j += 1
        ranges.append((m.start(), j))
    return ranges


def scan(path, fix):
    s = path.read_text(errors='surrogateescape')
    if 'define_test' not in s and 'test.hpp' not in s and not (path.name.endswith('_test.hpp') or path.name == 'test_support.hpp'):
        return []
    mask = code_mask(s)
    ranges = test_ranges(s, mask, path.name)
    hits = [m for m in TOKEN.finditer(s)
            if mask[m.start()] and any(a <= m.start() < b for a, b in ranges)]
    if fix and hits:
        out, last = [], 0
        for m in hits:
            out.append(s[last:m.start()]); out.append('require('); last = m.end()
        out.append(s[last:])
        path.write_text(''.join(out), errors='surrogateescape')
    return [(s.count('\n', 0, m.start()) + 1, m.group(1)) for m in hits
            if fix or m.group(1) == 'assert']


def main():
    root = pathlib.Path(sys.argv[1])
    fix = '--fix' in sys.argv
    stamp = sys.argv[sys.argv.index('--stamp') + 1] if '--stamp' in sys.argv else None
    bad = 0
    for f in sorted(root.rglob('*')):
        if f.suffix not in EXTS or not f.is_file():
            continue
        for line, what in scan(f, fix):
            bad += 1
            if not fix:
                print(f'{f}:{line}: error: {what}( in a test; use require( (core/test.hpp)')
    if fix:
        print(f'rewrote {bad} checks to require(')
        return 0
    if bad:
        print(f'{bad} assert( in tests (tools/lint_test_asserts.py)', file=sys.stderr)
        return 1
    if stamp:
        pathlib.Path(stamp).touch()
    return 0


if __name__ == '__main__':
    sys.exit(main())
