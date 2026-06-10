#!/usr/bin/env python3
import struct, math
from collections import Counter

pkts = [bytes.fromhex(l.split()[1]) for l in open("/tmp/gaze_in_packets.txt") if len(l.split())>=2]
P = [d for d in pkts if len(d)==1724]
print("1724-byte gaze packets:", len(P))
s = P[0]
def be(o,d=None): d=s if d is None else d; return struct.unpack_from(">I",d,o)[0]
def le(o,d=None): d=s if d is None else d; return struct.unpack_from("<I",d,o)[0]
print("hdr: type=%d len=%d | op=0x%x seq=%d st=%d obj=0x%x w6=%d innerlen=0x%x"%(
    le(0),le(4),be(8),be(12),be(16),be(20),be(24),be(28)))
print("first 48 bytes:", s[:48].hex())
print("bytes 32..96:", s[32:96].hex())

# Walk TLV as [subid u32be][len u32be][data], from offset 32
def walk(d):
    o=32; recs=[]
    while o+8<=len(d):
        subid=struct.unpack_from(">I",d,o)[0]; ln=struct.unpack_from(">I",d,o+4)[0]
        if ln<=0 or o+8+ln>len(d): break
        recs.append((subid,o,ln)); o+=8+ln
    return recs,o
recs,end = walk(s)
print(f"\nwalked {len(recs)} records, ended at {end}/{len(s)}:")
for subid,o,ln in recs[:20]:
    print(f"  off {o:4d} subid=0x{subid:x} len={ln} data={s[o+8:o+8+min(ln,32)].hex()}")
