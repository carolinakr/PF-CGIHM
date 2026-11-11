"""
Hornea la animacion de un .glb con esqueleto (p. ej. Mixamo) en una secuencia de mallas estaticas.

El motor no reproduce esqueletos: este script calcula la pose de cada cuadro del ciclo (skinning por vertice) y escribe
un .glb nuevo con UNA MALLA POR CUADRO (mismos UV, indices, materiales y texturas; solo cambian posicion y normal).
El motor carga el archivo con Model::LoadModelFrames y dibuja el cuadro que toca.

Uso:  python tools/bake_skinned_glb.py entrada.glb salida.glb [cuadros=24] [--keep-root] [--only-skin=N]
  --keep-root   : conserva el desplazamiento de la cadera (por defecto se quita para que camine "en su lugar")
  --only-skin=N : hornea solo el personaje del esqueleto N (un .glb puede traer varios personajes)
Requiere: numpy
"""
import json
import struct
import sys
import numpy as np


def load_glb(path):
    d = open(path, "rb").read()
    total = struct.unpack_from("<I", d, 8)[0]
    off, js, binc = 12, None, None
    while off < total:
        cl, ct = struct.unpack_from("<II", d, off)
        if ct == 0x4E4F534A:
            js = json.loads(d[off + 8: off + 8 + cl])
        elif ct == 0x004E4942:
            binc = d[off + 8: off + 8 + cl]
        off += 8 + cl
    return js, binc


CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def accessor(js, binc, ai):
    a = js["accessors"][ai]
    v = js["bufferViews"][a["bufferView"]]
    dt, nc = CT[a["componentType"]], NC[a["type"]]
    o = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    isz = np.dtype(dt).itemsize * nc
    stride = v.get("byteStride") or isz
    if stride != isz:
        raw = np.frombuffer(binc, dtype=np.uint8, count=a["count"] * stride, offset=o).reshape(a["count"], stride)
        arr = np.frombuffer(raw[:, :isz].tobytes(), dtype=dt).reshape(a["count"], nc)
    else:
        arr = np.frombuffer(binc, dtype=dt, count=a["count"] * nc, offset=o).reshape(a["count"], nc)
    if a.get("normalized"):
        arr = arr.astype(np.float64) / np.iinfo(dt).max
    return arr


def quat_to_mat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
        [0, 0, 0, 1]], dtype=np.float64)


def slerp(a, b, t):
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    d = np.dot(a, b)
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        r = a + t * (b - a)
        return r / np.linalg.norm(r)
    th = np.arccos(d)
    return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)


def sample(times, values, t, path):
    """Interpolacion lineal (slerp para rotaciones) con el tiempo acotado al rango de la animacion."""
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    i = np.searchsorted(times, t, side="right") - 1
    f = (t - times[i]) / (times[i + 1] - times[i])
    if path == "rotation":
        return slerp(values[i], values[i + 1], f)
    return values[i] * (1 - f) + values[i + 1] * f


def main():
    src, dst = sys.argv[1], sys.argv[2]
    nframes = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else 24
    keep_root = "--keep-root" in sys.argv
    only_skin = None   # --only-skin=N : hornea solo el personaje del esqueleto N (para separar un .glb con varios personajes)
    for a in sys.argv:
        if a.startswith("--only-skin="):
            only_skin = int(a.split("=")[1])
    js, binc = load_glb(src)
    nodes = js["nodes"]
    parent = [-1] * len(nodes)
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i

    anim = js["animations"][0]
    chan = {}   # (nodo, ruta) -> (tiempos, valores)
    dur = 0.0
    for ch in anim["channels"]:
        smp = anim["samplers"][ch["sampler"]]
        if smp.get("interpolation", "LINEAR") == "CUBICSPLINE":
            raise SystemExit("CUBICSPLINE no soportado")
        times = accessor(js, binc, smp["input"]).reshape(-1).astype(np.float64)
        vals = accessor(js, binc, smp["output"]).astype(np.float64)
        chan[(ch["target"]["node"], ch["target"]["path"])] = (times, vals)
        dur = max(dur, times[-1])
    print(f"animacion '{anim.get('name')}'  {dur:.3f} s  {len(chan)} canales  {nframes} cuadros")

    def local_matrix(ni, t):
        n = nodes[ni]
        T = np.array(n.get("translation", [0, 0, 0]), np.float64)
        R = np.array(n.get("rotation", [0, 0, 0, 1]), np.float64)
        S = np.array(n.get("scale", [1, 1, 1]), np.float64)
        if (ni, "translation") in chan: T = sample(*chan[(ni, "translation")], t, "translation")
        if (ni, "rotation") in chan: R = sample(*chan[(ni, "rotation")], t, "rotation")
        if (ni, "scale") in chan: S = sample(*chan[(ni, "scale")], t, "scale")
        if "matrix" in n and not any((ni, p) in chan for p in ("translation", "rotation", "scale")):
            return np.array(n["matrix"], np.float64).reshape(4, 4).T
        M = quat_to_mat(R)
        M[:3, :3] *= S
        M[:3, 3] = T
        return M

    def globals_at(t):
        G = [None] * len(nodes)

        def g(i):
            if G[i] is None:
                L = local_matrix(i, t)
                G[i] = L if parent[i] < 0 else g(parent[i]) @ L
            return G[i]
        for i in range(len(nodes)):
            g(i)
        return G

    # ---- mallas del archivo ----
    prims = []   # por primitiva: dict con datos base
    for ni, n in enumerate(nodes):
        if "mesh" not in n:
            continue
        for p in js["meshes"][n["mesh"]]["primitives"]:
            at = p["attributes"]
            d = {
                "node": ni, "skin": n.get("skin"),
                "pos": accessor(js, binc, at["POSITION"]).astype(np.float64),
                "nor": accessor(js, binc, at["NORMAL"]).astype(np.float64) if "NORMAL" in at else None,
                "uv": accessor(js, binc, at["TEXCOORD_0"]).astype(np.float32) if "TEXCOORD_0" in at else None,
                "idx": accessor(js, binc, p["indices"]).reshape(-1).astype(np.uint32),
                "material": p.get("material", 0),
            }
            if only_skin is not None and n.get("skin") != only_skin:
                continue
            if d["skin"] is not None:
                d["joints"] = accessor(js, binc, at["JOINTS_0"]).astype(np.int64)
                d["weights"] = accessor(js, binc, at["WEIGHTS_0"]).astype(np.float64)
                d["weights"] /= np.maximum(d["weights"].sum(1, keepdims=True), 1e-9)
            prims.append(d)
    print("primitivas:", len(prims), " vertices:", sum(len(d["pos"]) for d in prims))

    skins = js["skins"]
    ibm = []
    for s in skins:
        m = accessor(js, binc, s["inverseBindMatrices"]).astype(np.float64).reshape(-1, 4, 4)
        ibm.append(np.transpose(m, (0, 2, 1)))   # glTF guarda por columnas
    hips = {}   # esqueleto -> nodo de la cadera (un .glb puede traer varios personajes)
    for si, sk in enumerate(skins):
        for j in sk["joints"]:
            if "hips" in (nodes[j].get("name") or "").lower():
                hips[si] = j
                break
    # ---- hornear ----
    frames = []   # frames[k][p] = (pos, nor)
    hips_xz = []
    for k in range(nframes):
        t = dur * k / nframes
        G = globals_at(t)
        hips_xz.append({si: G[j][[0, 2], 3].copy() for si, j in hips.items()})
        per = []
        for d in prims:
            if d["skin"] is None:
                M = G[d["node"]]
                P = (np.c_[d["pos"], np.ones(len(d["pos"]))] @ M.T)[:, :3]
                N = d["nor"] @ np.linalg.inv(M[:3, :3]).T if d["nor"] is not None else None
            else:
                s = skins[d["skin"]]
                J = np.stack([G[j] @ ibm[d["skin"]][i] for i, j in enumerate(s["joints"])])   # (nj,4,4)
                comb = np.einsum("nk,nkab->nab", d["weights"], J[d["joints"]])               # (nv,4,4)
                P = np.einsum("nab,nb->na", comb, np.c_[d["pos"], np.ones(len(d["pos"]))])[:, :3]
                N = np.einsum("nab,nb->na", comb[:, :3, :3], d["nor"]) if d["nor"] is not None else None
            if N is not None:
                N = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
            per.append([P, N])
        frames.append(per)

    if hips and not keep_root:
        for si in hips:
            hx = np.array([h[si] for h in hips_xz])
            drift = hx.max(0) - hx.min(0)
            print("esqueleto %d: desplazamiento de la cadera en el ciclo (x, z): %.3f, %.3f  -> se quita (camina en su lugar)" % ((si,) + tuple(drift)))
            mean = hx.mean(0)
            for k in range(nframes):
                off = hx[k] - mean
                for pi, d in enumerate(prims):
                    if d["skin"] == si:
                        frames[k][pi][0][:, 0] -= off[0]
                        frames[k][pi][0][:, 2] -= off[1]

    allp = np.concatenate([per[0] for fr in frames for per in fr])
    print("caja del personaje: min", allp.min(0).round(3), "max", allp.max(0).round(3))

    # ---- escribir .glb: una malla por cuadro ----
    blob = bytearray()
    bviews, accs = [], []

    def add_view(b, target=None):
        while len(blob) % 4:
            blob.append(0)
        v = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(b)}
        if target:
            v["target"] = target
        blob.extend(b)
        bviews.append(v)
        return len(bviews) - 1

    # imagenes originales (embebidas) con indices nuevos
    new_images = []
    for im in js.get("images", []):
        bv = js["bufferViews"][im["bufferView"]]
        raw = bytes(binc[bv.get("byteOffset", 0): bv.get("byteOffset", 0) + bv["byteLength"]])
        ni = dict(im)
        ni["bufferView"] = add_view(raw)
        new_images.append(ni)

    shared = []   # por primitiva: (acc_uv, acc_idx)
    for d in prims:
        a_uv = a_idx = None
        if d["uv"] is not None:
            accs.append({"bufferView": add_view(d["uv"].astype("<f4").tobytes(), 34962), "componentType": 5126, "count": len(d["uv"]), "type": "VEC2"})
            a_uv = len(accs) - 1
        accs.append({"bufferView": add_view(d["idx"].astype("<u4").tobytes(), 34963), "componentType": 5125, "count": int(d["idx"].size), "type": "SCALAR"})
        a_idx = len(accs) - 1
        shared.append((a_uv, a_idx))

    meshes, out_nodes = [], []
    for k in range(nframes):
        mprims = []
        for pi, d in enumerate(prims):
            P, N = frames[k][pi]
            accs.append({"bufferView": add_view(P.astype("<f4").tobytes(), 34962), "componentType": 5126, "count": len(P), "type": "VEC3",
                         "min": P.min(0).tolist(), "max": P.max(0).tolist()})
            ap = len(accs) - 1
            attrs = {"POSITION": ap}
            if N is not None:
                accs.append({"bufferView": add_view(N.astype("<f4").tobytes(), 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
                attrs["NORMAL"] = len(accs) - 1
            if shared[pi][0] is not None:
                attrs["TEXCOORD_0"] = shared[pi][0]
            mprims.append({"attributes": attrs, "indices": shared[pi][1], "material": d["material"]})
        meshes.append({"name": f"frame{k}", "primitives": mprims})
        out_nodes.append({"name": f"frame{k}", "mesh": k})

    while len(blob) % 4:
        blob.append(0)
    out = {
        "asset": {"version": "2.0", "generator": "bake_skinned_glb"},
        "scene": 0, "scenes": [{"nodes": list(range(nframes))}],
        "nodes": out_nodes, "meshes": meshes,
        "materials": js.get("materials", []), "textures": js.get("textures", []),
        "samplers": js.get("samplers", []), "images": new_images,
        "accessors": accs, "bufferViews": bviews, "buffers": [{"byteLength": len(blob)}],
    }
    if js.get("extensionsUsed"):
        out["extensionsUsed"] = js["extensionsUsed"]
    jj = json.dumps(out, separators=(",", ":")).encode()
    jj += b" " * ((4 - len(jj) % 4) % 4)
    total = 12 + 8 + len(jj) + 8 + len(blob)
    with open(dst, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(jj), 0x4E4F534A)); f.write(jj)
        f.write(struct.pack("<II", len(blob), 0x004E4942)); f.write(bytes(blob))
    print(f"escrito {dst}: {nframes} cuadros x {len(prims)} primitivas, {total/1048576:.1f} MB")


if __name__ == "__main__":
    main()
