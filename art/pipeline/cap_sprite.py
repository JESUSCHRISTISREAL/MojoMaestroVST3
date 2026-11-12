import sys, numpy as np
from PIL import Image, ImageFilter
src, capf, out = sys.argv[1], sys.argv[2], sys.argv[3]
import os; S = int(os.environ.get('S','2')); K = 4
cap = Image.open(capf).convert('RGBA'); bb = cap.getbbox(); cx, cy = (bb[0] + bb[2]) // 2, (bb[1] + bb[3]) // 2
TW, TH = 40, 44                                          # logical tile
tile = cap.crop((cx - TW * K // 2, cy - TH * K // 2, cx + TW * K // 2, cy + TH * K // 2))
al = np.asarray(tile)[..., 3].astype(np.float64) / 255
def sh(blur, dx, dy, k):
    m = Image.fromarray((al * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur * K))
    return np.roll(np.roll(np.asarray(m).astype(np.float64) / 255, int(dy * K), 0), int(dx * K), 1) * k
sha = np.clip(1 - (1 - sh(1.1, 0.0, 2.0, 0.30)) * (1 - sh(0.4, 0.0, 0.8, 0.38)), 0, 1)
full = np.zeros((tile.height, tile.width, 4)); full[..., :3] = (30, 26, 34); full[..., 3] = sha * 255
t = np.asarray(tile).astype(np.float64); a_ = t[..., 3:4] / 255
full[..., :3] = full[..., :3] * (1 - a_) + t[..., :3] * a_
full[..., 3] = np.maximum(full[..., 3], t[..., 3])
pm = full / 255.0; pm[..., :3] *= pm[..., 3:4]
sz = (TW * S, TH * S)
ch = [np.asarray(Image.fromarray(pm[..., i].astype(np.float32), mode='F').resize(sz, Image.LANCZOS)) for i in range(4)]
o = np.stack(ch, -1).clip(0, 1); al2 = o[..., 3:4]
rgb = np.where(al2 > 1e-6, o[..., :3] / np.maximum(al2, 1e-6), 0).clip(0, 1)
spr = Image.fromarray((np.concatenate([rgb, al2], -1) * 255 + 0.5).astype(np.uint8), 'RGBA')
spr.save(out.replace('.png','_sprite.png'))
img = Image.open(src).convert('RGBA')
for x in (96, 384):
    img.alpha_composite(spr, (int(round(x * S - sz[0] / 2)), int(round(158 * S - sz[1] / 2))))
img.save(out)
