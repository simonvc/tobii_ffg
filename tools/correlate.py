#!/usr/bin/env python3
# Find gaze x,y float offsets by correlating each packet-float time series
# with the logged gaze trajectory (robust to filtering/scaling). Pure python.
import struct, math

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]
L = min(len(d) for d in P)

# logged gaze trajectory (in order) — INCLUDE zeros to keep 1:1 phase with packets
Lx, Ly = [], []
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and p[0][0].isdigit() and (p[1][0].isdigit() or p[1][0]=='-'):
        try:
            x=float(p[1]); y=float(p[2]); Lx.append(x); Ly.append(y)
        except: pass

def pearson(a,b):
    n=min(len(a),len(b))
    if n<30: return 0
    a=a[:n]; b=b[:n]
    ma=sum(a)/n; mb=sum(b)/n
    va=sum((x-ma)**2 for x in a); vb=sum((x-mb)**2 for x in b)
    if va<=0 or vb<=0: return 0
    cov=sum((a[i]-ma)*(b[i]-mb) for i in range(n))
    return cov/math.sqrt(va*vb)

def best_corr(series, ref):
    # slide ref over series across lags, return best |corr| and lag
    best=0; blag=0
    for lag in range(-150,151,1):
        if lag>=0: a=series[lag:]; b=ref
        else: a=series; b=ref[-lag:]
        c=pearson(a,b)
        if abs(c)>abs(best): best=c; blag=lag
    return best,blag

# candidate offsets: float varies a lot
import statistics
cands=[]
for off in range(0,L-4):
    vals=[]
    ok=True
    for d in P:
        v=struct.unpack_from("<f",d,off)[0]
        if not math.isfinite(v): ok=False; break
        vals.append(v)
    if not ok: continue
    rng=[v for v in vals if -5<=v<=5]
    if len(rng)<len(vals)*0.8: continue
    if max(rng)-min(rng) < 0.03: continue
    cands.append((off,vals))
print(f"1724 packets={len(P)} logged={len(Lx)} candidate offsets={len(cands)}")

scored=[]
for off,vals in cands:
    cx,lagx=best_corr(vals,Lx)
    cy,lagy=best_corr(vals,Ly)
    best = cx if abs(cx)>=abs(cy) else cy
    which = "X" if abs(cx)>=abs(cy) else "Y"
    scored.append((abs(best),off,cx,lagx,cy,lagy,which))
scored.sort(reverse=True)
print("\nTop offsets by correlation with logged gaze (LE float32):")
for ac,off,cx,lagx,cy,lagy,which in scored[:15]:
    print(f"  off {off:4d} (0x{off:03x}): corrX={cx:+.3f}@{lagx}  corrY={cy:+.3f}@{lagy}  -> {which}")
