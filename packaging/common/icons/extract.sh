#!/bin/sh
# Extracts the Linux hicolor PNGs of the application icon from the master .icns (plan/features/0015
# A3). The icon is the v1 icon (branch `main`: resources/icons/icons.icns and icons.ico, copied as
# hio-monitor.icns / hio-monitor.ico). Run once when the master icon changes; the PNGs are committed
# (Git LFS, .gitattributes).
#
# The .icns holds 128, 256 and 512 px representations. They are written as they are; the smaller
# sizes (16, 24, 32, 48, 64 px) are scaled down from the 512 px representation (Lanczos).
#
# Needs: python3 with Pillow (e.g. `apt install python3-pil`).
# Usage: packaging/common/icons/extract.sh
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
python3 -I - "$here/hio-monitor.icns" "$here" <<'PY'
import sys
from PIL import Image
from PIL.IcnsImagePlugin import IcnsFile

source, out = sys.argv[1], sys.argv[2]
with open(source, "rb") as handle:
    icns = IcnsFile(handle)
    native = {size[0]: icns.getimage(size).convert("RGBA") for size in icns.itersizes()
              if size[2] == 1}
master = native[512]
for size in (16, 24, 32, 48, 64, 128, 256, 512):
    image = native.get(size) or master.resize((size, size), Image.LANCZOS)
    assert image.size == (size, size), (size, image.size)
    image.save(f"{out}/hio-monitor-{size}.png", optimize=True)
    print(f"hio-monitor-{size}.png", "(native)" if size in native else "(scaled from 512)")
PY
