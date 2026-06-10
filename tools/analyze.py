#!/usr/bin/env python3
# Locate gaze float fields inside the 1788-byte Tobii stream packets by
# scanning every offset for a float32 pair that plausibly tracks gaze.
import struct, sys

pkts = []
for line in open("/tmp/gaze_in_packets.txt"):
    parts = line.split()
    if len(parts) < 2: continue
    t = float(parts[0]); data = bytes.fromhex(parts[1])
    pkts.append((t, data))
print("packets:", len(pkts), "len set:", sorted({len(d) for _,d in pkts})[:5])

# For each byte offset, read float32 (LE and BE) across all packets; find offsets
# where values mostly lie in a gaze-ish range and VARY (so not constants).
N = min(len(d) for _,d in pkts)
def scan(endian):
    fmt = ("<" if endian=="LE" else ">") + "f"
    cands = []
    for off in range(0, N-4):
        vals = []
        ok = True
        for _, d in pkts[:400]:
            try: v = struct.unpack_from(fmt, d, off)[0]
            except: ok=False; break
            vals.append(v)
        if not ok: continue
        import math
        finite = [v for v in vals if math.isfinite(v)]
        if len(finite) < len(vals)*0.9: continue
        inrange = [v for v in finite if -0.3 <= v <= 1.5]
        if len(inrange) < len(finite)*0.7: continue
        lo, hi = min(inrange), max(inrange)
        if hi - lo < 0.15: continue           # must vary (gaze moves)
        cands.append((off, lo, hi, sum(inrange)/len(inrange)))
    return cands

for endian in ("LE","BE"):
    c = scan(endian)
    print(f"\n=== {endian} candidate float offsets (range>0.15, mostly in [-0.3,1.5]):")
    for off, lo, hi, mean in c[:40]:
        print(f"  off {off:4d} (0x{off:03x}): {lo:+.3f}..{hi:+.3f} mean {mean:+.3f}")
