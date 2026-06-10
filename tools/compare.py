#!/usr/bin/env python3
import struct
pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]

# log data lines (x,y) in order
log=[]
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and (p[0][0].isdigit()) and (p[1][0].isdigit() or p[1][0]=='-'):
        try: log.append((float(p[1]),float(p[2]), p[3] if len(p)>3 else "", p[4] if len(p)>4 else ""))
        except: pass

# tail align
k=min(len(P),len(log))
Pt=P[-k:]; Lt=log[-k:]
print(f"packets={len(P)} logframes={len(log)} aligned={k}")

offs=[68,124,187,200,267,280,360,373,386,427,440,453]
def f(d,o):
    try: return struct.unpack_from("<f",d,o)[0]
    except: return float('nan')

# find a region where logged gaze is clearly moving & valid (R=True)
start=None
for i in range(200,k-20):
    if Lt[i][3]=="R=True" and abs(Lt[i][0])>0.1:
        start=i; break
start = start or 300
print(f"\nshowing frames {start}..{start+14}; logged (x,y) vs packet floats at offsets {offs}")
print("logx   logy  | " + "  ".join("o%d"%o for o in offs))
for i in range(start,start+14):
    d=Pt[i]; lx,ly,L,R=Lt[i]
    vals="  ".join("%+.2f"%f(d,o) for o in offs)
    print(f"{lx:+.3f} {ly:+.3f} | {vals}   [{L} {R}]")
