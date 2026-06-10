#!/usr/bin/env python3
# Verify packet<->log alignment and find the validity field by matching the
# valid/invalid boolean pattern. Scans byte offsets and small lags.
import struct

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]
log=[]
for l in open("captures/talon_gaze_valid.log"):
    p=l.split()
    if len(p)>=3 and p[0][0].isdigit() and (p[1][0].isdigit() or p[1][0]=='-'):
        log.append(1 if ("R=True" in l or "L=True" in l) else 0)

k=min(len(P),len(log))
Pp=P[-k:]; Lg=log[-k:]
base=sum(Lg)/len(Lg)
print(f"aligned {k}, valid fraction {base:.2f}")

best=(0,None,None,None)
for lag in range(-25,26):
    for off in range(0,1724):
        agree=0; tot=0
        for i in range(k):
            j=i+lag
            if j<0 or j>=k: continue
            tot+=1
            b=Pp[i][off]
            # treat nonzero byte as 'valid' guess
            if (1 if b!=0 else 0)==Lg[j]: agree+=1
        if tot>500:
            frac=agree/tot
            score=max(frac,1-frac)   # also catch inverted
            if score>best[0]: best=(score,off,lag,tot)
print("best byte-pattern match: score=%.3f off=%s lag=%s tot=%s"%best)
# also try matching a u32 word ==validity over the gaze block
