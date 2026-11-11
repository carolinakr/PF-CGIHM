"""
Analiza la geometria de un .glb de una sola malla: componentes conectados y siluetas con color de textura.

Uso:  python tools/glb_mesh_probe.py Models/Animaciones/Taiko/manoder.glb <carpeta_salida>

Sirve para decidir si un modelo "fusionado" (p. ej. generado con IA) se puede separar en partes
por piezas sueltas (componentes conectados) o si hay que cortarlo por planos.
"""
import io
import json
import struct
import sys
import numpy as np
from PIL import Image
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components


def load(path):
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


def accessor(js, binc, ai, ncomp, dtype):
    a = js["accessors"][ai]
    v = js["bufferViews"][a["bufferView"]]
    o = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = v.get("byteStride")
    itemsize = np.dtype(dtype).itemsize * ncomp
    if stride and stride != itemsize:
        raw = np.frombuffer(binc, dtype=np.uint8, count=a["count"] * stride, offset=o).reshape(a["count"], stride)
        return np.frombuffer(raw[:, :itemsize].tobytes(), dtype=dtype).reshape(a["count"], ncomp)
    return np.frombuffer(binc, dtype=dtype, count=a["count"] * ncomp, offset=o).reshape(a["count"], ncomp)


path, out = sys.argv[1], sys.argv[2]
js, binc = load(path)
prim = js["meshes"][0]["primitives"][0]
pos = accessor(js, binc, prim["attributes"]["POSITION"], 3, np.float32).astype(np.float64)
uv = accessor(js, binc, prim["attributes"]["TEXCOORD_0"], 2, np.float32)
ia = js["accessors"][prim["indices"]]
idt = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32}[ia["componentType"]]
idx = accessor(js, binc, prim["indices"], 1, idt).reshape(-1, 3).astype(np.int64)
print("vertices", len(pos), "triangles", len(idx))
print("bbox min", pos.min(0).round(3), "max", pos.max(0).round(3))

# soldar vertices por posicion (los .glb duplican vertices por UV/normal)
key = np.round(pos / 1e-4).astype(np.int64)
_, inv = np.unique(key, axis=0, return_inverse=True)
inv = inv.reshape(-1)
tri_w = inv[idx]
n = inv.max() + 1
rows = np.concatenate([tri_w[:, 0], tri_w[:, 1], tri_w[:, 2]])
cols = np.concatenate([tri_w[:, 1], tri_w[:, 2], tri_w[:, 0]])
g = coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n))
nc, lab = connected_components(g, directed=False)
tri_comp = lab[tri_w[:, 0]]
sizes = np.bincount(tri_comp, minlength=nc)
order = np.argsort(-sizes)
print("componentes conectados:", nc)
for k in order[:15]:
    sel = tri_comp == k
    pts = pos[idx[sel]].reshape(-1, 3)
    print(f"  comp {k}: {sizes[k]:7d} tris  x[{pts[:,0].min():.2f},{pts[:,0].max():.2f}]  y[{pts[:,1].min():.2f},{pts[:,1].max():.2f}]  z[{pts[:,2].min():.2f},{pts[:,2].max():.2f}]")

# colores por vertice desde la textura
img = None
if js.get("images"):
    bv = js["bufferViews"][js["images"][0]["bufferView"]]
    raw = binc[bv.get("byteOffset", 0): bv.get("byteOffset", 0) + bv["byteLength"]]
    img = np.asarray(Image.open(io.BytesIO(raw)).convert("RGB"))
cen = pos[idx].mean(1)
if img is not None:
    uvc = uv[idx].mean(1)
    px = np.clip((uvc[:, 0] % 1.0) * (img.shape[1] - 1), 0, img.shape[1] - 1).astype(int)
    py = np.clip((uvc[:, 1] % 1.0) * (img.shape[0] - 1), 0, img.shape[0] - 1).astype(int)
    col = img[py, px]
else:
    col = np.full((len(idx), 3), 180, np.uint8)


def render(u, v, depth, name, S=900):
    """Proyeccion ortografica de los centroides de triangulos, el mas cercano gana (z-buffer simple)."""
    lo = np.array([cen[:, u].min(), cen[:, v].min()])
    hi = np.array([cen[:, u].max(), cen[:, v].max()])
    sc = (S - 20) / max(hi - lo)
    X = ((cen[:, u] - lo[0]) * sc + 10).astype(int)
    Y = (S - ((cen[:, v] - lo[1]) * sc + 10)).astype(int)
    order = np.argsort(depth)  # lejos primero
    im = np.zeros((S, S, 3), np.uint8)
    im[Y[order], X[order]] = col[order]
    for _ in range(2):  # rellenar huecos de 1 px
        m = (im.sum(2) == 0)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.roll(np.roll(im, dy, 0), dx, 1)
            im[m] = sh[m]
    Image.fromarray(im).save(f"{out}/{name}.png")


render(0, 1, cen[:, 2], "frente")         # mira desde +Z
render(2, 1, -cen[:, 0], "lado")           # mira desde +X
render(0, 2, cen[:, 1], "planta")          # vista desde arriba
