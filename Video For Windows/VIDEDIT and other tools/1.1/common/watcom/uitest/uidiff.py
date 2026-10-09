#!/usr/bin/env python3
"""Compare the results of two uitest runs.

usage: uidiff.py A_TESTDIR B_TESTDIR

Each directory holds UI.LOG and the IMG directory of one run.  Log lines
are compared after masking what legitimately differs between runs:
  - the instance handle that WinExec returns ("run ... = N"),
  - the capture date in an AVI's IDIT chunk,
  - frame and chunk counts of captures (lines marked with "frames=" or a
    "movi" summary) when --capture-counts is given,
  - the four reserved DWORDs at the end of an AVI main header ("avih"),
    which the Video for Windows 1.1 AVI writer leaves uninitialised.
A "file" line (size and CRC) whose CRCs differ still matches if both runs
left the file in their directory and the files differ only in those
reserved DWORDs.  Images are compared byte for byte.  Exit status 0 if
everything matches.
"""
import os, re, sys


def norm(line, counts):
    line = line.rstrip('\r\n')
    line = re.sub(r'^(run .*) = \d+$', r'\1 = <hinst>', line)
    line = re.sub(r'^(\s*IDIT \d+ )[0-9A-F]+$', r'\1<date>', line)
    line = re.sub(r'^(\s*avih 56 [0-9A-F]{80})[0-9A-F]{32}$', r'\1<reserved>', line)
    if counts:
        line = re.sub(r'(LIST movi )\d+: \d+ chunks, \d+ bytes', r'\1<n>', line)
    return line


def same_file_masked(a, b, line):
    """True if the file a "file" log line names is the same in both runs
    apart from the reserved avih DWORDs."""
    m = re.match(r'^file (.*): \d+ bytes', line)
    if not m:
        return False
    name = m.group(1).replace('\\', '/').split('/')[-1]
    pa, pb = os.path.join(a, name), os.path.join(b, name)
    if not (os.path.exists(pa) and os.path.exists(pb)):
        return False
    da, db = bytearray(open(pa, 'rb').read()), bytearray(open(pb, 'rb').read())
    if len(da) != len(db):
        return False
    i = da.find(b'avih')
    if i >= 0 and db[i:i + 4] == b'avih':
        da[i + 8 + 40:i + 8 + 56] = db[i + 8 + 40:i + 8 + 56] = bytes(16)
    return da == db


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    counts = '--capture-counts' in sys.argv
    a, b = args
    la = [norm(x, counts) for x in open(os.path.join(a, 'UI.LOG'), encoding='latin-1')]
    lb = [norm(x, counts) for x in open(os.path.join(b, 'UI.LOG'), encoding='latin-1')]
    bad = 0
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else '<missing>'
        y = lb[i] if i < len(lb) else '<missing>'
        if x != y and x.startswith('file ') and y.startswith('file ') and \
                x.split(' crc=')[0] == y.split(' crc=')[0] and same_file_masked(a, b, x):
            continue
        if x != y:
            bad += 1
            if bad <= 30:
                print('log line %d:\n  A: %s\n  B: %s' % (i + 1, x, y))
    print('log: %d lines, %d differ' % (max(len(la), len(lb)), bad))

    ia, ib = os.path.join(a, 'IMG'), os.path.join(b, 'IMG')
    names = sorted(set(os.listdir(ia)) | set(os.listdir(ib)))
    nbad = 0
    for n in names:
        pa, pb = os.path.join(ia, n), os.path.join(ib, n)
        if not os.path.exists(pa) or not os.path.exists(pb):
            print('image %s: only in one run' % n)
            nbad += 1
            continue
        da, db = open(pa, 'rb').read(), open(pb, 'rb').read()
        if da != db:
            ndiff = sum(1 for x, y in zip(da, db) if x != y) + abs(len(da) - len(db))
            print('image %s: %d bytes differ' % (n, ndiff))
            nbad += 1
    print('images: %d, %d differ' % (len(names), nbad))
    return 1 if bad or nbad else 0


if __name__ == '__main__':
    sys.exit(main())
