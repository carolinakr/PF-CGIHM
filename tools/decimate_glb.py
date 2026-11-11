"""
Reduce los triangulos de un .glb (agrupamiento de vertices en una rejilla 5D: posicion + UV).

Los modelos de escaneo/fotogrametria traen 100-500 mil triangulos; a la distancia de la calle sobran. Cada vertice cae en una celda
de (x, y, z, u, v); todos los de una celda se funden en uno (promedio de posicion, normal y UV). Como la celda incluye los UV, las
costuras de textura se respetan. El tamano de celda se busca por biseccion hasta acercarse a los triangulos pedidos.

Uso:  python tools/decimate_glb.py entrada.glb salida.glb triangulos_objetivo
Conserva nodos, materiales y texturas; descarta atributos que el motor no usa (color, tangentes).
El motor busca primero Models/Opt/<mismo nombre>.glb (ver Model.cpp); los originales no se tocan.
Requiere: numpy
"""
import json
import struct
import sys
import numpy as np

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_glb(path):
    d = open(path, "rb").read()
    total = struct.unpack_from("<I", d, 8)[0]
    off, js, binc = 12, None, b""
    while off < total:
        cl, ct = struct.unpack_from("<II", d, off)
        if ct == 0x4E4F534A:
            js = json.loads(d[off + 8: off + 8 + cl])
        elif ct == 0x004E4942:
            binc = d[off + 8: off + 8 + cl]
        off += 8 + cl
    return js, binc


def accessor(js, binc, ai):
    a = js["accessors"][ai]
    v = js["bufferViews"][a["bufferView"]]
    dt, nc = CT[a["componentType"]], NC[a["type"]]
    o = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = v.get("byteStride")
    item = np.dtype(dt).itemsize * nc
    if stride and stride != item:
        raw = np.frombuffer(binc, np.uint8, a["count"] * stride, o).reshape(a["count"], stride)
        return np.frombuffer(raw[:, :item].tobytes(), dt).reshape(a["count"], nc)
    return np.frombuffer(binc, dt, a["count"] * nc, o).reshape(a["count"], nc)


def cluster(pos, uv, tris, c, ext):
    key = np.concatenate([np.floor(pos / c), np.floor(uv * ext / c)], axis=1).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    t = inv[tris]
    ok = (t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])
    return inv, t[ok]


def decimate(pos, nor, uv, tris, target):
    if len(tris) <= target:
        return pos, nor, uv, tris
    ext = float((pos.max(0) - pos.min(0)).max())
    lo, hi = ext * 1e-5, ext * 0.5
    best = None
    best_c = hi
    for _ in range(14):
        c = np.sqrt(lo * hi)
        inv, t = cluster(pos, uv, tris, c, ext)
        if len(t) > target:
            lo = c
        else:
            hi = c
            best, best_c = (inv, t), c
    if best is None:
        best = cluster(pos, uv, tris, hi, ext)
        best_c = hi
    inv, t = best
    n = inv.max() + 1
    cnt = np.bincount(inv, minlength=n).astype(np.float64)[:, None]
    U = np.zeros((n, 2)); N = np.zeros((n, 3))
    np.add.at(U, inv, uv); np.add.at(N, inv, nor)
    U /= cnt
    # La posicion se promedia por CELDA ESPACIAL (sin UV): los vertices duplicados de una costura de textura caen en
    # clusters distintos pero deben quedar exactamente en el mismo punto, si no se abren grietas en la malla.
    c_sp = best_c
    skey = np.floor(pos / c_sp).astype(np.int64)
    _, sinv = np.unique(skey, axis=0, return_inverse=True)
    sinv = sinv.reshape(-1)
    scnt = np.bincount(sinv).astype(np.float64)[:, None]
    SP = np.zeros((sinv.max() + 1, 3)); np.add.at(SP, sinv, pos); SP /= scnt
    # cada cluster toma la posicion de la celda espacial de su primer vertice (todos sus miembros comparten celda espacial)
    first = np.zeros(n, np.int64); first[inv[::-1]] = np.arange(len(inv))[::-1]
    P = SP[sinv[first]]
    L = np.linalg.norm(N, axis=1, keepdims=True); L[L == 0] = 1
    N /= L
    used, inv2 = np.unique(t.reshape(-1), return_inverse=True)      # quitar vertices huerfanos
    return P[used], N[used], U[used], inv2.reshape(-1, 3)


def pad4(b, ch=b"\x00"):
    return b + ch * ((4 - len(b) % 4) % 4)


def main(src, dst, target_total):
    js, binc = load_glb(src)
    prims = [(mi, pi) for mi, m in enumerate(js["meshes"]) for pi in range(len(m["primitives"]))]
    counts = []
    for mi, pi in prims:
        p = js["meshes"][mi]["primitives"][pi]
        counts.append(js["accessors"][p["indices"]]["count"] // 3)
    total = sum(counts)
    new_views, new_acc, blob = [], [], b""

    def add_view(data, target=None):
        nonlocal blob
        v = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target:
            v["target"] = target
        new_views.append(v)
        blob += pad4(data)
        return len(new_views) - 1

    for (mi, pi), cnt in zip(prims, counts):
        p = js["meshes"][mi]["primitives"][pi]
        pos = accessor(js, binc, p["attributes"]["POSITION"]).astype(np.float64)
        nor = accessor(js, binc, p["attributes"]["NORMAL"]).astype(np.float64) if "NORMAL" in p["attributes"] else np.tile([0.0, 1.0, 0.0], (len(pos), 1))
        uv = accessor(js, binc, p["attributes"]["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in p["attributes"] else np.zeros((len(pos), 2))
        tris = accessor(js, binc, p["indices"]).reshape(-1, 3).astype(np.int64)
        tgt = max(300, int(target_total * cnt / total))
        P, N, U, T = decimate(pos, nor, uv, tris, tgt)
        P32, N32, U32 = P.astype("<f4"), N.astype("<f4"), U.astype("<f4")
        base = len(new_acc)
        new_acc.append({"bufferView": add_view(P32.tobytes(), 34962), "componentType": 5126, "count": len(P32), "type": "VEC3",
                        "min": P32.min(0).tolist(), "max": P32.max(0).tolist()})
        new_acc.append({"bufferView": add_view(N32.tobytes(), 34962), "componentType": 5126, "count": len(N32), "type": "VEC3"})
        new_acc.append({"bufferView": add_view(U32.tobytes(), 34962), "componentType": 5126, "count": len(U32), "type": "VEC2"})
        I32 = T.reshape(-1).astype("<u4")
        new_acc.append({"bufferView": add_view(I32.tobytes(), 34963), "componentType": 5125, "count": int(I32.size), "type": "SCALAR"})
        p["attributes"] = {"POSITION": base, "NORMAL": base + 1, "TEXCOORD_0": base + 2}
        p["indices"] = base + 3
        print(f"  malla {mi}.{pi}: {cnt:7d} -> {len(T):7d} tris, {len(pos):7d} -> {len(P):7d} vertices")

    for img in js.get("images", []):
        if "bufferView" in img:
            v = js["bufferViews"][img["bufferView"]]
            o = v.get("byteOffset", 0)
            img["bufferView"] = add_view(bytes(binc[o: o + v["byteLength"]]))
    js["accessors"], js["bufferViews"] = new_acc, new_views
    js["buffers"] = [{"byteLength": len(blob)}]
    js.pop("skins", None); js.pop("animations", None)
    jj = pad4(json.dumps(js, separators=(",", ":")).encode(), b" ")
    with open(dst, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(jj) + 8 + len(blob)))
        f.write(struct.pack("<II", len(jj), 0x4E4F534A)); f.write(jj)
        f.write(struct.pack("<II", len(blob), 0x004E4942)); f.write(blob)
    print(f"{src} -> {dst}: {total} -> objetivo {target_total}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]))
