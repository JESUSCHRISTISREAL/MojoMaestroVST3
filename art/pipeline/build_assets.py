# Turns the Blender renders (art/out, stored at 4x) into the pictures in Assets/.
#   python build_assets.py <scale> <renders folder> <output folder>
#   e.g. python build_assets.py 3 ../out ../../Assets
# Needs numpy, Pillow and opencv-python.
import sys, os, shutil, tempfile, subprocess, numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
S = int(sys.argv[1]); U = os.path.join(os.path.abspath(sys.argv[2]), ''); out = os.path.abspath(sys.argv[3]); R = 4
os.makedirs(out, exist_ok=True)
work = tempfile.mkdtemp()          # intermediate pictures
PY = sys.executable
env = dict(os.environ, S=str(S), NOERASE='1')
def down(img, size):
    a = np.asarray(img.convert('RGBA'), dtype=np.float64) / 255.0
    pm = a.copy(); pm[..., :3] *= a[..., 3:4]
    ims = [Image.fromarray(pm[..., i].astype(np.float32), mode='F').resize(size, Image.LANCZOS) for i in range(4)]
    o = np.stack([np.asarray(i) for i in ims], -1).clip(0, 1); al = o[..., 3:4]
    rgb = np.where(al > 1e-6, o[..., :3] / np.maximum(al, 1e-6), 0).clip(0, 1)
    return Image.fromarray((np.concatenate([rgb, al], -1) * 255 + 0.5).astype(np.uint8), 'RGBA')
def crop(img, cx, cy, hw, hh): return img.crop((int((cx - hw) * R), int((cy - hh) * R), int((cx + hw) * R), int((cy + hh) * R)))
# plate
from slots_proc import paint_slots
pl = Image.open(U + 'plate.png').convert('RGBA').resize((480 * S, 392 * S), Image.LANCZOS)
paint_slots(pl, S)
pl.convert('RGB').save(work + '/p0.png')
shutil.copy(work + '/p0.png', work + '/p1.png')          # the render has no screws
subprocess.check_call([PY, os.path.join(HERE, 'window.py'), work + '/p1.png', work + '/p2.png', '283', '212', '28'], env=env)
subprocess.check_call([PY, os.path.join(HERE, 'shape.py'), work + '/p2.png', out + '/plate_3x.png', str(S)])   # cut to the outline of the walnut end cheeks
import cv2
def hairline(img, width):
    # a thin darker line round the whole edge so the circle always reads on white; a little stronger at the bottom
    a_ = np.asarray(img).astype(np.float64); al = a_[..., 3] / 255.0
    d = cv2.distanceTransform((al > 0.5).astype(np.uint8), cv2.DIST_L2, 3)
    w = np.clip(1 - d / (width * S), 0, 1) * (al > 0.5)
    yy = np.linspace(0, 1, a_.shape[0])[:, None]
    k = 0.07 + 0.10 * yy
    a_[..., :3] *= (1 - w * k)[..., None]
    return Image.fromarray(np.clip(a_, 0, 255).astype(np.uint8), 'RGBA')
# knobs: Blender renders (scene.py modes knob and small), tile 120 and 40 logical px
for n, t in (('knob_big', 120), ('knob_small', 40)):
    hairline(down(Image.open(U + n + '.png'), (t * S, t * S)), 0.9).save(out + f'/{n}_3x.png', optimize=True)
for n, t in (('ptr_big', 96), ('ptr_small', 32)):
    down(Image.open(U + n + '.png'), (t * S, t * S)).save(out + f'/{n}_3x.png', optimize=True)
# cap
from capfix import fix_cap
fix_cap(U + 'cap.png', work + '/cap4_fixed.png')
subprocess.check_call([PY, os.path.join(HERE, 'cap_sprite.py'), work + '/p0.png', work + '/cap4_fixed.png', work + '/capdemo.png'], env=env)
shutil.move(work + '/capdemo_sprite.png', out + '/cap_3x.png')
for n in ('plate', 'knob_big', 'knob_small', 'ptr_big', 'ptr_small', 'cap'): print(n, Image.open(out + f'/{n}_3x.png').size)
shutil.rmtree(work, ignore_errors=True)
