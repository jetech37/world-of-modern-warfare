"""Dump a WDC5 .db2 (as extracted by casc-read) to TSV: one row per record, every field as ints;
fields that look like string offsets are also resolved to text.

Usage: python -I db2dump.py <file.db2> [--strings col,col,...]

Read-only analysis helper. Encrypted sections (zero-filled by casc-read) are skipped.
"""
import struct
import sys


def main():
    path = sys.argv[1]
    str_cols = set()
    if "--strings" in sys.argv:
        str_cols = {int(x) for x in sys.argv[sys.argv.index("--strings") + 1].split(",")}
    b = open(path, "rb").read()
    assert b[:4] == b"WDC5", b[:4]
    o = 8 + 128
    (rc, fc, rs, sts, th, lh, min_id, max_id, loc, flags, id_index, tfc, bpo, lcc,
     fsis, cds, pds, sc) = struct.unpack_from("<4I2I3I2H7I", b, o)
    o += 4 * 4 + 8 + 12 + 4 + 7 * 4
    secs = []
    for _ in range(sc):
        secs.append(struct.unpack_from("<Q8I", b, o))
        o += 40
    o += fc * 4  # field_structure
    fsi = []
    for _ in range(fsis // 24):
        fsi.append(struct.unpack_from("<2H5I", b, o))
        o += 24
    pallet_start = o
    o += pds
    common_start = o
    o += cds
    # per-field pallet / common slices
    pal, com = {}, {}
    po, co = pallet_start, common_start
    for i, (_, _, add, comp, v1, v2, v3) in enumerate(fsi):
        if comp in (3, 4):
            pal[i] = b[po:po + add]
            po += add
        elif comp == 2:
            d = {}
            for j in range(0, add, 8):
                k, v = struct.unpack_from("<II", b, co + j)
                d[k] = v
            com[i] = (d, v1)
            co += add
    if flags & 1:
        sys.exit("offset-map (variable-size) DB2s are not supported")
    total_rec = sum(s[2] for s in secs) * rs
    rows = []
    rec_base = 0
    str_base = 0
    strtabs = b""
    for (tact, off, nrec, nstr, _, idsz, relsz, _, copysz) in secs:
        recs = b[off:off + nrec * rs]
        st = b[off + nrec * rs: off + nrec * rs + nstr]
        strtabs += st
        p = off + nrec * rs + nstr
        ids = list(struct.unpack_from("<%dI" % (idsz // 4), b, p)) if idsz else None
        encrypted = tact != 0 and recs.count(0) == len(recs)
        for r in range(nrec):
            if encrypted:
                continue
            rec = recs[r * rs:(r + 1) * rs]
            bits = int.from_bytes(rec, "little")
            vals = []
            for i, (fob, fsb, add, comp, v1, v2, v3) in enumerate(fsi):
                if comp == 0:
                    n = fsb // 32 if fsb >= 32 else 1
                    w = fsb // n
                    vs = [(bits >> (fob + k * w)) & ((1 << w) - 1) for k in range(n)]
                    vals.append(vs[0] if n == 1 else vs)
                elif comp in (1, 5):
                    v = (bits >> fob) & ((1 << fsb) - 1)
                    if comp == 5 and v >> (fsb - 1):
                        v -= 1 << fsb
                    vals.append(v)
                elif comp in (3, 4):
                    ix = (bits >> fob) & ((1 << fsb) - 1)
                    n = v3 if comp == 4 else 1
                    vs = list(struct.unpack_from("<%dI" % n, pal[i], ix * 4 * n))
                    vals.append(vs[0] if n == 1 else vs)
                elif comp == 2:
                    vals.append(None)  # filled after the ID is known
                else:
                    vals.append("?")
            rid = ids[r] if ids else vals[id_index]
            for i in com:
                vals[i] = com[i][0].get(rid, com[i][1])
            strs = {}
            for c in str_cols:
                v = vals[c]
                fo = fsi[c][0] // 8
                pos = rec_base + r * rs + fo + v - total_rec
                if 0 <= pos < 10**9:
                    pos -= 0
                strs[c] = (pos, v)
            rows.append((rid, vals, strs, rec_base + r * rs))
        rec_base += nrec * rs
        str_base += nstr
    for rid, vals, strs, _ in rows:
        out = [str(rid)]
        for i, v in enumerate(vals):
            if i in strs:
                pos, raw = strs[i]
                if 0 <= pos < len(strtabs) and raw:
                    s = strtabs[pos:strtabs.index(b"\0", pos)].decode("utf-8", "replace")
                    out.append(repr(s[:60]))
                    continue
                out.append("''")
            else:
                out.append(str(v))
        print("\t".join(out))


main()
