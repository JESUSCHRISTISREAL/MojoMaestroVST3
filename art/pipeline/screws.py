# Re-draw every screw (4 faceplate corners + 2 on the nameplate) from ONE procedural sprite lit from the top-left, like everything else.
import sys, numpy as np
from PIL import Image, ImageFilter
src, out, H, cy, PW = sys.argv[1], sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
import os; S = int(os.environ.get('S','2')); W = 480; K = 16; T = 20          # tile 20 logical px, drawn at 16x
im = Image.open(src).convert('RGBA'); a = np.asarray(im).astype(np.float64)
import cv2
def erase_all(pts, r=7.6):
    global a
    yy, xx = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    m = np.zeros(a.shape[:2], np.uint8)
    for cx, cy_ in pts:
        m[np.hypot(xx + 0.5 - cx * S, yy + 0.5 - cy_ * S) / S <= r] = 255
    rgb = np.clip(a[..., :3], 0, 255).astype(np.uint8)
    a[..., :3] = cv2.inpaint(rgb, m, 6, cv2.INPAINT_TELEA)
# sprites
N = T * K
yy, xx = np.mgrid[0:N, 0:N].astype(np.float64); x = (xx + 0.5 - N / 2) / K; y = (yy + 0.5 - N / 2) / K
rr = np.hypot(x, y)
def blur(m, s_): return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(s_ * K))).astype(np.float64) / 255
def shift(m, dx, dy): return np.roll(np.roll(m, int(dy * K), 0), int(dx * K), 1)
def make(kind):
    R = 4.3 if kind == 'rivet' else 4.2
    head = np.clip((R - rr) * K + 0.5, 0, 1)
    seat = blur(np.clip((R + 0.9 - rr) * K, 0, 1), 0.45) * 0.14
    cast = shift(blur(head, 0.5), 0.0, 0.9) * 0.30
    sh = np.clip(1 - (1 - seat) * (1 - cast), 0, 1)
    z = np.sqrt(np.clip(1 - (rr / R) ** 2, 0, 1)); nx, ny = x / R, y / R
    if kind == 'rivet':
        nz = 0.80 + 0.20 * z; k1, k2, k3 = 0.66, 0.22, (0.06, 0.22)
    else:                                   # Phillips pan head: rounder dome, satin steel, softer highlight
        nz = 0.45 + 0.55 * z; k1, k2, k3 = 0.58, 0.30, (0.035, 0.40)
    nn = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2); nx, ny, nz = nx / nn, ny / nn, nz / nn
    L = np.array([0.0, -0.62, 0.78]); L /= np.linalg.norm(L)
    dif = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
    env = 0.5 - 0.5 * ny
    refl = k1 + k2 * env ** 1.4 + 0.20 * dif
    bounce = np.exp(-((ny - 0.82) / 0.16) ** 2) * 0.18 * (rr / R > 0.55)
    spec = np.exp(-(((x / R) ** 2 + (y / R + 0.42) ** 2) / k3[0])) * k3[1]
    rim = np.clip((rr / R - 0.86) / 0.14, 0, 1) * 0.10 * (0.5 + 0.5 * dif)
    v = np.clip(refl + bounce + spec + rim, 0, 1) * 235
    if kind == 'rivet':
        flange = np.clip((rr / R - 0.84) / 0.05, 0, 1) * np.clip((1.0 - rr / R) / 0.04, 0, 1)
        v = v * (1 - 0.14 * flange * (0.6 + 0.4 * (ny > 0)))
        v = v + np.clip((rr / R - 0.92) / 0.08, 0, 1) * (ny < 0) * 0.10 * 255 * 0.5
        hr = 1.15; hole = np.clip((hr - rr) * K + 0.5, 0, 1)
        v = v * (1 - hole) + v * np.where(y < 0, 0.30, 0.70) * hole
        ring = np.clip(1 - np.abs(rr - (hr + 0.18)) / 0.18, 0, 1) * 0.10
        v = v * (1 - ring * (y > 0)) + ring * (y < 0) * 40
    else:                                   # Phillips recess: upright cross (+), tapered arms, cone at the centre
        arm_w, arm_l = 0.62, 2.55
        tapw = arm_w * (1 - 0.25 * np.clip(np.maximum(np.abs(x), np.abs(y)) / arm_l, 0, 1))
        hor = (np.abs(y) < tapw / 2) & (np.abs(x) < arm_l); ver = (np.abs(x) < tapw / 2) & (np.abs(y) < arm_l)
        cross = hor | ver
        cen = np.clip((1.0 - rr) * K * 0.5 + 0.5, 0, 1)
        cross = np.clip(cross.astype(float) + cen * 0.6, 0, 1)
        cross = np.asarray(Image.fromarray((cross * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.05 * K))).astype(np.float64) / 255
        wall = 0.42   # upper walls in shade, lower walls catch light
        v = v * (1 - cross) + v * wall * cross
        lipx = np.roll(cross, int(0.28 * K), 0)
        v = v + np.clip(lipx - cross, 0, 1) * 26                                       # thin lit lower edge of the recess
    col = np.stack([v, v, v * 1.012], -1)
    spr = np.zeros((N, N, 4)); spr[..., 3] = sh * 255; spr[..., :3] = (28, 24, 32)
    hm = head[..., None]
    spr[..., :3] = spr[..., :3] * (1 - hm) + col * hm
    spr[..., 3] = np.maximum(spr[..., 3], head * 255)
    return spr
def down(t):
    pm = t / 255.0; pm[..., :3] *= pm[..., 3:4]
    sz = (T * S, T * S)
    c = [np.asarray(Image.fromarray(pm[..., i].astype(np.float32), mode='F').resize(sz, Image.LANCZOS)) for i in range(4)]
    o = np.stack(c, -1).clip(0, 1); al = o[..., 3:4]
    rgb = np.where(al > 1e-6, o[..., :3] / np.maximum(al, 1e-6), 0).clip(0, 1)
    return np.concatenate([rgb, al], -1)
sp_r = down(make("rivet")); sp_p = down(make("phillips"))
pos = [(14, 14), (466, 14), (14, H - 14), (466, H - 14)]
import os
if not os.environ.get('NOERASE'): erase_all(pos)
img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')
sprim_r = Image.fromarray((sp_r * 255 + 0.5).astype(np.uint8), 'RGBA'); sprim_p = Image.fromarray((sp_p * 255 + 0.5).astype(np.uint8), 'RGBA')
for i_, (cx, cyy) in enumerate(pos):
    sprim = sprim_p
    img.alpha_composite(sprim, (int(round(cx * S - T * S / 2)), int(round(cyy * S - T * S / 2))))
img.save(out)
b = os.path.dirname(os.path.abspath(out)) + os.sep
c = Image.new('RGBA', (4 * 240, 240), 'white')
for i, (cx, cyy) in enumerate(pos):
    c.paste(img.crop((int((cx - 10) * S), int((cyy - 10) * S), int((cx + 10) * S), int((cyy + 10) * S))).resize((240, 240), Image.LANCZOS), (i * 240, 0))
c.save(b + 'phillips_cmp.png')
