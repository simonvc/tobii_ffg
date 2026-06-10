#!/usr/bin/env python3
# Find the gaze (x,y) float pair offset by matching packet bytes against
# the actual coordinates Talon logged.
import struct, math
from collections import Counter

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]

# Logged valid gaze pairs (x,y)
logged = []
for l in open("captures/talon_gaze_valid.log"):
    p = l.split()
    if len(p)>=3 and (p[-1]=="R=True" or p[-2]=="L=True"):
        try:
            x=float(p[1]); y=float(p[2])
            if not (x==0.0 and y==0.0): logged.append((x,y))
        except: pass
logged_set = logged
print("packets:", len(pkts), "logged pairs:", len(logged_set))

def near_any(x, y, tol=0.02):
    for lx,ly in logged_set:
        if abs(x-lx)<tol and abs(y-ly)<tol: return True
    return False

for endian,fmt in (("LE","<f"),("BE",">f")):
    tally = Counter()
    for d in pkts:
        for off in range(0, len(d)-8):
            try:
                x = struct.unpack_from(fmt,d,off)[0]
                y = struct.unpack_from(fmt,d,off+4)[0]
            except: break
            if not (math.isfinite(x) and math.isfinite(y)): continue
            if -0.5<=x<=1.5 and -0.5<=y<=1.5 and (abs(x)>1e-4 or abs(y)>1e-4):
                if near_any(x,y):
                    tally[off]+=1
    print(f"\n=== {endian}: top matching offsets (off: #packets matched a logged pair):")
    for off,c in tally.most_common(12):
        print(f"  off {off:4d} (0x{off:03x}): {c} matches")
