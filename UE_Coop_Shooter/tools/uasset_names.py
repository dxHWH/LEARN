"""Dump FString-like strings from a .uasset (name map + inline strings).

UE stores FString as int32 len (incl. NUL; negative => UTF-16) followed by chars.
We scan the whole file for that pattern; this captures the name map
(function / variable / node / pin names) without a full package parser.
Usage: python uasset_names.py file.uasset [--all]
"""
import struct, sys, re

def scan(data):
    out = []
    i, n = 0, len(data)
    while i < n - 5:
        (ln,) = struct.unpack_from('<i', data, i)
        if 2 <= ln <= 512 and i + 4 + ln <= n and data[i + 4 + ln - 1] == 0:
            s = data[i + 4:i + 4 + ln - 1]
            if all(32 <= b < 127 for b in s):
                out.append((i, s.decode('ascii')))
                i += 4 + ln
                continue
        elif -512 <= ln <= -2:
            m = -ln
            end = i + 4 + m * 2
            if end <= n and data[end - 2:end] == b'\0\0':
                try:
                    s = data[i + 4:end - 2].decode('utf-16-le')
                    if s and all(ch.isprintable() for ch in s):
                        out.append((i, s))
                        i = end
                        continue
                except UnicodeDecodeError:
                    pass
        i += 1
    return out

if __name__ == '__main__':
    path = sys.argv[1]
    data = open(path, 'rb').read()
    seen = []
    for off, s in scan(data):
        if '--all' in sys.argv or s not in seen:
            seen.append(s)
    sys.stdout.reconfigure(encoding='utf-8')
    for s in seen:
        print(s)
