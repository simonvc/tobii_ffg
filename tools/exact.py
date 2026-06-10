#!/usr/bin/env python3
# Decisive: tail-align packets<->log, then for each offset count how many frames
# have LEfloat(packet,off) ~= logged gaze x (or y). If any offset matches most
# valid frames, that's the stored gaze field. If none, gaze is host-derived.
import struct, math

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]
log=[]
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and p[0][0].isdigit() and (p[1][0].isdigit() or p[1][0]=='-'):
        try: log.append((float(p[1]),float(p[2]),"R=True" in l or "L=True" in l))
        except: pass

LAG=1   # packet[i] <-> log[i+LAG], confirmed by validity-byte match at off 154
k=min(len(P),len(log))
Pp=P[-k:]; Ll=log[-k:]
valid=[i for i in range(k) if i+LAG<k and Ll[i+LAG][2]]
print("aligned",k,"valid frames",len(valid),"LAG",LAG)

for endian,fmt in (("LE","<f"),("BE",">f")):
    for which,axis in ((0,'X'),(1,'Y')):
        ranked=[]
        for off in range(0,1720):
            m=0
            for i in valid:
                try: v=struct.unpack_from(fmt,Pp[i],off)[0]
                except: continue
                if math.isfinite(v) and abs(v-Ll[i+LAG][which])<0.02: m+=1
            ranked.append((m,off))
        ranked.sort(reverse=True)
        top=ranked[0]
        print("%s %s: best %d/%d at off %d (0x%x)   next: %s"%(
            endian,axis,top[0],len(valid),top[1],top[1],
            ["%d@%d"%(m,o) for m,o in ranked[1:4]]))
