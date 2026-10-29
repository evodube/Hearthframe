import struct
from nif import R
def parse_avobj(r):
    o={}
    o['name']=r.i32(); ne=r.u32(); o['extra']=[r.i32() for _ in range(ne)]; o['ctrl']=r.i32()
    o['flags']=r.u32(); o['trans']=r.f(3); o['rot']=r.f(9); o['scale']=r.f(); o['coll']=r.i32()
    return o
def pack_avobj(o):
    b=struct.pack('<iI',o['name'],len(o['extra']))+b''.join(struct.pack('<i',x) for x in o['extra'])+struct.pack('<i',o['ctrl'])
    b+=struct.pack('<I3f9ff i'.replace(' ',''),o['flags'],*o['trans'],*o['rot'],o['scale'],o['coll'])
    return b
def parse_nitrishape(d):
    r=R(d); o=parse_avobj(r)
    o['data']=r.i32(); o['skin']=r.i32()
    nm=r.u32(); o['matnames']=[r.i32() for _ in range(nm)]; o['matextra']=[r.i32() for _ in range(nm)]
    o['activemat']=r.i32(); o['dirty']=r.u8()
    o['shader']=r.i32(); o['alpha']=r.i32()
    assert r.i==len(d),(r.i,len(d)); return o
def parse_tsdata(d):
    r=R(d); g={}
    g['group']=r.i32(); n=r.u16(); g['keep']=r.u8(); g['comp']=r.u8()
    hv=r.u8(); g['v']=[r.f(3) for _ in range(n)] if hv else []
    vf=r.u16(); g['vf']=vf; g['crc']=r.u32()
    hn=r.u8(); g['n']=[r.f(3) for _ in range(n)] if hn else []
    if hn and (vf&4096):
        g['t']=[r.f(3) for _ in range(n)]; g['b']=[r.f(3) for _ in range(n)]
    else: g['t']=g['b']=[]
    g['center']=r.f(3); g['radius']=r.f()
    hc=r.u8(); g['c']=[r.f(4) for _ in range(n)] if hc else []
    nuv=vf&1 if True else 0
    nuv=(vf&1)
    g['uv']=[[r.f(2) for _ in range(n)] for _ in range(nuv)]
    g['cons']=r.u16(); g['add']=r.i32()
    ntri=r.u16(); ntp=r.u32(); ht=r.u8()
    g['tri']=[(r.u16(),r.u16(),r.u16()) for _ in range(ntri)] if ht else []
    nmg=r.u16()
    for _ in range(nmg):
        k=r.u16(); r.raw(2*k)
    assert r.i==len(d),(r.i,len(d)); return g
def parse_bstrishape(d):
    r=R(d); o=parse_avobj(r)
    o['bcenter']=r.f(3); o['bradius']=r.f(); o['skin']=r.i32(); o['shader']=r.i32(); o['alpha']=r.i32()
    o['desc']=r.u64(); o['ntri']=r.u16(); o['nv']=r.u16(); o['dsize']=r.u32()
    o['vdata']=r.raw(o['dsize']-o['ntri']*6) if o['dsize'] else b''
    o['tris']=r.raw(o['ntri']*6) if o['dsize'] else b''
    rest=len(d)-r.i
    o['rest']=r.raw(rest)
    return o
