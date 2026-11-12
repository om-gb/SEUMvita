"""Writer/reader for .smsh mesh pools. Format: docs/DATA_FORMAT.md."""
import struct

MAGIC = b"SMSH"
VERSION = 1
F_NORMALS, F_UV0, F_UV1, F_COLORS, F_IDX32, F_TANGENTS = 1, 2, 4, 8, 16, 32


class MeshPool:
    def __init__(self):
        self.meshes = []      # list of bytes blobs
        self.index = {}       # source key -> mesh index
        self.names = []

    def add(self, key, name, pos, nrm, uv0, uv1, col, submeshes, indices):
        """pos/nrm: lists of (x,y,z); uv: (u,v); col: (r,g,b,a) 0-255;
        submeshes: [(first, count, topology)]; indices: flat list of ints."""
        if key in self.index:
            return self.index[key]
        vc = len(pos)
        flags = 0
        if nrm and len(nrm) == vc: flags |= F_NORMALS
        if uv0 and len(uv0) == vc: flags |= F_UV0
        if uv1 and len(uv1) == vc: flags |= F_UV1
        if col and len(col) == vc: flags |= F_COLORS
        if vc > 0xFFFF: flags |= F_IDX32
        if vc:
            mn = [min(p[i] for p in pos) for i in range(3)]
            mx = [max(p[i] for p in pos) for i in range(3)]
        else:
            mn = mx = [0.0, 0.0, 0.0]
        nb = name.encode("utf-8")[:255]
        out = bytearray()
        out += struct.pack("<I", len(nb)) + nb
        out += b"\0" * (-len(out) % 4)
        out += struct.pack("<IIII", flags, vc, len(indices), len(submeshes))
        for first, count, topo in submeshes:
            out += struct.pack("<III", first, count, topo)
        out += struct.pack("<6f", *mn, *mx)
        out += struct.pack("<%df" % (vc * 3), *[c for p in pos for c in p[:3]])
        if flags & F_NORMALS: out += struct.pack("<%df" % (vc * 3), *[c for n in nrm for c in n[:3]])
        if flags & F_UV0: out += struct.pack("<%df" % (vc * 2), *[c for u in uv0 for c in u[:2]])
        if flags & F_UV1: out += struct.pack("<%df" % (vc * 2), *[c for u in uv1 for c in u[:2]])
        if flags & F_COLORS: out += bytes(int(max(0, min(255, c))) for k in col for c in k[:4])
        if flags & F_IDX32:
            out += struct.pack("<%dI" % len(indices), *indices)
        else:
            out += struct.pack("<%dH" % len(indices), *indices)
        out += b"\0" * (-len(out) % 4)
        self.index[key] = len(self.meshes)
        self.meshes.append(bytes(out))
        self.names.append(name)
        return self.index[key]

    def write(self, path):
        with open(path, "wb") as f:
            f.write(MAGIC + struct.pack("<II", VERSION, len(self.meshes)))
            for m in self.meshes:
                f.write(struct.pack("<I", len(m)))
                f.write(m)


def read(path):
    """Parse a .smsh back into dicts (used by the validator)."""
    data = open(path, "rb").read()
    assert data[:4] == MAGIC, "bad magic"
    ver, n = struct.unpack_from("<II", data, 4)
    assert ver == VERSION
    off, out = 12, []
    for _ in range(n):
        (size,) = struct.unpack_from("<I", data, off); off += 4
        m, p = data[off:off + size], 0
        off += size
        (nl,) = struct.unpack_from("<I", m, p); p += 4
        name = m[p:p + nl].decode("utf-8", "replace"); p += nl; p += -p % 4
        flags, vc, ic, sc = struct.unpack_from("<IIII", m, p); p += 16
        subs = [struct.unpack_from("<III", m, p + 12 * i) for i in range(sc)]; p += 12 * sc
        aabb = struct.unpack_from("<6f", m, p); p += 24
        pos = struct.unpack_from("<%df" % (vc * 3), m, p); p += vc * 12
        if flags & F_NORMALS: p += vc * 12
        if flags & F_UV0: p += vc * 8
        if flags & F_UV1: p += vc * 8
        if flags & F_COLORS: p += vc * 4
        isz = 4 if flags & F_IDX32 else 2
        idx = struct.unpack_from("<%d%s" % (ic, "I" if isz == 4 else "H"), m, p); p += ic * isz
        p += -p % 4
        assert p == size, f"mesh {name}: parsed {p} of {size} bytes"
        out.append(dict(name=name, flags=flags, vc=vc, ic=ic, subs=subs, aabb=aabb, pos=pos, idx=idx))
    return out
