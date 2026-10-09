"""Generate Terrain Shadows heightmaps from Fallout 4 LAND records.

Matches xLODGen 132's FO4 height map export, which crashes on worldspaces
whose land grid has holes.
"""

import argparse
from array import array
from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
PLUGINS = (
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm",
)
# Worlds with their own land; parent-land children resolve to these.
WORLDSPACES = ("Commonwealth", "SanctuaryHillsWorld", "DiamondCity", "DLC03FarHarbor", "NukaWorld")
CELL = 32
VERTS = 33
ZERO = 32767
COMPRESSED = 0x00040000


def subrecords(data):
    offset, extended = 0, None
    while offset + 6 <= len(data):
        tag = data[offset:offset + 4]
        size = struct.unpack_from("<H", data, offset + 4)[0]
        offset += 6
        if tag == b"XXXX":
            extended = struct.unpack_from("<I", data, offset)[0]
            offset += size
            continue
        if extended is not None:
            size, extended = extended, None
        yield tag, data[offset:offset + size]
        offset += size


def decode_heights(vhgt):
    row = struct.unpack_from("<f", vhgt)[0]
    deltas = struct.unpack_from(f"<{VERTS * VERTS}b", vhgt, 4)
    heights = []
    for r in range(VERTS):
        row += deltas[r * VERTS]
        value = row
        heights.append(value)
        for c in range(1, VERTS):
            value += deltas[r * VERTS + c]
            heights.append(value)
    return heights


class LoadOrder:
    def __init__(self):
        self.worlds = {}
        self.land = {}

    def load(self, path):
        data = path.read_bytes()
        header = struct.unpack_from("<I", data, 4)[0]
        masters = [v.rstrip(b"\0").decode("latin-1")
                   for t, v in subrecords(data[24:24 + header]) if t == b"MAST"]
        self.owner = lambda form: ((masters[form >> 24] if form >> 24 < len(masters) else path.name).lower(),
                                   form & 0xFFFFFF)
        self.walk(data, 24 + header, len(data), None, None)

    def body(self, data, offset, size, flags):
        raw = data[offset + 24:offset + 24 + size]
        return zlib.decompress(raw[4:]) if flags & COMPRESSED else raw

    def walk(self, data, offset, end, world, cell):
        while offset < end:
            tag = data[offset:offset + 4]
            size, flags, form = struct.unpack_from("<III", data, offset + 4)
            if tag == b"GRUP":
                kind = struct.unpack_from("<i", data, offset + 12)[0]
                if kind != 0 or data[offset + 8:offset + 12] == b"WRLD":
                    inner = self.owner(flags) if kind == 1 else world
                    self.walk(data, offset + 24, offset + size, inner, cell)
                offset += size
                continue
            if tag == b"WRLD":
                fields = dict(subrecords(self.body(data, offset, size, flags)))
                name = fields[b"EDID"].rstrip(b"\0").decode("latin-1")
                land = self.worlds.get(name, (None, 0.0))[1]
                if b"DNAM" in fields:
                    land = struct.unpack_from("<f", fields[b"DNAM"])[0] / 8
                self.worlds[name] = (self.owner(form), land)
            elif tag == b"CELL":
                fields = dict(subrecords(self.body(data, offset, size, flags)))
                cell = struct.unpack_from("<ii", fields[b"XCLC"]) if b"XCLC" in fields else None
            elif tag == b"LAND" and world and cell:
                fields = dict(subrecords(self.body(data, offset, size, flags)))
                self.land[(world, *cell)] = decode_heights(fields[b"VHGT"]) if b"VHGT" in fields else None
            offset += 24 + size


def dds_l16(width, height, pixels):
    header = struct.pack("<4s7I44x", b"DDS ", 124, 0x00081007, height, width, width * height * 2, 0, 0)
    pixel_format = struct.pack("<2I4s5I", 32, 0x00020000, b"\0\0\0\0", 16, 0xFFFF, 0, 0, 0)
    return header + pixel_format + struct.pack("<5I", 0x1000, 0, 0, 0, 0) + pixels.tobytes()


def export(order, name, out):
    world, default = order.worlds[name]
    cells = {(x, y): h for (w, x, y), h in order.land.items() if w == world}
    west, east = min(x for x, _ in cells), max(x for x, _ in cells)
    south, north = min(y for _, y in cells), max(y for _, y in cells)
    width, height = (east - west + 1) * CELL, (north - south + 1) * CELL
    pixels = array("H", [round(default) + ZERO]) * (width * height)
    for (x, y), heights in cells.items():
        if heights is None:
            continue
        left = (x - west) * CELL
        for r in range(CELL):
            start = ((north - y) * CELL + CELL - 1 - r) * width + left
            pixels[start:start + CELL] = array("H", (round(v) + ZERO for v in heights[r * VERTS:r * VERTS + CELL]))
    low, high = min(pixels) - ZERO, max(pixels) - ZERO
    path = out / f"{name}.Terrain.HeightMap.{west}.{south}.{east}.{north}.{low}.{high}.dds"
    path.write_bytes(dds_l16(width, height, pixels))
    print(f"{path.name} {width}x{height}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", type=Path, help="Fallout 4 Data directory")
    parser.add_argument("--out", type=Path, default=ROOT / "package/Textures/Terrain/HeightMaps")
    args = parser.parse_args()
    order = LoadOrder()
    for plugin in PLUGINS:
        order.load(args.data / plugin)
    args.out.mkdir(parents=True, exist_ok=True)
    for stale in args.out.glob("*.Terrain.HeightMap.*.dds"):
        stale.unlink()
    for name in WORLDSPACES:
        export(order, name, args.out)


if __name__ == "__main__":
    main()
