import struct
class R:
    def __init__(s,d,i=0): s.d=d; s.i=i
    def u8(s): v=s.d[s.i]; s.i+=1; return v
    def u16(s): v=struct.unpack_from('<H',s.d,s.i)[0]; s.i+=2; return v
    def u32(s): v=struct.unpack_from('<I',s.d,s.i)[0]; s.i+=4; return v
    def i32(s): v=struct.unpack_from('<i',s.d,s.i)[0]; s.i+=4; return v
    def u64(s): v=struct.unpack_from('<Q',s.d,s.i)[0]; s.i+=8; return v
    def f(s,n=1):
        v=struct.unpack_from('<%df'%n,s.d,s.i); s.i+=4*n; return v if n>1 else v[0]
    def raw(s,n): v=s.d[s.i:s.i+n]; s.i+=n; return v

def read_nif(path):
    d=open(path,'rb').read()
    i=d.index(b'\n')+1
    r=R(d,i)
    h={'line':d[:i]}
    h['ver']=r.u32(); h['endian']=r.u8(); h['user']=r.u32(); nb=r.u32()
    h['bsver']=r.u32()
    def sstr():
        n=r.u8(); return r.raw(n)
    h['author']=sstr(); h['proc']=sstr(); h['exp']=sstr()
    nt=r.u16(); types=[r.raw(r.u32()).decode() for _ in range(nt)]
    tidx=[r.u16() for _ in range(nb)]
    sizes=[r.u32() for _ in range(nb)]
    ns=r.u32(); h['maxlen']=r.u32()
    strings=[r.raw(r.u32()).decode('latin1') for _ in range(ns)]
    ng=r.u32(); groups=[r.u32() for _ in range(ng)]
    blocks=[]
    for k in range(nb):
        blocks.append([types[tidx[k]], r.raw(sizes[k])])
    nr=r.u32(); roots=[r.i32() for _ in range(nr)]
    assert r.i==len(d),(r.i,len(d))
    return h,blocks,strings,roots

def write_nif(path,h,blocks,strings,roots):
    out=bytearray(h['line'])
    out+=struct.pack('<IBII',h['ver'],h['endian'],h['user'],len(blocks))
    out+=struct.pack('<I',h['bsver'])
    for k in ('author','proc','exp'):
        out+=bytes([len(h[k])])+h[k]
    types=[]
    for t,_ in blocks:
        if t not in types: types.append(t)
    out+=struct.pack('<H',len(types))
    for t in types: out+=struct.pack('<I',len(t))+t.encode()
    for t,_ in blocks: out+=struct.pack('<H',types.index(t))
    for _,b in blocks: out+=struct.pack('<I',len(b))
    out+=struct.pack('<II',len(strings),max([len(s) for s in strings]+[0]))
    for s in strings: out+=struct.pack('<I',len(s))+s.encode('latin1')
    out+=struct.pack('<I',0)
    for _,b in blocks: out+=b
    out+=struct.pack('<I',len(roots))
    for x in roots: out+=struct.pack('<i',x)
    open(path,'wb').write(out)
