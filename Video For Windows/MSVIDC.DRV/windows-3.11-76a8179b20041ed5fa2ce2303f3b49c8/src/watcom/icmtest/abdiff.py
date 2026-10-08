#!/usr/bin/env python3
"""Compare two icmtest output files record by record.

usage: abdiff.py ORIG.OUT NEW.OUT

Each record is a 16-byte NUL-padded tag, a WORD sequence number, a WORD of
padding, a DWORD length, then the data.  Prints the first differences and a
count of differing records per tag.
"""
import collections, struct, sys


def records(path):
    data = open(path, 'rb').read()
    off = 0
    while off + 24 <= len(data):
        tag = data[off:off + 16].split(b'\0')[0].decode('latin-1')
        seq, length = struct.unpack_from('<H2xI', data, off + 16)
        yield tag, seq, data[off + 24:off + 24 + length]
        off += 24 + length


def main():
    a = list(records(sys.argv[1]))
    b = list(records(sys.argv[2]))
    print('records:', len(a), len(b))
    ctx = ''
    counts = collections.Counter()
    shown = 0
    for i, ((ta, _, da), (tb, _, db)) in enumerate(zip(a, b)):
        if ta.startswith('seq'):
            ctx = '%s q=%d' % (ta, struct.unpack('<l', da)[0])
        if ta != tb:
            print('tag mismatch at record', i, ta, tb, ctx)
            break
        if da == db:
            continue
        counts[ta] += 1
        if shown < 20:
            shown += 1
            if len(da) == 4 and len(db) == 4:
                print('%5d %-12s %-24s %d / %d' % (i, ta, ctx, struct.unpack('<l', da)[0],
                                                   struct.unpack('<l', db)[0]))
            else:
                n = sum(1 for x, y in zip(da, db) if x != y)
                print('%5d %-12s %-24s %d of %d bytes differ' % (i, ta, ctx, n, len(da)))
    print('differing records by tag:', dict(counts))


if __name__ == '__main__':
    main()
