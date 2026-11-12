"""Texture conversion: decode Unity textures, halve them, encode DXT1/DXT5 with
mipmaps (or RGBA8 for UI art), and write standard .dds files."""
import os, struct
from multiprocessing import Pool
from PIL import Image
import etcpak

MAX_SIZE = 1024          # Vita cap after halving
HALVE_ABOVE = 64         # textures this small or smaller keep their size

DDSD = 0x1 | 0x2 | 0x4 | 0x1000          # caps, height, width, pixelformat
DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE, DDSD_PITCH = 0x20000, 0x80000, 0x8
DDPF_FOURCC, DDPF_RGB, DDPF_ALPHAPIXELS = 0x4, 0x40, 0x1
DDSCAPS_TEXTURE, DDSCAPS_COMPLEX, DDSCAPS_MIPMAP = 0x1000, 0x8, 0x400000


def dds_header(w, h, mips, fmt, top_size):
    flags = DDSD | (DDSD_MIPMAPCOUNT if mips > 1 else 0)
    caps = DDSCAPS_TEXTURE | ((DDSCAPS_COMPLEX | DDSCAPS_MIPMAP) if mips > 1 else 0)
    if fmt == "RGBA8":
        flags |= DDSD_PITCH
        pf = struct.pack("<II4sIIIII", 32, DDPF_RGB | DDPF_ALPHAPIXELS, b"\0\0\0\0", 32,
                         0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
        pitch = w * 4
    else:
        flags |= DDSD_LINEARSIZE
        pf = struct.pack("<II4sIIIII", 32, DDPF_FOURCC, fmt.encode(), 0, 0, 0, 0, 0)
        pitch = top_size
    return (b"DDS " + struct.pack("<7I", 124, flags, h, w, pitch, 0, mips) + b"\0" * 44 + pf +
            struct.pack("<5I", caps, 0, 0, 0, 0))


def target_size(w, h):
    if max(w, h) > HALVE_ABOVE:
        w, h = max(1, w // 2), max(1, h // 2)
    while max(w, h) > MAX_SIZE:
        w, h = max(1, w // 2), max(1, h // 2)
    return max(4, (w + 3) // 4 * 4), max(4, (h + 3) // 4 * 4)


def _encode(job):
    tid, w, h, mode, raw, ui, dst = job
    try:
        img = Image.frombytes(mode, (w, h), raw).convert("RGBA")
        alpha = img.getchannel("A").getextrema()[0] < 250
        tw, th = target_size(w, h)
        if ui:
            tw, th = max(1, w // 2) if w > HALVE_ABOVE else w, max(1, h // 2) if h > HALVE_ABOVE else h
            img = img.resize((tw, th), Image.LANCZOS)
            body = img.tobytes()
            with open(dst, "wb") as f:
                f.write(dds_header(tw, th, 1, "RGBA8", len(body)) + body)
            return tid, dict(w=tw, h=th, fmt="RGBA8", mips=1, alpha=alpha, srcW=w, srcH=h)
        fmt = "DXT5" if alpha else "DXT1"
        enc = etcpak.compress_bc3 if alpha else etcpak.compress_bc1
        levels, cw, ch = [], tw, th
        level = img.resize((cw, ch), Image.LANCZOS)
        while True:
            pw, ph = max(4, (cw + 3) // 4 * 4), max(4, (ch + 3) // 4 * 4)
            src = level if (pw, ph) == level.size else level.resize((pw, ph), Image.BILINEAR)
            levels.append(enc(src.tobytes(), pw, ph))
            if cw <= 4 and ch <= 4:
                break
            cw, ch = max(1, cw // 2), max(1, ch // 2)
            level = level.resize((max(4, cw), max(4, ch)), Image.LANCZOS)
        with open(dst, "wb") as f:
            f.write(dds_header(tw, th, len(levels), fmt, len(levels[0])))
            for lv in levels:
                f.write(lv)
        return tid, dict(w=tw, h=th, fmt=fmt, mips=len(levels), alpha=alpha, srcW=w, srcH=h)
    except Exception as e:
        return tid, dict(error=repr(e))


def _jobs(textures, out_dir, ui_ids, table, log):
    for tid, o in sorted(textures.items()):
        name = tid.replace(":", "_").replace("/", "_")
        dst = os.path.join(out_dir, "tex", name + ".dds")
        try:
            d = o.read()
            meta = {"name": d.m_Name, "file": "tex/" + name + ".dds"}
            if o.type.name != "Texture2D":
                table[tid] = dict(meta, error="unsupported " + o.type.name)
                continue
            table[tid] = meta
            if os.path.exists(dst):
                with open(dst, "rb") as f:
                    hdr = f.read(128)
                th, tw, mips = struct.unpack_from("<II", hdr, 12) + struct.unpack_from("<I", hdr, 28)
                cc = hdr[84:88]
                fmt = cc.decode() if cc in (b"DXT1", b"DXT5") else "RGBA8"
                table[tid].update(w=tw, h=th, fmt=fmt, mips=max(1, mips), alpha=fmt != "DXT1",
                                  srcW=d.m_Width, srcH=d.m_Height, cached=True)
                continue
            img = d.image
        except Exception as e:
            table[tid] = {"error": "decode: %r" % e}
            continue
        yield (tid, img.width, img.height, img.mode, img.tobytes(), tid in ui_ids, dst)


def export_all(textures, out_dir, jobs, log, ui_ids=frozenset()):
    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    table = {}
    done = 0
    with Pool(max(1, jobs)) as pool:
        for tid, info in pool.imap_unordered(_encode, _jobs(textures, out_dir, ui_ids, table, log), chunksize=1):
            table[tid].update(info)
            done += 1
            if done % 200 == 0:
                log(f"  textures: {done} encoded")
    errs = sum(1 for v in table.values() if "error" in v)
    log(f"textures: {len(table)} referenced, {done} encoded this run, {errs} errors")
    return table
