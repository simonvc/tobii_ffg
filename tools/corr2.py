#!/usr/bin/env python3
# Correlate every LE float offset with logged gaze X and Y at the known alignment
# (lag=1). A linear-transformed gaze field correlates ~|1| even if scaled/offset.
import struct, math

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]
log=[]
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and p[0][0].isdigit() and (p[1][0].isdigit() or p[1][0]=='-'):
        log.append((float(p[1]),float(p[2]),("R=True" in l or "L=True" in l)))

LAG=1
k=min(len(P),len(log)); Pp=P[-k:]; Ll=log[-k:]
idx=[i for i in range(k) if i+LAG<k and Ll[i+LAG][2]]   # valid frames
Lx=[Ll[i+LAG][0] for i in idx]; Ly=[Ll[i+LAG][1] for i in idx]

def pear(a,b):
    n=len(a); ma=sum(a)/n; mb=sum(b)/n
    va=sum((x-ma)**2 for x in a); vb=sum((x-mb)**2 for x in b)
    if va<=0 or vb<=0: return 0
    return sum((a[i]-ma)*(b[i]-mb) for i in range(n))/math.sqrt(va*vb)

rx=[]; ry=[]
for off in range(0,1720):
    vals=[]
    ok=True
    for i in idx:
        v=struct.unpack_from("<f",Pp[i],off)[0]
        if not math.isfinite(v) or abs(v)>1e6: ok=False; break
        vals.append(v)
    if not ok: continue
    if max(vals)-min(vals) < 1e-4: continue
    cx=pear(vals,Lx); cy=pear(vals,Ly)
    rx.append((abs(cx),off,cx)); ry.append((abs(cy),off,cy))
rx.sort(reverse=True); ry.sort(reverse=True)
print("Top |corr| with logged X (LE float, lag=1):")
for ac,off,c in rx[:8]: print("  off %4d (0x%03x): %+.3f"%(off,off,c))
print("Top |corr| with logged Y:")
for ac,off,c in ry[:8]: print("  off %4d (0x%03x): %+.3f"%(off,off,c))
