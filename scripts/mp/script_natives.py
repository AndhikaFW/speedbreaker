import struct,sys
d=open('/home/dhika/.cache/NFS-MostWanted/tools/image.bin','rb').read()
B=0x82000000
def w(a): o=a-B; return struct.unpack('>I',d[o:o+4])[0]
def s(a):
    o=a-B
    if not (0<=o<len(d)): return None
    e=d.find(b'\x00',o)
    t=d[o:e]
    return t.decode('latin1') if 2<=len(t)<48 and all(32<=c<127 for c in t) else None
start,end=0x8235E8A8,0x82361000
regs={}
consts=[]
a=start
while a<end:
    i=w(a); op=i>>26; rd=(i>>21)&31; ra=(i>>16)&31; imm=i&0xFFFF
    simm=imm-0x10000 if imm&0x8000 else imm
    if op==15:   # addis / lis
        regs[rd]=((simm<<16) if ra==0 else (regs.get(ra,0)+(simm<<16)))&0xFFFFFFFF
    elif op==14 and ra in regs:  # addi
        v=(regs[ra]+simm)&0xFFFFFFFF; regs[rd]=v; consts.append((a,v))
    elif op==24 and ra in regs:  # ori (rs=rd field is source)
        pass
    a+=4
out=[]
last=None
for a,v in consts:
    t=s(v) if 0x82000000<=v<0x82100000 else None
    if t: last=t
    elif 0x82100000<=v<0x82CD0000 and last:
        out.append((last,v)); last=None
for n,v in out: print('%08X %s'%(v,n))
