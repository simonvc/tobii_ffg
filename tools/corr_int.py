#!/usr/bin/env python3
# Correlate int16/int32 (LE/BE, signed) at every offset with logged gaze X/Y at lag=1.
import struct, math
pkts=[bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P=[d for d in pkts if len(d)==1724]
log=[]
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and p[0][0].isdigit() and (p[1][0].isdigit() or p[1][0]=='-'):
        log.append((float(p[1]),float(p[2]),("R=True" in l or "L=True" in l)))
LAG=1
k=min(len(P),len(log)); Pp=P[-k:]; Ll=log[-k:]
idx=[i for i in range(k) if i+LAG<k and Ll[i+LAG][2]]
Lx=[Ll[i+LAG][0] for i in idx]; Ly=[Ll[i+LAG][1] for i in idx]
def pear(a,b):
    n=len(a); ma=sum(a)/n; mb=sum(b)/n
    va=sum((x-ma)**2 for x in a); vb=sum((x-mb)**2 for x in b)
    if va<=0 or vb<=0: return 0
    return sum((a[i]-ma)*(b[i]-mb) for i in range(n))/math.sqrt(va*vb)

for name,fmt,sz in (("i16LE","<h",2),("i16BE",">h",2),("i32LE","<i",4),("i32BE",">i",4)):
    rx=[]; ry=[]
    for off in range(0,1724-sz):
        vals=[struct.unpack_from(fmt,Pp[i],off)[0] for i in idx]
        if max(vals)-min(vals)<2: continue
        cx=pear(vals,Lx); cy=pear(vals,Ly)
        rx.append((abs(cx),off,cx)); ry.append((abs(cy),off,cy))
    rx.sort(reverse=True); ry.sort(reverse=True)
    print(f"== {name}: topX {[ '%+.2f@%d'%(c,o) for _,o,c in rx[:4] ]}  topY {[ '%+.2f@%d'%(c,o) for _,o,c in ry[:4] ]}")
