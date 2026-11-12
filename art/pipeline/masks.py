import json, math, random
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from layout import *
SS = 4                      # supersample over S
K = S * SS                  # px per logical px in the work canvas
cw, ch = W*K, H*K
import os
HERE = os.path.dirname(os.path.abspath(__file__)); ART = os.path.join(HERE, '..')
hand = os.path.join(ART, 'fonts', 'caveat-700.ttf'); serif = os.path.join(ART, 'fonts', 'libre-baskerville-400.ttf')   # not in the repo, see art/README.md
ink = Image.new('L', (cw, ch), 0); d = ImageDraw.Draw(ink)
report = []

def text_bbox(s, font, tracking):
    # ink bbox of tracked text (tracking in px at work scale)
    x = 0; boxes = []
    for c in s:
        b = font.getbbox(c, anchor='ls'); adv = font.getlength(c)
        boxes.append((x + b[0], b[1], x + b[2], b[3])); x += adv + tracking
    return (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))

def draw_text(s, fontfile, size, tracking, cx=None, cy=None, left=None, right=None, name=''):
    font = ImageFont.truetype(fontfile, int(round(size * K)))
    tr = tracking * K
    bx0, by0, bx1, by1 = text_bbox(s, font, tr)
    w, h = bx1 - bx0, by1 - by0
    cb = font.getbbox('H', anchor='ls'); ctop, cbot = cb[1], cb[3]
    if cx is not None: x0 = cx*K - w/2
    elif left is not None: x0 = left*K
    else: x0 = right*K - w
    oy = cy*K - (ctop + cbot)/2.0     # baseline so the capital-H box is centred on cy
    ox = x0 - bx0
    y0 = oy + by0
    x = ox
    for c in s:
        d.text((x, oy), c, font=font, fill=255, anchor='ls'); x += font.getlength(c) + tr
    report.append((name or s, round((x0+w/2)/K,2), round((y0+h/2)/K,2), round(w/K,2), round(h/K,2), round(x0/K,2), round((x0+w)/K,2), round(y0/K,2), round((y0+h)/K,2)))

draw_text('MOJO MAESTRO', hand, 34, 2.0, cx=CX, cy=40.0, name='TITLE')
draw_text('CIRCUIT BY ETHAN WINER', serif, 7.6, 1.3, cx=CX, cy=68.0, name='SUB')
draw_text('AMOUNT', hand, 15, 1.4, cx=CX, cy=106.8, name='AMOUNT')
for nm, ang in (('MIN', -135.0), ('MAX', 135.0)):
    a = math.radians(ang); r = 88.0
    draw_text(nm, hand, 13, 1.0, cx=KNOB['cx'] + math.sin(a)*r, cy=KNOB['cy'] - math.cos(a)*r, name=nm)
# side labels, aligned to mirrored edges, ink-centred on each cap travel position
draw_text('GLUE',   hand, 13, 1.0, right=SIDE_EDGE, cy=SW_Y[0], name='GLUE')
draw_text('BYPASS', hand, 13, 1.0, right=SIDE_EDGE, cy=SW_Y[1], name='BYPASS')
draw_text('MOJO',   hand, 13, 1.0, right=SIDE_EDGE, cy=SW_Y[2], name='MOJO')
draw_text('EVEN',   hand, 13, 1.0, left=W-SIDE_EDGE, cy=SW_Y[0], name='EVEN')
draw_text('ODD',    hand, 13, 1.0, left=W-SIDE_EDGE, cy=SW_Y[2], name='ODD')
for x, nm in zip(SMALL_X, ('REFERENCE', 'TRIM', 'OVERSAMPLE', 'MODEL', 'DC BLOCK')):
    draw_text(nm, hand, 11, 0.9, cx=x, cy=SMALL_LABEL_Y, name=nm)

# bottom line: sentence + outlined heart, centred together on the centre line
def heart_pts(n=360):
    pts = []
    for i in range(n + 1):
        t = 2 * math.pi * i / n
        pts.append((16 * math.sin(t) ** 3, -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))))
    return pts
def draw_group(s, fontfile, size, tracking, cy, gap, heart_h, stroke, name):
    font = ImageFont.truetype(fontfile, int(round(size * K))); tr = tracking * K
    bx0, by0, bx1, by1 = text_bbox(s, font, tr); tw = bx1 - bx0
    hp = heart_pts(); xs = [p[0] for p in hp]; ys = [p[1] for p in hp]
    sc = heart_h * K / (max(ys) - min(ys)); hw = (max(xs) - min(xs)) * sc
    gw = tw + gap * K + hw
    x0 = CX * K - gw / 2
    cb = font.getbbox('H', anchor='ls'); oy = cy * K - (cb[1] + cb[3]) / 2.0
    x = x0 - bx0
    for c in s:
        d.text((x, oy), c, font=font, fill=255, anchor='ls'); x += font.getlength(c) + tr
    hx0 = x0 + tw + gap * K
    ymid = (max(ys) + min(ys)) / 2
    pts = [(hx0 + (px - min(xs)) * sc, cy * K + (py - ymid) * sc) for px, py in hp]
    d.line(pts, fill=255, width=int(round(stroke * K)), joint='curve')
    report.append((name, round((x0 + gw / 2) / K, 2), round(cy, 2), round(gw / K, 2), round(heart_h, 2), round(x0 / K, 2), round((x0 + gw) / K, 2), round(cy - heart_h / 2, 2), round(cy + heart_h / 2, 2)))
# (the GLORY line now lives on the pink display)

# tick marks: radial lines, round caps
def tick(cx, cy, a_deg, r0, r1, wpx):
    a = math.radians(a_deg); dx, dy = math.sin(a), -math.cos(a)
    p0 = ((cx+dx*r0)*K, (cy+dy*r0)*K); p1 = ((cx+dx*r1)*K, (cy+dy*r1)*K)
    d.line([p0, p1], fill=255, width=int(round(wpx*K)))
    for p in (p0, p1): d.ellipse([p[0]-wpx*K/2, p[1]-wpx*K/2, p[0]+wpx*K/2, p[1]+wpx*K/2], fill=255)
for i in range(11):
    tick(KNOB['cx'], KNOB['cy'], -135 + 27*i, RING[0], RING[1] + (2 if i in (0,5,10) else 0), 1.4)
for x in SMALL_X:
    for i in range(7):
        tick(x, SMALL_Y, -135 + 45*i, SMALL_R+4, SMALL_R+7, 1.0)

# silkscreen character: mesh-edge wobble + ink bleed, deterministic
rng = np.random.default_rng(7)
a = np.asarray(ink, dtype=np.float32)/255.0
noise = rng.random((ch//8+2, cw//8+2)).astype(np.float32)
noise = np.asarray(Image.fromarray((noise*255).astype(np.uint8)).resize((cw, ch), Image.BICUBIC), dtype=np.float32)/255.0
blur = np.asarray(ink.filter(ImageFilter.GaussianBlur(0.55*K*0.5)), dtype=np.float32)/255.0
edge = np.clip((blur - (0.42 + 0.16*noise)) * 6.0 + 0.5, 0, 1)
fine = rng.random((ch, cw)).astype(np.float32)
edge *= (1.0 - 0.10*(fine > 0.985))                         # tiny pinholes
density = 0.93 + 0.07*noise                                 # uneven ink laydown
m = Image.fromarray((edge*density*255).astype(np.uint8)).resize((W*4, H*4), Image.LANCZOS)
m.save(os.path.join(ART, 'silk.png'))
hm = m.filter(ImageFilter.GaussianBlur(1.1)); hm.save(os.path.join(ART, 'silk_height.png'))
for r in report: print('%-12s cx=%7.2f cy=%7.2f  w=%6.2f h=%6.2f  x[%.2f..%.2f] y[%.2f..%.2f]' % r)
print('plate', m.size)
