"""Wrap every Chinese wide-string literal in src/*.cpp with LS(...).

Why a script and not 245 hand edits: the literals are frequently split across
lines and implicitly concatenated (L"a " L"b"), so the real translation key is
only visible after merging. This merges, then wraps, then reports the key set.
"""
import io
import os
import re
import sys
import glob

CJK = re.compile(r'[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]')
WIDE = re.compile(r'L"((?:[^"\\]|\\.)*)"')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def code_mask(text):
    """True for every character index that is real code (not comment/string)."""
    mask = bytearray(b'\x01' * len(text))
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                mask[k] = 0
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                mask[k] = 0
            i = j
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            i += 1
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == '\\' else 1
            i += 1
        else:
            i += 1
    return mask


def scan(text):
    """-> list of (start, end, merged_body) for runs of wide literals."""
    mask = code_mask(text)
    runs = []
    i = 0
    n = len(text)
    while i < n:
        m = WIDE.search(text, i)
        if not m:
            break
        if not mask[m.start()]:
            i = m.end()
            continue
        # already wrapped?
        before = text[:m.start()].rstrip()
        if before.endswith('LS(') or before.endswith('LF('):
            i = m.end()
            continue
        start, end, body = m.start(), m.end(), m.group(1)
        j = m.end()
        while True:
            gap = j
            while gap < n and text[gap] in ' \t\r\n':
                gap += 1
            m2 = WIDE.match(text, gap)
            if not m2 or not mask[m2.start()]:
                break
            body += m2.group(1)
            end = m2.end()
            j = m2.end()
        runs.append((start, end, body))
        i = end
    return runs


def process(path, dry):
    text = io.open(path, encoding='utf-8').read()
    runs = scan(text)
    keys = []
    out = []
    last = 0
    hits = 0
    for start, end, body in runs:
        if not CJK.search(body):
            continue
        hits += 1
        keys.append(body)
        out.append(text[last:start])
        out.append('LS(L"%s")' % body)
        last = end
    out.append(text[last:])
    new = ''.join(out)
    if not dry and hits:
        io.open(path, 'w', encoding='utf-8', newline='').write(new)
    return hits, keys


def table_keys():
    """Every key the translation table in src/lang.cpp defines."""
    t = io.open('src/lang.cpp', encoding='utf-8').read()
    body = t.split('enTable()')[1]
    return set(re.findall(r'\{ L"((?:[^"\\]|\\.)*)"\s*,\s*L"', body))


def main():
    dry = '--dry' in sys.argv
    allkeys = []
    files = [f for f in sorted(glob.glob('src/*.cpp'))
             if os.path.basename(f) != 'lang.cpp']
    for f in files:
        hits, keys = process(f, dry)
        allkeys += keys
        print('%-20s wrapped %d' % (f, hits))
    uniq = []
    for k in allkeys:
        if k not in uniq:
            uniq.append(k)
    print()
    print('unique merged keys =', len(uniq))

    have = table_keys()
    missing = [k for k in uniq if k not in have]
    extra = [k for k in have if k not in uniq]
    if missing:
        print('MISSING from the table (%d):' % len(missing))
        for k in missing:
            print('   | %s' % k)
    if extra:
        print('in the table but never used (%d):' % len(extra))
        for k in extra:
            print('   | %s' % k)
    if not missing and not extra:
        print('table matches the source exactly')

    if '--list' in sys.argv:
        for i, k in enumerate(uniq, 1):
            print('%3d | %s' % (i, k))
    if dry:
        print('(dry run, nothing written)')


main()
