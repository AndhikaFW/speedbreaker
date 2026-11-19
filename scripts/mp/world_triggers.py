import sys,struct,re; sys.path.insert(0,'/home/dhika/.cache/NFS-MostWanted/tools')
import gmem
p=gmem.pid(); m=open('/proc/%d/mem'%p,'rb',0)
def rd(a,n): m.seek(gmem.BASE+a); return m.read(n)
lo,hi=0xA9900000,0xA99D0000
data=rd(lo,hi-lo)
def name_at(a):
    if not (lo<=a<hi): return None
    o=a-lo; e=data.find(b'\x00',o,o+80); t=data[o:e]
    return t.decode('latin1') if len(t)>=3 and all(32<=c<127 for c in t) else None
recs={}
for off in range(0,len(data)-20,4):
    w=struct.unpack('>5I',data[off:off+20]); f=struct.unpack('>3f',data[off:off+12])
    n=name_at(w[3])
    if n and w[4]==0 and all(abs(x)<10000 and x==x for x in f) and any(abs(x)>1 for x in f):
        recs[(lo+off)]=(f,n)
seen=set(); out=[]
for a,(f,n) in sorted(recs.items()):
    key=(n,tuple(round(x) for x in f))
    if key in seen: continue
    seen.add(key); out.append((a,f,n))
print(len(out))
import json; json.dump([(a,list(f),n) for a,f,n in out],open('/tmp/ao/trigs.json','w'))
for a,f,n in out:
    if re.search(r'circuit|sprint|drag|knockout|tollbooth|speedtrap|race|engage|checkpoint|startgrid|bin_',n): print(hex(a),[round(x,1) for x in f],n)
