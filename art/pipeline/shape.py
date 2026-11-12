# Cuts the faceplate picture to its outline: end cheeks with a large radius at the bottom corners, a small one at the top.
#   python shape.py <plate in> <plate out> [scale]
import sys, numpy as np
from PIL import Image
S = int(sys.argv[3]) if len(sys.argv) > 3 else 3
im = Image.open(sys.argv[1]).convert('RGBA'); W, H = im.size
assert (W, H) == (480 * S, 392 * S)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64); px, py = xx + 0.5, yy + 0.5
cx, cy = W / 2, H / 2; hx, hy = W / 2, H / 2
r = np.where(py < cy, 5.0 * S, 14.0 * S)
qx = np.abs(px - cx) - hx + r; qy = np.abs(py - cy) - hy + r
sd = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
a = np.asarray(im).copy(); a[..., 3] = (np.clip(0.5 - sd, 0, 1) * 255).astype(np.uint8)
Image.fromarray(a, 'RGBA').save(sys.argv[2], optimize=True)
