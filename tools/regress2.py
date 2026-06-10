import struct,math
pkts=[bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P=[d for d in pkts if len(d)==1724]
log=[]
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and p[0][0].isdigit() and (p[1][0].isdigit() or p[1][0]=='-'):
        log.append((float(p[1]),float(p[2]),("R=True" in l or "L=True" in l)))
LAG=1; k=min(len(P),len(log)); Pp=P[-k:]; Ll=log[-k:]
idx=[i for i in range(k) if i+LAG<k and Ll[i+LAG][2]]
def lin(xs,ys):
    n=len(xs); mx=sum(xs)/n; my=sum(ys)/n
    sxx=sum((x-mx)**2 for x in xs); sxy=sum((xs[i]-mx)*(ys[i]-my) for i in range(n))
    a=sxy/sxx; b=my-a*mx
    import statistics
    res=[ys[i]-(a*xs[i]+b) for i in range(n)]
    return a,b,statistics.pstdev(res),max(abs(r) for r in res)
for axis,which in (("X",0),("Y",1)):
    L=[Ll[i+LAG][which] for i in idx]
    best=None
    for off in range(1400,1440):
        V=[struct.unpack_from(">h",Pp[i],off)[0] for i in idx]
        if max(V)-min(V)<2: continue
        a,b,sd,mr=lin(V,L)
        # rank by std of residual (robust to a few filter spikes)
        if best is None or sd<best[0]: best=(sd,off,a,b,mr,min(V),max(V))
    sd,off,a,b,mr,lo,hi=best
    print(f"gaze {axis}: i16BE off {off} (0x{off:x})  {axis.lower()} = {a:.8g}*raw + {b:.6g}  std={sd:.4f} maxres={mr:.3f} raw[{lo},{hi}]")
