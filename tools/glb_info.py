"""
Resume un .glb: triangulos, materiales, texturas embebidas, y caja envolvente (tamano en metros).

Uso:  python tools/glb_info.py Models/*.glb
Sirve para decidir escala/orientacion al meter un modelo en la escena (1 unidad = 1 m, Y arriba).
"""
import json
import struct
import sys
import numpy as np


def load_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF", "no es un .glb"
    off = 12
    js = None
    while off < length:
        clen, ctype = struct.unpack_from("<II", data, off)
        if ctype == 0x4E4F534A:  # JSON
            js = json.loads(data[off + 8: off + 8 + clen])
        off += 8 + clen
    return js


def node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], dtype=np.float64).reshape(4, 4).T
    t = np.array(n.get("translation", [0, 0, 0]), dtype=np.float64)
    q = n.get("rotation", [0, 0, 0, 1])
    s = np.array(n.get("scale", [1, 1, 1]), dtype=np.float64)
    x, y, z, w = q
    r = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    m = np.eye(4)
    m[:3, :3] = r * s
    m[:3, 3] = t
    return m


def info(path):
    js = load_glb(path)
    acc = js["accessors"]
    meshes = js.get("meshes", [])
    nodes = js.get("nodes", [])
    tris = 0
    for m in meshes:
        for p in m["primitives"]:
            if "indices" in p:
                tris += acc[p["indices"]]["count"] // 3
            else:
                tris += acc[p["attributes"]["POSITION"]]["count"] // 3

    lo = np.full(3, np.inf)
    hi = np.full(3, -np.inf)

    def walk(ni, parent):
        m = parent @ node_matrix(nodes[ni])
        mi = nodes[ni].get("mesh")
        if mi is not None:
            for p in meshes[mi]["primitives"]:
                a = acc[p["attributes"]["POSITION"]]
                if "min" in a and "max" in a:
                    mn, mx = a["min"], a["max"]
                    for cx in (mn[0], mx[0]):
                        for cy in (mn[1], mx[1]):
                            for cz in (mn[2], mx[2]):
                                v = (m @ np.array([cx, cy, cz, 1.0]))[:3]
                                np.minimum(lo, v, out=lo)
                                np.maximum(hi, v, out=hi)
        for c in nodes[ni].get("children", []):
            walk(c, m)

    scene = js["scenes"][js.get("scene", 0)]
    for r in scene["nodes"]:
        walk(r, np.eye(4))

    size = hi - lo
    return {
        "tris": tris,
        "meshes": len(meshes),
        "materials": len(js.get("materials", [])),
        "images": len(js.get("images", [])),
        "min": lo, "max": hi, "size": size,
    }


if __name__ == "__main__":
    for path in sys.argv[1:]:
        i = info(path)
        s = i["size"]
        print(f"{path}\n  tris={i['tris']:,} meshes={i['meshes']} materiales={i['materials']} imagenes={i['images']}"
              f"\n  tamano (X,Y,Z) = {s[0]:.3f} x {s[1]:.3f} x {s[2]:.3f}"
              f"\n  min = {np.round(i['min'], 3)}  max = {np.round(i['max'], 3)}")
