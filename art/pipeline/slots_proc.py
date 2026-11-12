# Recessed slide-switch channels, lit from straight above (so both are identical and mirror-symmetric).
import numpy as np
from PIL import Image
def slot_sprite(S, a=3.2, half=38.0, TW=24, TH=96):
    K = 12; W_, H_ = TW * K, TH * K
    yy, xx = np.mgrid[0:H_, 0:W_].astype(np.float64); x = (xx + 0.5 - W_ / 2) / K; y = (yy + 0.5 - H_ / 2) / K
    dy = np.maximum(np.abs(y) - (half - a), 0); d = np.hypot(x, dy) - a            # <0 inside the opening
    # outward normal of the opening
    nx = x / np.maximum(np.hypot(x, dy), 1e-9); ny = np.where(np.abs(y) > half - a, np.sign(y) * dy / np.maximum(np.hypot(x, dy), 1e-9), 0.0)
    lit = -ny                                    # facing vector of a wall is -normal; light comes from above (0, -1): dot = -(-ny)... see below
    toward_light = ny * -1.0 * -1.0              # placeholder to keep the maths explicit
    # a wall at the bottom end faces up (towards the light): ny = +1 there (outward = down), so facing_up = ny
    facing_up = ny
    CH = 0.9                                      # chamfer width on the plate side
    floor = 30 + 26 * smooth01((y + half) / (2 * half))
    wall = 40 + 125 * np.clip(facing_up, 0, 1) - 22 * np.clip(-facing_up, 0, 1)
    wall_w = np.clip((d + 0.9) / 0.9, 0, 1)       # 0 deep in the floor, 1 at the opening edge
    inner = floor * (1 - wall_w) + wall * wall_w
    cham = 238 + 14 * facing_up - 34 * np.clip(-facing_up, 0, 1)
    v = np.where(d < 0, inner, cham)
    alpha = np.clip((CH - d) * K + 0.5, 0, 1)
    rgba = np.stack([v, v, v * 1.01, alpha * 255], -1)
    pm = rgba / 255.0; pm[..., :3] *= pm[..., 3:4]
    sz = (TW * S, TH * S)
    ch = [np.asarray(Image.fromarray(pm[..., i].astype(np.float32), mode='F').resize(sz, Image.LANCZOS)) for i in range(4)]
    o = np.stack(ch, -1).clip(0, 1); al = o[..., 3:4]
    rgb = np.where(al > 1e-6, o[..., :3] / np.maximum(al, 1e-6), 0).clip(0, 1)
    return Image.fromarray((np.concatenate([rgb, al], -1) * 255 + 0.5).astype(np.uint8), 'RGBA')
def smooth01(a): a = np.clip(a, 0, 1); return a * a * (3 - 2 * a)
def paint_slots(plate, S, cy=190.0, xs=(96.0, 384.0), shift=40.0, TW=24, TH=96):
    sp = slot_sprite(S, TW=TW, TH=TH)
    for x in xs:
        x0 = int(round((x - TW / 2) * S)); y0 = int(round((cy - TH / 2) * S))
        sx = int(round((x + (shift if x < 240 else -shift) - TW / 2) * S))        # a clean strip of plate beside it
        plate.paste(plate.crop((sx, y0, sx + TW * S, y0 + TH * S)), (x0, y0))
        plate.alpha_composite(sp, (x0, y0))
    return plate
