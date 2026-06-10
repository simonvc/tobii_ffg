#!/usr/bin/env python3
# Parse the Tobii TTP stream packet TLV structure to find per-stream records.
import struct, math

pkts = []
for l in open("/tmp/gaze_in_packets.txt"):
    p=l.split()
    if len(p)>=2: pkts.append(bytes.fromhex(p[1]))

# pick the most common big size
from collections import Counter
sizes = Counter(len(d) for d in pkts)
print("sizes:", sizes.most_common())
big = max(sizes, key=lambda k:(k))  # largest
sample = [d for d in pkts if len(d)==big][0]
print(f"\nparsing a {big}-byte packet")

def u32be(d,o): return struct.unpack_from(">I",d,o)[0]
def u32le(d,o): return struct.unpack_from("<I",d,o)[0]

# transport header
ttype = u32le(sample,0); tlen = u32le(sample,4)
print(f"transport type={ttype} len={tlen}")
opcode=u32be(sample,8); seq=u32be(sample,12); status=u32be(sample,16); obj=u32be(sample,20)
print(f"opcode=0x{opcode:x} seq={seq} status={status} obj=0x{obj:x}")
# bytes 24..: try 0x00000000 then inner_len
w6=u32be(sample,24); inner_len=u32be(sample,28)
print(f"w6={w6} inner_len=0x{inner_len:x} (={inner_len})  (packet payload after 32 = {len(sample)-32})")

# Walk records starting at 32: [subid u32be][len u32be][data...]
o=32
print("\nrecords [subid len]:")
recs=[]
while o+8 <= len(sample):
    subid=u32be(sample,o); ln=u32be(sample,o+4)
    if ln > len(sample) or ln==0:
        print(f"  stop at {o}: subid=0x{subid:x} len={ln} (implausible)")
        break
    data=sample[o+8:o+8+ln]
    recs.append((subid,o,ln,data))
    print(f"  off {o:4d}: subid=0x{subid:<6x} len={ln:<5d} data[:24]={data[:24].hex()}")
    o += 8+ln
