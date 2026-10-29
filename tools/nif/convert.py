"""LE (bsver 83) painting NIF -> SSE (bsver 100) frame NIF for FolderGallery.
- NiTriShape/NiTriShapeData -> BSTriShape (vertex desc as the existing SSE frames)
- turned 180 degrees so the picture faces +Y, back of the frame at y = 0 (what the plugin expects)
- shapes renamed Canvas -> SGPicture, Frame -> SGFrame; texture paths left exactly as they are
- collision box moved with the mesh (bhkRigidBodyT translation)"""
import struct, math, sys
from nif import read_nif, write_nif
from geom import parse_nitrishape, parse_tsdata, pack_avobj

DESC = 0x0003B00007650408  # pos+bitanX(16) uv(4) normal+bitanY(4) tangent+bitanZ(4) colour(4)
HAVOK = 69.99125

def half(x): return struct.pack('<e', x)
def nb(v): return max(0, min(255, int(round((v + 1.0) * 127.5))))

def convert(src, dst, root_name):
    h, blocks, strings, roots = read_nif(src)
    assert h['bsver'] == 83, h['bsver']
    # overall depth range -> shift so the back sits at y=0 after turning
    ymax = -1e9
    geo = {}
    for k, (t, d) in enumerate(blocks):
        if t == 'NiTriShape':
            o = parse_nitrishape(d); g = parse_tsdata(blocks[o['data']][1]); geo[k] = (o, g)
            ymax = max(ymax, max(v[1] for v in g['v']))
    shift = ymax
    T = lambda v: (-v[0], -v[1] + shift, v[2])
    R = lambda v: (-v[0], -v[1], v[2])

    keep = [k for k, (t, _) in enumerate(blocks) if t != 'NiTriShapeData']
    remap = {old: new for new, old in enumerate(keep)}
    ref = lambda x: remap.get(x, -1) if x >= 0 else -1
    out = []
    for k in keep:
        t, d = blocks[k]
        if t == 'NiTriShape':
            o, g = geo[k]
            name = strings[o['name']]
            n = len(g['v'])
            P = [T(v) for v in g['v']]
            N = [R(v) for v in g['n']]
            TG = [R(v) for v in g['t']] if g['t'] else [(1, 0, 0)] * n
            BT = [R(v) for v in g['b']] if g['b'] else [(0, 0, 1)] * n
            UV = g['uv'][0] if g['uv'] else [(0, 0)] * n
            if name == 'Canvas':  # whole picture on the canvas
                UV = [(0.0 if u < 0.5 else 1.0, 0.0 if v < 0.5 else 1.0) for u, v in UV]
            C = g['c'] if g['c'] else [(1, 1, 1, 1)] * n
            vd = bytearray()
            for i in range(n):
                vd += struct.pack('<4f', *P[i], BT[i][0])
                vd += half(UV[i][0]) + half(UV[i][1])
                vd += bytes([nb(N[i][0]), nb(N[i][1]), nb(N[i][2]), nb(BT[i][1])])
                vd += bytes([nb(TG[i][0]), nb(TG[i][1]), nb(TG[i][2]), nb(BT[i][2])])
                vd += bytes([max(0, min(255, int(round(c * 255)))) for c in C[i]])
            tris = b''.join(struct.pack('<3H', *tr) for tr in g['tri'])
            cx = sum(p[0] for p in P) / n; cy = sum(p[1] for p in P) / n; cz = sum(p[2] for p in P) / n
            rad = max(math.dist((cx, cy, cz), p) for p in P)
            o2 = dict(o); o2['coll'] = ref(o['coll'])
            b = pack_avobj(o2)
            b += struct.pack('<4f', cx, cy, cz, rad)
            b += struct.pack('<iii', -1, ref(o['shader']), ref(o['alpha']))
            b += struct.pack('<QHHI', DESC, len(g['tri']), n, len(vd) + len(tris))
            b += bytes(vd) + tris + struct.pack('<I', 0)
            out.append(['BSTriShape', b])
        elif t in ('BSFadeNode', 'NiNode'):
            # NiAVObject then children / effects ref lists
            from nif import R as Rd
            r = Rd(d); ne = None
            r.i32(); ne = r.u32(); r.i += 4 * ne; r.i32()  # name, extra, controller
            r.u32(); r.i += 4 * 13; coll_at = r.i; r.i += 4
            ch_at = r.i; nch = r.u32()
            dd = bytearray(d)
            c = struct.unpack_from('<i', d, coll_at)[0]; struct.pack_into('<i', dd, coll_at, ref(c))
            for j in range(nch):
                c = struct.unpack_from('<i', d, ch_at + 4 + 4 * j)[0]; struct.pack_into('<i', dd, ch_at + 4 + 4 * j, ref(c))
            eff_at = ch_at + 4 + 4 * nch; ne2 = struct.unpack_from('<I', d, eff_at)[0]
            for j in range(ne2):
                c = struct.unpack_from('<i', d, eff_at + 4 + 4 * j)[0]; struct.pack_into('<i', dd, eff_at + 4 + 4 * j, ref(c))
            out.append([t, bytes(dd)])
        elif t == 'bhkCollisionObject':
            dd = bytearray(d)
            tgt, fl, body = struct.unpack_from('<iHi', d, 0)
            struct.pack_into('<iHi', dd, 0, ref(tgt), fl, ref(body))
            out.append([t, bytes(dd)])
        elif t == 'bhkRigidBody':
            dd = bytearray(d)
            struct.pack_into('<i', dd, 0, ref(struct.unpack_from('<i', d, 0)[0]))
            tx, ty, tz, tw = struct.unpack_from('<4f', d, 52)
            # centre turns with the mesh and moves with the shift
            struct.pack_into('<4f', dd, 52, -tx, -ty + shift / HAVOK, tz, tw)
            out.append(['bhkRigidBodyT', bytes(dd)])
        elif t == 'BSLightingShaderProperty':
            # shader type u32, name i32, extra list, controller -> texture set ref after flags/uv
            dd = bytearray(d)
            ne = struct.unpack_from('<I', d, 8)[0]
            ctrl_at = 12 + 4 * ne
            c = struct.unpack_from('<i', d, ctrl_at)[0]; struct.pack_into('<i', dd, ctrl_at, ref(c))
            ts_at = ctrl_at + 4 + 8 + 16  # flags1, flags2, uv offset, uv scale
            c = struct.unpack_from('<i', d, ts_at)[0]; struct.pack_into('<i', dd, ts_at, ref(c))
            out.append([t, bytes(dd)])
        elif t in ('BSXFlags', 'bhkBoxShape', 'BSShaderTextureSet'):
            out.append([t, d])
        else:
            raise SystemExit(f'unhandled block {t} in {src}')
    strings = [{'Canvas': 'SGPicture', 'Frame': 'SGFrame', 'Scene Root': root_name}.get(s, s) for s in strings]
    h['bsver'] = 100
    h['author'] = b'FolderGallery\x00'; h['proc'] = b'\x00'; h['exp'] = b'\x00'
    write_nif(dst, h, out, strings, [ref(x) for x in roots])
    return ymax
