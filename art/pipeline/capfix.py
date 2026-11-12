# Re-shade the cap's outline for light from straight above: thin light line on top, equal mid-grey on both sides, dark lower edge.
import numpy as np, cv2
from PIL import Image
def fix_cap(src, dst):
    im = np.asarray(Image.open(src).convert('RGBA')).astype(np.float64)
    a = im[..., 3] / 255.0
    m = (a > 0.5).astype(np.uint8)
    d = cv2.distanceTransform(m, cv2.DIST_L2, 5)                 # px inside the silhouette
    g = cv2.GaussianBlur(a.astype(np.float32), (0, 0), 3.0)
    gy, gx = np.gradient(g); nrm = np.hypot(gx, gy) + 1e-9
    nx, ny = -gx / nrm, -gy / nrm                                   # outward normal (y down)
    ring = np.clip((4.2 - d) / 1.2, 0, 1) * (m > 0)                 # outermost ~3 px
    shade = 172 - 58 * np.clip(ny, -1, 1)                           # top light, sides mid, bottom dark
    rgb = im[..., :3]
    for c in range(3):
        rgb[..., c] = rgb[..., c] * (1 - ring) + shade * ring
    # the extra grey band on the right edge: pull it up to the face colour (both sides now match)
    band = np.clip((d - 3.5) / 2.0, 0, 1) * np.clip((14.0 - d) / 3.0, 0, 1) * np.clip((nx - 0.45) / 0.3, 0, 1) * (m > 0)
    for c in range(3):
        rgb[..., c] = rgb[..., c] + (np.maximum(250.0 - rgb[..., c], 0)) * band
    # stray dark specks just inside the top edge and corners (the old 'black flaw'): lift to the face colour
    zone = (d > 3.6) * (d < 10.0) * (ny < -0.12) * (m > 0)
    zw = np.clip((d - 3.6) / 0.8, 0, 1) * zone
    for c in range(3):
        rgb[..., c] = rgb[..., c] + (np.maximum(251.0 - rgb[..., c], 0)) * zw
    im[..., :3] = rgb
    Image.fromarray(np.clip(im, 0, 255).astype(np.uint8), 'RGBA').save(dst)
