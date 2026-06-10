#!/usr/bin/env python3
# Find dynamic fields in the 1724-byte gaze packet: offsets whose 4-byte value
# changes nearly every frame are gaze/eye data. Classify their encoding.
import struct, math

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]
L = 1724
n = len(P)
print("packets:", n)

# change frequency per 4-byte LE word at each offset (step 1 to catch any alignment)
rows=[]
for off in range(0, L-4):
    changes=0
    prev=None
    for d in P:
        v=d[off:off+4]
        if prev is not None and v!=prev: changes+=1
        prev=v
    frac=changes/(n-1)
    if frac>0.5:  # changes more than half the frames
        # interpret as LE float and BE float and int
        fvals=[struct.unpack_from("<f",d,off)[0] for d in P]
        finite=[x for x in fvals if math.isfinite(x)]
        inr=[x for x in finite if -3<=x<=3]
        rows.append((off,frac,len(inr)/n if n else 0,
                     min(inr) if inr else 0, max(inr) if inr else 0))

print(f"\noffsets changing >50% of frames: {len(rows)}")
print("off    chg%  LEfloat_in[-3,3]%   range")
for off,frac,inrf,lo,hi in rows:
    flag = "  <== float-like [0,1.x]" if (inrf>0.8 and -0.5<=lo and hi<=2.0 and hi-lo>0.1) else ""
    print(f"{off:4d} (0x{off:03x}) {frac*100:4.0f}%  {inrf*100:4.0f}%  {lo:+.3f}..{hi:+.3f}{flag}")
