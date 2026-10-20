"""
Convierte un HDRI equirectangular (.exr) en las 6 caras del skybox del motor.

Uso (desde la carpeta ProyectoFinal/ProyectoFinal):
    python tools/hdri_to_cubemap.py Textures/HDRI/moonless_golf_4k.exr Textures/Skybox [tamano] [exposicion]

Requiere: pip install OpenEXR numpy pillow

Notas de convencion:
 - El centro de la imagen equirectangular queda mirando hacia -Z (la calle).
 - skybox.vert hace TexCoords = -pos, asi que el cubemap guarda sky(-dir); aqui se compensa.
 - Nombres de cara (orden GL +X -X -Y +Y +Z -Z): rt lf dn up bk ft.
"""
import sys
import numpy as np
import OpenEXR
from PIL import Image

src = sys.argv[1]
out_dir = sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
exposure = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0

hdr = OpenEXR.File(src).channels()["RGB"].pixels.astype(np.float32)
H, W, _ = hdr.shape
hdr = np.nan_to_num(hdr, nan=0.0, posinf=0.0, neginf=0.0)


def sample(direction):
    """Muestreo bilineal del equirectangular. direction: (...,3) unitario."""
    x, y, z = direction[..., 0], direction[..., 1], direction[..., 2]
    u = 0.5 + np.arctan2(x, -z) / (2.0 * np.pi)
    v = 0.5 - np.arcsin(np.clip(y, -1.0, 1.0)) / np.pi
    fx = u * W - 0.5
    fy = np.clip(v * H - 0.5, 0, H - 1)
    x0 = np.floor(fx).astype(int)
    y0 = np.floor(fy).astype(int)
    tx = (fx - x0)[..., None]
    ty = (fy - y0)[..., None]
    x0m, x1m = x0 % W, (x0 + 1) % W
    y1 = np.clip(y0 + 1, 0, H - 1)
    top = hdr[y0, x0m] * (1 - tx) + hdr[y0, x1m] * tx
    bot = hdr[y1, x0m] * (1 - tx) + hdr[y1, x1m] * tx
    return top * (1 - ty) + bot * ty


def tonemap(c):
    """ACES (Narkowicz) + sRGB; los puntos de luz de 1e4+ se recortan a blanco."""
    c = c * exposure
    c = np.clip((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1 / 2.4) - 0.055)


# (a, b) = coordenadas de cara en [-1,1] segun la especificacion de cubemap de GL
a, b = np.meshgrid((np.arange(size) + 0.5) / size * 2 - 1, (np.arange(size) + 0.5) / size * 2 - 1)
one = np.ones_like(a)
faces = {
    "rt": np.stack([one, -b, -a], -1),    # +X
    "lf": np.stack([-one, -b, a], -1),    # -X
    "up": np.stack([a, one, b], -1),      # +Y
    "dn": np.stack([a, -one, -b], -1),    # -Y
    "bk": np.stack([a, -b, one], -1),     # +Z
    "ft": np.stack([-a, -b, -one], -1),   # -Z
}

rng = np.random.default_rng(1)
for name, d in faces.items():
    d = d / np.linalg.norm(d, axis=-1, keepdims=True)
    img = tonemap(sample(-d))  # -d: compensa TexCoords = -pos del shader
    img = img + (rng.random(img.shape) - 0.5) / 255.0  # dither: evita bandas en cielo oscuro
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(f"{out_dir}/{name}.png")
    print("ok", name)
