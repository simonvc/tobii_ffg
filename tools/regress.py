#!/usr/bin/env python3
# Pin exact offset + scale/offset for gaze X,Y stored as BE fixed-point int.
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

def lin(xs,ys):  # least squares ys=a*xs+b ; return a,b,maxresid
    n=len(xs); mx=sum(xs)/n; my=sum(ys)/n
    sxx=sum((x-mx)**2 for x in xs); sxy=sum((xs[i]-mx)*(ys[i]-my) for i in range(n))
    a=sxy/sxx; b=my-a*mx
    resid=max(abs(ys[i]-(a*xs[i]+b)) for i in range(n))
    return a,b,resid

for axis,which,offs in (("X",0,range(1405,1416)),("Y",1,range(1419,1430))):
    print(f"--- gaze {axis} ---")
    L=[Ll[i+LAG][which] for i in idx]
    for fmt,sz,nm in (("<i",4,"i32LE"),(">i",4,"i32BE"),("<h",2,"i16LE"),(">h",2,"i16BE")):
        for off in offs:
            try: V=[struct.unpack_from(fmt,Pp[i],off)[0] for i in idx]
            except: continue
            if max(V)-min(V)<2: continue
            a,b,r=lin(V,L)
            if r<0.2:
                print(f"  {nm} off {off} (0x{off:x}): x = {a:.9g}*raw + {b:.6g}   maxresid={r:.2e}  rawrange[{min(V)},{max(V)}]")
