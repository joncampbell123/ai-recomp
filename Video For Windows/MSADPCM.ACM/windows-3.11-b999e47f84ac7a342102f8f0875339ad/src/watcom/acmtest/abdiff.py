#!/usr/bin/env python3
"""Compare two acmtest output files record by record.

usage: abdiff.py ORIG.OUT NEW.OUT

Each record is a 16-byte NUL-padded tag, a WORD sequence number, a WORD of
padding, a DWORD length, then the data.  Prints the first differences and a
count of differing records per tag group.  Decoded data from fuzzed
streams with predictor 7 (tags "fz7*.d"), where the original decoder
reads uninitialised stack, is counted separately; the return values and
lengths of those conversions must still match.

ACMDRIVERDETAILS records ("dd.d") are compared up to each string's
terminator only: the driver builds the structure on its stack and copies
it out whole, so the bytes after the end of each string are leftovers.
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


# ACMDRIVERDETAILS string fields: offset, size
DD_STRINGS = ((0x26, 32), (0x46, 128), (0xC6, 80), (0x116, 128), (0x196, 512))


def mask_details(d):
    """blank the bytes after each string's terminator, within cbStruct"""
    d = bytearray(d)
    cb = min(struct.unpack_from('<I', d)[0], 0x396)
    for off, size in DD_STRINGS:
        end = min(off + size, cb)
        if off >= end:
            continue
        nul = d.find(0, off, end)
        if nul >= 0:
            d[nul + 1:end] = bytes(end - nul - 1)
    return bytes(d)


def group(tag):
    """'e0111r.c160.d' -> 'e.c.d' style key for the summary"""
    head, _, rest = tag.partition('.')
    key = head.rstrip('0123456789xyzrn')
    if rest:
        key += '.' + '.'.join(p.rstrip('0123456789') for p in rest.split('.'))
    return key


def main():
    a = list(records(sys.argv[1]))
    b = list(records(sys.argv[2]))
    print('records:', len(a), len(b))
    ctx = ''
    counts = collections.Counter()
    expected = collections.Counter()
    shown = 0
    for i, ((ta, _, da), (tb, _, db)) in enumerate(zip(a, b)):
        if ta in ('enc', 'fuzz'):
            ctx = '%s=%d' % (ta, struct.unpack('<l', da)[0])
        if ta != tb:
            print('tag mismatch at record', i, ta, tb, ctx)
            break
        if ta == 'dd.d':
            da, db = mask_details(da), mask_details(db)
        if da == db:
            continue
        if ta.startswith('fz7') and ta.endswith('.d'):
            expected[group(ta)] += 1
            continue
        counts[group(ta)] += 1
        if shown < 30:
            shown += 1
            if len(da) == 4 and len(db) == 4:
                print('%6d %-15s %-16s %d / %d' % (i, ta, ctx, struct.unpack('<l', da)[0],
                                                   struct.unpack('<l', db)[0]))
            else:
                diff = [j for j, (x, y) in enumerate(zip(da, db)) if x != y]
                print('%6d %-15s %-16s %d of %d bytes differ, first at %d' %
                      (i, ta, ctx, len(diff), len(da), diff[0] if diff else -1))
    print('differing records by tag:', dict(counts) or 'none')
    print('predictor-7 records that differ (expected):', dict(expected) or 'none')
    return 1 if counts else 0


if __name__ == '__main__':
    sys.exit(main())
