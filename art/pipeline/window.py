# Display window: recessed bezel with plain smoked glass. The segments, mesh, filaments and glow are drawn live by the editor.
#   python window.py <plate in> <plate out> <window centre y> <window w> <window h>      (env S = picture scale)
import sys, os, numpy as np
from PIL import Image
src, out, cy = sys.argv[1], sys.argv[2], float(sys.argv[3]); WW, WH = float(sys.argv[4]), float(sys.argv[5])
S = int(os.environ.get('S', '3')); W = 480
Ww, Hh = int(WW * 12), int(WH * 12)
bg = np.zeros((Hh, Ww, 3)); bg[:] = (11, 13, 14)
yv = np.linspace(0, 1, Hh)[:, None]; bg += ((1 - yv) * 5)[..., None]               # glass a touch lighter at the top
# smoked-glass sheen: faint diagonal light band from the top-left, the same light as everywhere else
gx, gy = np.meshgrid(np.linspace(0, 1, Ww), np.linspace(0, 1, Hh))
sheen = np.clip(1 - (gx * 0.35 + gy * 0.9), 0, 1) ** 2.2 * 0.10
img = np.clip(bg + sheen[..., None] * 255 * np.array([1, 0.97, 0.98]), 0, 255).astype(np.uint8)
win = Image.fromarray(img).resize((int(round(WW * S)), int(round(WH * S))), Image.LANCZOS)
# the black bezel ring is rendered in Blender (scene.py); here only the glass is set into it, with a soft inner shadow
# from the ring (light from above: strongest under the top edge)
R = 3.0; K = 8
Wp, Hp = win.size
a = np.asarray(win).astype(np.float64)
yy, xx = np.mgrid[0:Hp, 0:Wp].astype(np.float64)
px, py = (xx + 0.5 - Wp / 2) / S, (yy + 0.5 - Hp / 2) / S
qx, qy = np.abs(px) - (WW / 2 - R), np.abs(py) - (WH / 2 - R)
sd = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - R      # < 0 inside the glass
dep = -sd
top = np.clip(1 - (py + WH / 2) / WH, 0, 1)                      # 1 at the top edge, 0 at the bottom
shade = np.exp(-dep / 1.6) * (0.40 + 0.60 * top) * 0.95 + np.exp(-dep / 0.35) * 0.30
a = a * (1 - np.clip(shade, 0, 0.95))[..., None]
alpha = np.clip(dep * S + 0.5, 0, 1)
base = Image.open(src).convert('RGBA'); b = np.asarray(base).astype(np.float64)
ox0 = int(round(W * S / 2 - Wp / 2)); oy0 = int(round(cy * S - Hp / 2))
reg = b[oy0:oy0 + Hp, ox0:ox0 + Wp, :3]
reg[:] = reg * (1 - alpha[..., None]) + a * alpha[..., None]
b[oy0:oy0 + Hp, ox0:ox0 + Wp, :3] = reg
Image.fromarray(np.clip(b, 0, 255).astype(np.uint8), 'RGBA').save(out)
