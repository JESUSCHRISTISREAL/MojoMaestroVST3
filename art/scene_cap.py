# Blender 5.x headless:  blender -b -P scene.py -- <art_dir> <mode> [samples]
# modes: plate | knob | small | cap | ptr_big | ptr_small | test
import bpy, bmesh, json, math, os, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
ART, MODE = argv[0], argv[1]
SAMPLES = int(argv[2]) if len(argv) > 2 else 128
L = json.load(open(os.path.join(ART, 'layout.json')))
W, H, S = L['W'], L['H'], L['RS']
OUT = os.path.join(ART, 'out'); os.makedirs(OUT, exist_ok=True)

def wx(px): return px - W / 2.0
def wy(py): return H / 2.0 - py

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.device = 'CPU'
sc.cycles.samples = SAMPLES; sc.cycles.use_adaptive_sampling = False
sc.cycles.use_denoising = False
sc.cycles.max_bounces = 8
sc.render.resolution_x = W * S; sc.render.resolution_y = H * S; sc.render.resolution_percentage = 100
sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
sc.view_settings.exposure = 0.06
sc.view_settings.view_transform = 'Standard'; sc.view_settings.look = 'None'
sc.render.filter_size = 0.8

# ---------------------------------------------------------------- helpers
def new_mat(name):
    m = bpy.data.materials.new(name); m.use_nodes = True
    m.node_tree.nodes.clear(); return m

def principled(nt):
    return nt.nodes.new('ShaderNodeBsdfPrincipled')

def set_in(node, name, val):
    node.inputs[name].default_value = val

def aluminium(name, rough=0.36, aniso=0.75, bands=None):
    m = new_mat(name); nt = m.node_tree
    out = nt.nodes.new('ShaderNodeOutputMaterial'); b = principled(nt)
    set_in(b, 'Base Color', (0.91, 0.92, 0.93, 1)); set_in(b, 'Metallic', 1.0)
    set_in(b, 'Roughness', rough); set_in(b, 'Anisotropic', aniso)
    tan = nt.nodes.new('ShaderNodeTangent'); tan.direction_type = 'RADIAL'; tan.axis = 'Z'
    nt.links.new(tan.outputs['Tangent'], b.inputs['Tangent'])
    # satin micro-texture: fine concentric turning marks (or bands along Y for the caps)
    wave = nt.nodes.new('ShaderNodeTexWave')
    wave.wave_type = 'RINGS' if bands is None else 'BANDS'
    if bands is not None: wave.bands_direction = 'Y'
    wave.rings_direction = 'Z'; wave.wave_profile = 'SIN'
    wave.inputs['Scale'].default_value = 9.0 if bands is None else bands
    wave.inputs['Distortion'].default_value = 0.6; wave.inputs['Detail'].default_value = 3.0
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nt.links.new(tc.outputs['Object'], wave.inputs['Vector'])
    bump = nt.nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.18 if bands is None else 0.22
    bump.inputs['Distance'].default_value = 0.05 if bands is None else 0.25
    nt.links.new(wave.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs['Normal'], b.inputs['Normal'])
    nt.links.new(b.outputs['BSDF'], out.inputs['Surface']); return m

def revolve(name, profile, segs=160):
    me = bpy.data.meshes.new(name); bm = bmesh.new()
    rings = []
    for (r, z) in profile:
        rings.append([bm.verts.new((r * math.cos(2 * math.pi * i / segs), r * math.sin(2 * math.pi * i / segs), z)) for i in range(segs)])
    for a, b in zip(rings[:-1], rings[1:]):
        for i in range(segs):
            j = (i + 1) % segs
            try: bm.faces.new((a[i], a[j], b[j], b[i]))
            except ValueError: pass
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); sc.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40))
    return ob

def rounded_pts(pts, n=5):
    return pts

def knob_profile(R, Ht, taper, groove_r):
    # one smooth turned cylinder: straight side with a hint of draft, large rounded top edge, flat top
    rt = min(3.6, Ht * 0.3)
    side_top = Ht - rt
    p = [(R, 0.0), (R + 0.0, 0.5), (R - 0.15, 1.2)]
    p += [(R - taper * 0.2 * (z / side_top), z) for z in [i * side_top / 6 for i in range(1, 7)]]
    cr = R - taper * 0.2 - rt
    for i in range(1, 13):
        a_ = math.radians(90.0 * i / 12)
        p.append((cr + rt * math.cos(a_), side_top + rt * math.sin(a_)))
    p += [(cr * 0.6, Ht), (0.0, Ht)]
    return p

def make_knob(name, cx_px, cy_px, R, Ht, taper, groove, z0, mat):
    ob = revolve(name, knob_profile(R, Ht, taper, groove))
    ob.location = (wx(cx_px), wy(cy_px), z0); ob.data.materials.append(mat); return ob

def box(name, sx, sy, sz, loc, bevel=0.0, mat=None, segs=4):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.active_object; ob.name = name; ob.scale = (sx, sy, sz)
    bpy.ops.object.transform_apply(scale=True)
    if bevel > 0:
        md = ob.modifiers.new('bev', 'BEVEL'); md.width = bevel; md.segments = segs; md.limit_method = 'ANGLE'
    if mat: ob.data.materials.append(mat)
    return ob

def slot_cutter(name, cx_px, y0_px, y1_px, w, loc_z=-3.0, h=10.0):   # recessed channel (the look the owner approved)
    ln = y1_px - y0_px
    ob = box(name, w, ln, h, (wx(cx_px), wy((y0_px + y1_px) / 2.0), loc_z), bevel=w / 2.0 - 0.001, segs=16)
    ob.hide_render = True; ob.display_type = 'WIRE'; return ob

# ---------------------------------------------------------------- materials
img_ink = bpy.data.images.load(os.path.join(ART, 'silk.png')); img_ink.colorspace_settings.name = 'Non-Color'
img_h = bpy.data.images.load(os.path.join(ART, 'silk_height.png')); img_h.colorspace_settings.name = 'Non-Color'

def powder_ink_material():
    m = new_mat('PowderCoat'); nt = m.node_tree
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = img_ink; tex.interpolation = 'Cubic'; tex.extension = 'EXTEND'
    hx = nt.nodes.new('ShaderNodeTexImage'); hx.image = img_h; hx.interpolation = 'Cubic'; hx.extension = 'EXTEND'
    nt.links.new(tc.outputs['Generated'], tex.inputs['Vector']); nt.links.new(tc.outputs['Generated'], hx.inputs['Vector'])
    # powder-coat orange peel: two noise scales
    n1 = nt.nodes.new('ShaderNodeTexNoise'); n1.inputs['Scale'].default_value = 1.6; n1.inputs['Detail'].default_value = 4.0
    n2 = nt.nodes.new('ShaderNodeTexNoise'); n2.inputs['Scale'].default_value = 0.35; n2.inputs['Detail'].default_value = 2.0
    nt.links.new(tc.outputs['Object'], n1.inputs['Vector']); nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    add = nt.nodes.new('ShaderNodeMath'); add.operation = 'ADD'
    nt.links.new(n1.outputs['Fac'], add.inputs[0]); nt.links.new(n2.outputs['Fac'], add.inputs[1])
    bump1 = nt.nodes.new('ShaderNodeBump'); bump1.inputs['Strength'].default_value = 0.22; bump1.inputs['Distance'].default_value = 0.30
    nt.links.new(add.outputs['Value'], bump1.inputs['Height'])
    bump2 = nt.nodes.new('ShaderNodeBump'); bump2.inputs['Strength'].default_value = 0.45; bump2.inputs['Distance'].default_value = 0.18
    nt.links.new(hx.outputs['Color'], bump2.inputs['Height']); nt.links.new(bump1.outputs['Normal'], bump2.inputs['Normal'])
    white = principled(nt); set_in(white, 'Base Color', (1.0, 1.0, 1.0, 1)); set_in(white, 'Roughness', 0.58)
    set_in(white, 'Emission Color', (1.0, 1.0, 1.0, 1)); set_in(white, 'Emission Strength', float(os.environ.get('MOJO_WHITE', '0.07')))   # lifts flat areas to true white; shadows stay
    set_in(white, 'Specular IOR Level', 0.45)
    pink = principled(nt); set_in(pink, 'Base Color', (0.80, 0.035, 0.26, 1)); set_in(pink, 'Roughness', 0.55)
    set_in(pink, 'Specular IOR Level', 0.25)
    for b in (white, pink): nt.links.new(bump2.outputs['Normal'], b.inputs['Normal'])
    mix = nt.nodes.new('ShaderNodeMixShader')
    nt.links.new(tex.outputs['Color'], mix.inputs['Fac'])
    nt.links.new(white.outputs['BSDF'], mix.inputs[1]); nt.links.new(pink.outputs['BSDF'], mix.inputs[2])
    nt.links.new(mix.outputs['Shader'], out.inputs['Surface']); return m

mat_plate = powder_ink_material()
mat_al = aluminium('Aluminium', 0.44, 0.55)
mat_al_s = aluminium('AluminiumSmall', 0.44, 0.55)
mat_cap = aluminium('AluminiumCap', 0.26, 0.0, bands=3.0)
mat_dark = new_mat('Black'); d = principled(mat_dark.node_tree); set_in(d, 'Base Color', (0.01, 0.01, 0.012, 1)); set_in(d, 'Roughness', 0.7)
o = mat_dark.node_tree.nodes.new('ShaderNodeOutputMaterial'); mat_dark.node_tree.links.new(d.outputs['BSDF'], o.inputs['Surface'])
mat_steel = new_mat('Zinc'); d = principled(mat_steel.node_tree); set_in(d, 'Base Color', (0.62, 0.63, 0.65, 1)); set_in(d, 'Metallic', 1.0); set_in(d, 'Roughness', 0.33)
o = mat_steel.node_tree.nodes.new('ShaderNodeOutputMaterial'); mat_steel.node_tree.links.new(d.outputs['BSDF'], o.inputs['Surface'])

# ---------------------------------------------------------------- geometry
THK = 8.0
CH = float(os.environ.get('MOJO_CHAMFER', '5.0'))      # chamfer width in logical px, all four edges
def chamfered_plate():
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts: v.co.x *= W; v.co.y *= H; v.co.z = v.co.z * THK - THK / 2.0
    top = [e for e in bm.edges if all(abs(v.co.z) < 1e-6 for v in e.verts)]
    bmesh.ops.bevel(bm, geom=top, offset=CH, segments=1, affect='EDGES', profile=0.5)
    me = bpy.data.meshes.new('Plate'); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new('Plate', me); sc.collection.objects.link(ob)
    md = ob.modifiers.new('bev', 'BEVEL'); md.width = 0.35; md.segments = 2; md.limit_method = 'ANGLE'; md.angle_limit = math.radians(30)
    ob.data.materials.append(mat_plate); return ob
plate = chamfered_plate()
cutters = []
for i, x in enumerate(L['SW_X']):
    cutters.append(slot_cutter(f'slot{i}', x, L['SLOT']['y0'], L['SLOT']['y1'], L['SLOT']['w']))
bm_ = plate.modifiers.new('slots', 'BOOLEAN'); bm_.operation = 'DIFFERENCE'; bm_.solver = 'EXACT'
# one boolean per cutter
plate.modifiers.remove(bm_)
for c in cutters:
    m_ = plate.modifiers.new('cut_' + c.name, 'BOOLEAN'); m_.operation = 'DIFFERENCE'; m_.solver = 'EXACT'; m_.object = c
# reorder: bevel last
plate.modifiers.move(plate.modifiers.find('bev'), len(plate.modifiers) - 1)

chassis = box('Chassis', W + 40, H + 40, 1, (0, 0, -14), mat=mat_dark)

screws = []
for sx in (L['SCREW'], W - L['SCREW']):
    for sy in (L['SCREW'], H - L['SCREW']):
        head = revolve('screw', [(4.4, 0.0), (4.4, 0.7), (4.0, 1.25), (3.2, 1.65), (1.8, 1.9), (0.0, 1.95)], 64)
        head.location = (wx(sx), wy(sy), 0.0); head.data.materials.append(mat_steel)
        for k, ang in enumerate((0.0, 90.0)):
            c = box(f'cross{k}', 6.6, 1.05, 1.6, (wx(sx), wy(sy), 1.55)); c.rotation_euler[2] = math.radians(ang)
            c.hide_render = True; c.display_type = 'WIRE'
            m_ = head.modifiers.new(f'x{k}', 'BOOLEAN'); m_.operation = 'DIFFERENCE'; m_.solver = 'EXACT'; m_.object = c
        screws.append(head)

K = L['KNOB']
knob_big = make_knob('KnobBig', K['cx'], K['cy'], K['r'], 15.5, 3.6, 33.0, 2.0, mat_al)
smalls = [make_knob(f'KnobSmall{i}', x, L['SMALL_Y'], L['SMALL_R'], 8.5, 2.2, 8.5, 1.2, mat_al_s) for i, x in enumerate(L['SMALL_X'])]

cap = box('Cap', 26.0, 30.0, 6.0, (wx(L['SW_X'][0]), wy(L['SW_Y'][1]), 1.2 + 3.0), bevel=2.6, mat=mat_cap, segs=6)
cap_pos = (wx(L['SW_X'][0]), wy(L['SW_Y'][1]))
ribs = []
for k in range(-2, 3):
    r_ = box(f'Rib{k}', 21.0, 2.0, 1.6, (wx(L['SW_X'][0]), wy(L['SW_Y'][1]) + k * 4.6, 1.2 + 6.0 + 0.5), bevel=0.7, mat=mat_cap, segs=5)
    ribs.append(r_)

# ---------------------------------------------------------------- lights, world, camera
w = bpy.data.worlds.new('W'); sc.world = w; w.use_nodes = True
bg = w.node_tree.nodes['Background']; bg.inputs['Color'].default_value = (1, 1, 1, 1)
lp = w.node_tree.nodes.new('ShaderNodeLightPath'); mth = w.node_tree.nodes.new('ShaderNodeMath'); mth.operation = 'MULTIPLY_ADD'
mth.inputs[1].default_value = -0.38; mth.inputs[2].default_value = 0.50   # diffuse ambient 0.50, glossy (metal) world 0.12   # diffuse ambient 0.50, glossy (metal) 0.20
w.node_tree.links.new(lp.outputs['Is Glossy Ray'], mth.inputs[0]); w.node_tree.links.new(mth.outputs['Value'], bg.inputs['Strength'])

def area(name, loc, size, energy, target=(0, 0, 0)):
    ld = bpy.data.lights.new(name, 'AREA'); ld.shape = 'SQUARE'; ld.size = size; ld.energy = energy
    ob = bpy.data.objects.new(name, ld); sc.collection.objects.link(ob); ob.location = loc; ob.visible_glossy = False
    d = Vector(target) - Vector(loc); ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler(); return ob
KEY = float(os.environ.get('MOJO_KEY', '1500000'))
FILL = float(os.environ.get('MOJO_FILL', '400000'))
# everything on the centre line x = 0, so lighting and shadows are mirror-symmetric
area('key', (0, 120, 650), 200, KEY)
area('front', (0, -380, 260), 420, FILL)
# overhead softbox: seen by glossy reflections only (never lights the plate); used for the slide caps
top = area('top', (0, 0, 520), 300, 14000 if MODE == 'cap' else 0.0)
top.visible_diffuse = False; top.visible_glossy = True


def softbox(name, loc, w_, h_, strength):
    bpy.ops.mesh.primitive_plane_add(size=1, location=loc)
    o = bpy.context.active_object; o.name = name; o.scale = (w_, h_, 1)
    o.rotation_euler = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('Z', 'Y').to_euler()
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; nt.nodes.clear()
    e = nt.nodes.new('ShaderNodeEmission'); e.inputs['Strength'].default_value = strength
    out = nt.nodes.new('ShaderNodeOutputMaterial'); nt.links.new(e.outputs['Emission'], out.inputs['Surface'])
    o.data.materials.append(m)
    o.visible_camera = False; o.visible_diffuse = False; o.visible_shadow = False
    return o
SBX = float(os.environ.get('MOJO_SB', '14'))
CAPM = (MODE == 'cap')
softbox('sbL', (-420, 0, 230), 150, 420, SBX * (1.0 if CAPM else 1))      # mirror pair, symmetric about x = 0
softbox('sbR', ( 420, 0, 230), 150, 420, SBX * (1.0 if CAPM else 1))
softbox('sbT', (0, 330, 300), 300, 90, SBX * (2.4 if CAPM else 0.6))
softbox('sbB', (0, -330, 300), 300, 90, SBX * (0.15 if CAPM else 0.35))

cam = bpy.data.cameras.new('Cam'); cam.type = 'ORTHO'; cam.ortho_scale = float(W); cam.clip_start = 1; cam.clip_end = 2000
co = bpy.data.objects.new('Cam', cam); sc.collection.objects.link(co); co.location = (0, 0, 700); sc.camera = co

# ---------------------------------------------------------------- pointers
# Rendered pointing up (+Y) with the rotation centre at the tile centre; the plugin rotates the picture.
mat_ptr = new_mat('PointerEnamel'); _p = principled(mat_ptr.node_tree)
set_in(_p, 'Base Color', (0.80, 0.035, 0.26, 1)); set_in(_p, 'Roughness', 0.42); set_in(_p, 'Specular IOR Level', 0.5)
_o = mat_ptr.node_tree.nodes.new('ShaderNodeOutputMaterial'); mat_ptr.node_tree.links.new(_p.outputs['BSDF'], _o.inputs['Surface'])

def pointer_obj(name, r0, r1, w, z, thick=0.7):
    rad = w * 0.4; n = 6; bm = bmesh.new(); pts = []
    corners = [((w / 2 - rad, r1 - rad), 0), ((-(w / 2 - rad), r1 - rad), 90), ((-(w / 2 - rad), r0 + rad), 180), ((w / 2 - rad, r0 + rad), 270)]
    for (cx, cy), a0_ in corners:
        for i in range(n + 1):
            a_ = math.radians(a0_ + 90.0 * i / n)
            pts.append(bm.verts.new((cx + rad * math.cos(a_), cy + rad * math.sin(a_), 0.0)))
    face = bm.faces.new(pts)
    ret = bmesh.ops.extrude_face_region(bm, geom=[face])
    vs = [g_ for g_ in ret['geom'] if isinstance(g_, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=(0, 0, thick), verts=vs)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); sc.collection.objects.link(ob); ob.location = (0, 0, z)
    md = ob.modifiers.new('bev', 'BEVEL'); md.width = 0.22; md.segments = 2; md.limit_method = 'ANGLE'
    ob.data.materials.append(mat_ptr); ob.hide_render = True
    return ob

ptr_big = pointer_obj('PtrBig', 18.0, 43.0, 3.2, 2.0 + 15.5)
ptr_small = pointer_obj('PtrSmall', 4.0, 11.0, 2.0, 1.2 + 8.5)

def tile_pass(name, ob, tile):
    hide_all(); ob.hide_render = False
    sc.render.resolution_x = sc.render.resolution_y = int(tile * S)
    cam.ortho_scale = float(tile); co.location = (0, 0, 700)
    render(name, True)
# ---------------------------------------------------------------- passes
def vis(objs, camera):
    for o_ in objs: o_.visible_camera = camera

everything = [plate, chassis, cap, knob_big] + smalls + screws + ribs

def hide_all():
    for o_ in everything: o_.hide_render = True
    for c in cutters: c.hide_render = True

def render(name, transparent):
    sc.render.film_transparent = transparent
    sc.render.filepath = os.path.join(OUT, name + '.png'); bpy.ops.render.render(write_still=True)

if MODE in ('plate', 'test'):
    hide_all()
    for o_ in [plate, chassis] + screws + [knob_big] + smalls: o_.hide_render = False
    for o_ in [knob_big] + smalls: o_.visible_camera = False      # casts shadow, is not drawn
    cap.hide_render = True
    render('plate' if MODE == 'plate' else 'test', False)
elif MODE == 'knob':
    hide_all(); knob_big.hide_render = False; render('knob_big', True)
elif MODE == 'small':
    hide_all()
    for o_ in smalls: o_.hide_render = False
    render('knob_small', True)
elif MODE == 'cap':
    hide_all(); cap.hide_render = False
    for r_ in ribs: r_.hide_render = False      # cap alone on transparent; its shadow is added in post, noise-free
    render('cap', True)
elif MODE == 'ptr_big':
    tile_pass('ptr_big', ptr_big, 96)
elif MODE == 'ptr_small':
    tile_pass('ptr_small', ptr_small, 32)
print('DONE', MODE)
