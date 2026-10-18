"""
A Windows .ico from one picture, for an app's resource script (APP_RC in engine.mk).

    python tools/png_to_ico.py apps/chasm/assets/icon.png apps/chasm/chasm.ico

An .ico is a small directory of the same picture at several sizes, and Windows picks the one
nearest what it is drawing: 16 for the title bar and Explorer's details view, 32 for Alt-Tab,
48 and up for Explorer's larger views. Each size is scaled down here, once, with Lanczos;
leaving it to Windows means it shrinks the one large image at draw time, and that looks worse.

Sizes larger than the source are left out rather than scaled up. A source of 256 or more gets
the 256 entry Explorer's "extra large icons" view uses; a smaller one is enlarged by Explorer
instead, and this says so.

Every entry is stored as PNG, which Windows reads at any size since Vista. Writing the
container by hand rather than through Pillow's ICO writer keeps the resampling ours on every
Pillow version.

Windows' Python (the one with Pillow), not MSYS's - see CLAUDE.md, "Two pythons".
"""
import io
import struct
import sys

from PIL import Image

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    src, dst = sys.argv[1], sys.argv[2]

    im = Image.open(src).convert("RGBA")
    if im.width != im.height:
        print(f"{src} is {im.width}x{im.height}; an icon is square")
        return 1

    sizes = [s for s in SIZES if s <= im.width]
    blobs = []
    for s in sizes:
        frame = im if s == im.width else im.resize((s, s), Image.Resampling.LANCZOS)
        buf = io.BytesIO()
        frame.save(buf, "PNG", optimize=True)
        blobs.append(buf.getvalue())

    #ICONDIR, then one 16-byte ICONDIRENTRY per image, then the images. A width or height of
    #0 in an entry means 256.
    out = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    for s, blob in zip(sizes, blobs):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    out += b"".join(blobs)

    with open(dst, "wb") as f:
        f.write(out)

    print(f"{dst}: {len(out)} bytes, sizes {', '.join(map(str, sizes))}")
    if im.width < 256:
        print(f"  the source is {im.width}x{im.height}: a 256x256 one would make Explorer's largest view sharp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
