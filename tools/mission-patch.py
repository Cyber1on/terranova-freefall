"""mission-patch.py OUT N=ORIGINAL,CHANGED ... -- writes what a fan mission's author changed in
the game's resource files, and nothing else, as the patch file an extra mission is built from
(port/mods/extra.c applies it, in memory, to the player's own files).

Each pair is a resource file as the player has it and the author's file made from it; N is the
number port/mods/extra.c knows the file by.  For every resource that differs (same number, same
size: the mission editors of the time changed resources in place), the patch holds the bytes of
the two XORed with each other (zeros wherever the author changed nothing) packed with zlib, and
the CRC-32 of the original and of the result: without the player's own file the patch gives
neither of them.

The file: "FFMP", a count (2 bytes), then for each resource: file N (1 byte), resource number (2),
size (4), CRC-32 of the original (4), CRC-32 of the result (4), packed size (4), the packed bytes.
Numbers are little-endian.

    python tools/mission-patch.py port/res/stone-henge.ffmp \
        0=TNDEMO1/DATA/MISS1D.RES,MISS1SH.RES 1=MAPS/MAP2.RES,MAP5SH.RES \
        2=TNDEMO1/DATA/DEMOMIS1.RES,DEMOMISH.RES 3=TNDEMO1/DATA/DEMOPTRM.RES,DEMOPTSH.RES \
        4=TNDEMO1/DATA/DEMOGAM1.RES,DEMOGASH.RES
"""
import struct
import sys
import zlib


def lzw(src, size):
    """The resource library's LZW (14-bit codes, 0x3FFE restarts the table, 0x3FFF ends)."""
    out = bytearray()
    prefix = [0] * 0x4000
    append = [0] * 0x4000
    buf = cnt = p = 0

    def code():
        nonlocal buf, cnt, p
        while cnt <= 24:
            buf |= (src[p] if p < len(src) else 0) << (24 - cnt)
            p += 1
            cnt += 8
        v = (buf >> 18) & 0x3FFF
        buf = (buf << 14) & 0xFFFFFFFF
        cnt -= 14
        return v

    nxt = 256
    old = ch = code()
    if old > 255:
        return bytes(size)
    out.append(old)
    while len(out) < size:
        c = code()
        if c == 0x3FFF or p > len(src) + 4:
            break
        if c == 0x3FFE:
            nxt = 256
            old = ch = code()
            if old > 255:
                break
            out.append(old)
            continue
        if c > nxt:
            break
        st = []
        k = c
        if c >= nxt:
            st.append(ch)
            k = old
        while k > 255:
            st.append(append[k])
            k = prefix[k]
        st.append(k)
        ch = k
        out.extend(reversed(st))
        if nxt <= 0x3FFD:
            prefix[nxt] = old
            append[nxt] = ch
            nxt += 1
        old = c
    return bytes(out[:size]).ljust(size, b"\0")


def load(path):
    """A resource file's resources, unpacked: {number: bytes}."""
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 0x7C)[0]
    count, pos = struct.unpack_from("<HI", d, off)
    res = {}
    for i in range(count):
        rid, a, b = struct.unpack_from("<HII", d, off + 6 + i * 10)
        size, flags, packed = a & 0xFFFFFF, a >> 24, b & 0xFFFFFF
        raw = d[pos:pos + packed]
        if flags & 3 == 3:              # compound and packed: the item table is not packed
            t = 2 + 4 * (struct.unpack_from("<H", raw, 0)[0] + 1)
            res[rid] = raw[:t] + lzw(raw[t:], size - t)
        elif flags & 1:
            res[rid] = lzw(raw, size)
        else:
            res[rid] = raw[:size]
        pos += (packed + 3) & ~3
    return res


def main():
    out = sys.argv[1]
    recs = []
    for arg in sys.argv[2:]:
        n, paths = arg.split("=", 1)
        orig_path, new_path = paths.split(",", 1)
        orig, new = load(orig_path), load(new_path)
        for rid in sorted(new):
            if rid not in orig:
                sys.exit("%s: resource %d is not in %s" % (new_path, rid, orig_path))
            a, b = orig[rid], new[rid]
            if a == b:
                continue
            if len(a) != len(b):
                sys.exit("%s: resource %d has another size (%d, %d)" % (new_path, rid, len(a), len(b)))
            z = zlib.compress(bytes(x ^ y for x, y in zip(a, b)), 9)
            recs.append(struct.pack("<BHIIII", int(n), rid, len(a), zlib.crc32(a), zlib.crc32(b), len(z)) + z)
            print("file %s resource %d: %d bytes, %d changed, %d packed"
                  % (n, rid, len(a), sum(x != y for x, y in zip(a, b)), len(z)))
    with open(out, "wb") as f:
        f.write(b"FFMP" + struct.pack("<H", len(recs)) + b"".join(recs))
    print("%s: %d resources" % (out, len(recs)))


main()
