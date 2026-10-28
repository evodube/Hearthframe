#!/usr/bin/env python3
"""Copies the frame meshes to meshes/FolderGallery/ with texture paths moved to textures/FolderGallery/.

  textures\\SkyrimGallery\\Blank.dds        -> textures\\FolderGallery\\Blank.dds
  textures\\SkyrimGallery\\Canvas_n.dds     -> textures\\FolderGallery\\Canvas_n.dds
  textures\\SkyrimGallery\\Frames\\X.dds     -> textures\\FolderGallery\\Frames\\X.dds
  (vanilla texture paths are left alone)

Only BSShaderTextureSet blocks change; the header's block-size table is updated to match.
Everything else is copied byte for byte. Skyrim SE NIFs (20.2.0.7, user 12, bsver 100).

Usage: python3 tools/rebase_meshes.py <dir with Frame_*.nif> <output Data dir>
"""
import glob, os, struct, sys

MAP = [
    ("textures\\skyrimgallery\\frames\\", "textures\\FolderGallery\\Frames\\"),
    ("textures\\skyrimgallery\\", "textures\\FolderGallery\\"),
]


def remap(path):
    low = path.lower()
    for old, new in MAP:
        if low.startswith(old):
            return new + path[len(old):]
    return path


def rebase(data):
    nl = data.index(b"\n") + 1
    o = nl
    ver, = struct.unpack_from("<I", data, o); o += 4
    o += 1  # endian
    uver, nblocks = struct.unpack_from("<II", data, o); o += 8
    bsver, = struct.unpack_from("<I", data, o); o += 4
    if ver != 0x14020007 or uver != 12 or bsver != 100:
        raise ValueError(f"not a Skyrim SE nif (ver {ver:#x}, user {uver}, bs {bsver})")
    for _ in range(3):  # author, process script, export script (short strings)
        o += 1 + data[o]
    ntypes, = struct.unpack_from("<H", data, o); o += 2
    types = []
    for _ in range(ntypes):
        n, = struct.unpack_from("<I", data, o); o += 4
        types.append(data[o:o + n].decode("latin1")); o += n
    btypes = [types[v & 0x7FFF] for v in struct.unpack_from(f"<{nblocks}H", data, o)]; o += 2 * nblocks
    sizes_at = o
    sizes = list(struct.unpack_from(f"<{nblocks}I", data, o)); o += 4 * nblocks
    nstr, _maxlen = struct.unpack_from("<II", data, o); o += 8
    for _ in range(nstr):
        n, = struct.unpack_from("<I", data, o); o += 4 + n
    ngroups, = struct.unpack_from("<I", data, o); o += 4 + 4 * ngroups
    header = bytearray(data[:o])
    blocks_start = o

    out_blocks, changes = [], []
    for t, sz in zip(btypes, sizes):
        block = data[o:o + sz]; o += sz
        if t == "BSShaderTextureSet":
            k, = struct.unpack_from("<I", block, 0)
            p = 4; texs = []
            for _ in range(k):
                n, = struct.unpack_from("<I", block, p); p += 4
                texs.append(block[p:p + n].decode("latin1")); p += n
            if p != len(block):
                raise ValueError("unexpected BSShaderTextureSet layout")
            new = [remap(x) for x in texs]
            changes += [(a, b) for a, b in zip(texs, new) if a != b]
            block = struct.pack("<I", k) + b"".join(struct.pack("<I", len(x)) + x.encode("latin1") for x in new)
        out_blocks.append(block)
    footer = data[o:]
    struct.pack_into(f"<{nblocks}I", header, sizes_at, *[len(b) for b in out_blocks])
    return bytes(header) + b"".join(out_blocks) + footer, changes


def main():
    src, out = sys.argv[1], sys.argv[2]
    dst = os.path.join(out, "Meshes", "FolderGallery")
    os.makedirs(dst, exist_ok=True)
    files = sorted(glob.glob(os.path.join(src, "Frame_*.nif")))
    for f in files:
        data, changes = rebase(open(f, "rb").read())
        open(os.path.join(dst, os.path.basename(f)), "wb").write(data)
        print(f"{os.path.basename(f)}: {len(changes)} path(s) moved")
    print(f"{len(files)} mesh(es) written to {dst}")


if __name__ == "__main__":
    main()
