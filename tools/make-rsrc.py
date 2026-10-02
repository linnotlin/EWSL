import io, os, struct, sys
from PIL import Image, ImageFilter

LANG = 0x409
RT_ICON = 3
# RT_GROUP_ICON == RT_ICON + 11 == 14. Windows only honours this id for icon
# groups; writing the group under the wrong type makes every icon API
# (LoadIconW / PrivateExtractIconsW / SHGetFileInfoW) find nothing.
RT_GROUP_ICON = 14

# Dense ladder so Windows never has to scale an icon at 100/125/150/200% DPI
# (taskbar, Alt+Tab, title-bar caption, Explorer list/detail all hit a native
# size instead of upscaling a 32px bitmap, which is what looked blurry).
SIZES = (256, 128, 96, 64, 48, 32, 24, 16)


def dib_bytes(im):
    """RGBA image -> ICO DIB payload (BITMAPINFOHEADER + XOR BGRA + AND mask).

    ExtractIconEx and several legacy shell paths refuse PNG frames, so every
    non-256 size is stored as a plain 32bpp DIB -- the layout rc.exe emits.
    """
    w, h = im.size
    px = im.load()
    xor = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = px[x, y]
            row += bytes((b, g, r, a))
        xor += row
    and_stride = ((w + 31) // 32) * 4
    andmask = bytes(h * and_stride)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0,
                      len(xor) + len(andmask), 0, 0, 0, 0)
    return bytes(hdr) + bytes(xor) + andmask


def build_ico(raw, sizes=SIZES):
    """Return the raw bytes of a standard .ico file.

    256px is stored as PNG (lossless); every smaller size is a 32bpp DIB with
    LANCZOS downscaling plus an unsharp pass -- plain downscaling is what made
    the small caption/taskbar sizes look mushy.
    """
    im = Image.open(io.BytesIO(raw)).convert('RGBA')
    entries = []
    frames = []
    off = 6 + 16 * len(sizes)
    for s in sizes:
        f = im.resize((s, s), Image.LANCZOS)
        pct = 180 if s <= 32 else (120 if s <= 64 else 80)
        f = f.filter(ImageFilter.UnsharpMask(radius=1.2, percent=pct, threshold=2))
        if s >= 256:
            b = io.BytesIO()
            f.save(b, 'PNG', optimize=True)
            data = b.getvalue()
            pl, bpp = 0, 0
        else:
            data = dib_bytes(f)
            pl, bpp = 1, 32
        entries.append((s, len(data), off, pl, bpp))
        frames.append(data)
        off += len(data)
    out = bytearray(struct.pack('<HHH', 0, 1, len(sizes)))
    for s, dsz, doff, pl, bpp in entries:
        sz = 0 if s >= 256 else s
        out += struct.pack('<BBBBHHII', sz, sz, 0, 0, pl, bpp, dsz, doff)
    for d in frames:
        out += d
    return bytes(out)


def split_ico(ico):
    """ico -> [(size, payload)] for RT_ICON (id = index+1).

    Each RT_ICON entry holds one image's raw bytes (PNG or DIB), exactly as a
    resource compiler would emit them.
    """
    reserved, typ, count = struct.unpack_from('<HHH', ico, 0)
    assert reserved == 0 and typ == 1, 'not an icon file'
    images = []
    for i in range(count):
        e = 6 + 16 * i
        w, h, cc, rv, pl, bpp, bsz, off = struct.unpack_from('<BBBBHHII', ico, e)
        size = w or h or 256
        images.append((size, ico[off:off + bsz]))
    return images


def build_group(images):
    """Build the RT_GROUP_ICON payload.

    In the PE resource model the group is just an ICONDIR followed by
    ICONDIRENTRYs whose `imageOffset` field is repurposed to hold the RT_ICON
    resource *id* (1..n) -- NOT a byte offset into embedded data. The actual
    image bytes live in the separate RT_ICON resources, so the group carries no
    image payload of its own.
    """
    out = bytearray(struct.pack('<HHH', 0, 1, len(images)))
    for i, (size, payload) in enumerate(images):
        sz = 0 if size >= 256 else size  # 0 means 256 in an ICONDIRENTRY
        # PIL stores PNG images with planes=0/bpp=0 and DIBs with planes=1/bpp=32;
        # mirror the original entry's planes/bpp so the loader decodes correctly.
        pl = 0 if payload[:8].startswith(b'\x89PNG') else 1
        bpp = 0 if pl == 0 else 32
        out += struct.pack('<BBBBHHII', sz, sz, 0, 0, pl, bpp,
                           len(payload), i + 1)
    return bytes(out)


def align4(n, m=4):
    return (n + m - 1) // m * m


def build_section(leaves, rva_base):
    """leaves: [(type_id, name_id, payload)] -> full .rsrc bytes"""
    types = {}
    for t, n, _ in leaves:
        types.setdefault(t, set()).add(n)

    cur = 16 + 8 * len(types)
    type_off = {}
    for t in sorted(types):
        type_off[t] = cur
        cur += 16 + 8 * len(types[t])
    cur = align4(cur)
    name_off = {}
    for t in sorted(types):
        for n in sorted(types[t]):
            name_off[(t, n)] = cur
            cur += 16 + 8
    cur = align4(cur)
    data_off = {}
    for t, n, _ in leaves:
        data_off[(t, n)] = cur
        cur += 16
    cur = align4(cur)

    payload_off = {}
    for t, n, payload in leaves:
        payload_off[(t, n)] = cur
        cur = align4(cur + len(payload))
    total = cur

    out = bytearray(b'\x00' * total)

    def dir_header(numid):
        return struct.pack('<IIHHHH', 0, 0, 0, 0, 0, numid)

    root = bytearray(dir_header(len(types)))
    for t in sorted(types):
        root += struct.pack('<II', t, 0x80000000 | type_off[t])
    out[0:len(root)] = root

    for t in sorted(types):
        td = bytearray(dir_header(len(types[t])))
        for n in sorted(types[t]):
            td += struct.pack('<II', n, 0x80000000 | name_off[(t, n)])
        b, e = type_off[t], type_off[t] + len(td)
        out[b:e] = td

    for t, n, _ in leaves:
        nd = bytearray(dir_header(1))
        nd += struct.pack('<II', LANG, data_off[(t, n)])
        b, e = name_off[(t, n)], name_off[(t, n)] + len(nd)
        out[b:e] = nd

    for t, n, payload in leaves:
        rva = rva_base + payload_off[(t, n)]
        de = struct.pack('<II', rva, len(payload))
        b = data_off[(t, n)]
        out[b:b + len(de)] = de
        b = payload_off[(t, n)]
        out[b:b + len(payload)] = payload

    return bytes(out)


def parse_pe(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    assert bytes(d[pe:pe + 4]) == b'PE\0\0'
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    sectab = pe + 24 + optsz
    return {
        'pe': pe, 'nsec': nsec, 'sectab': sectab,
        'filealign': struct.unpack_from('<I', d, pe + 24 + 36)[0],
        'secalign': struct.unpack_from('<I', d, pe + 24 + 32)[0],
        'sizeofimage': struct.unpack_from('<I', d, pe + 24 + 56)[0],
        'sizeofheaders': struct.unpack_from('<I', d, pe + 24 + 60)[0],
    }


def locate(d, h):
    for i in range(h['nsec']):
        o = h['sectab'] + 40 * i
        if bytes(d[o:o + 8]).rstrip(b'\x00') == b'.rsrc':
            return o
    return None


def insert_section(d, h, idx, va, payload, filealign, secalign):
    nsec = h['nsec'] + 1
    assert h['sectab'] + 40 * nsec <= h['sizeofheaders'], (
        'no room for the section header inside SizeOfHeaders')
    off = align4(len(d), filealign)
    raw = align4(len(payload))
    vs = align4(len(payload), secalign)
    end = va + vs
    struct.pack_into('<I', d, h['pe'] + 6, nsec)
    struct.pack_into('<II', d, h['pe'] + 24 + 112 + 16, va, len(payload))
    struct.pack_into('<I', d, h['pe'] + 24 + 56, end)
    hdr = struct.pack('<8sIIIIIIHHI', b'.rsrc\0\0\0', len(payload), va, raw,
                      off, 0, 0, 0, 0, 0x40000040)
    d[idx:idx + len(hdr)] = hdr
    d.extend(b'\x00' * (off - len(d)))
    d[off:off + len(payload)] = payload
    return va


def patch(path, leaves):
    d = bytearray(io.open(path, 'rb').read())
    h = parse_pe(d)
    idx = locate(d, h)

    if idx is None:
        va = h['sizeofimage']
        section = build_section(leaves, va)
        insert_section(d, h, h['sectab'] + 40 * h['nsec'], va, section,
                       h['filealign'], h['secalign'])
    else:
        va = struct.unpack_from('<I', d, idx + 12)[0]
        raddr = struct.unpack_from('<I', d, idx + 20)[0]
        rsize = struct.unpack_from('<I', d, idx + 16)[0]
        section = build_section(leaves, va)
        assert len(section) <= rsize, 'resources (%d) exceed raw .rsrc (%d)' % (
            len(section), rsize)
        new_raw = align4(len(section))
        d[idx + 16:idx + 24] = struct.pack('<II', new_raw, raddr)
        d[raddr:raddr + len(section)] = section
        for i in range(len(section), new_raw):
            d[raddr + i] = 0
        struct.pack_into('<II', d, h['pe'] + 24 + 112 + 16, va, len(section))

    tmp = path + '.tmp'
    io.open(tmp, 'wb').write(bytes(d))
    if hasattr(os, 'replace'):
        os.replace(tmp, path)
    else:
        os.remove(path)
        io.open(path, 'wb').write(bytes(d))
    return len(section)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    target = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'dist', 'EWSL.exe')
    raw = io.open(os.path.join(root, 'icon.jpg'), 'rb').read()

    ico = build_ico(raw)
    images = split_ico(ico)
    group = build_group(images)

    leaves = []
    for i, (size, payload) in enumerate(images):
        leaves.append((RT_ICON, i + 1, payload))
    leaves.append((RT_GROUP_ICON, 1, group))

    n = patch(target, leaves)

    print('patched %s' % target)
    print('  .rsrc %d bytes, group %d bytes' % (n, len(group)))
    for (t, idn, payload) in leaves:
        print('  type=%d id=%d len=%d head=%s' % (t, idn, len(payload), payload[:8].hex()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
