"""Cycles global-illumination bake of the Lamplight room (light round 3). Runs inside the pinned Blender.

The room's final mesh pieces (after the texture-repeat cuts) become Blender objects with flat diffuse materials whose
colour is each texture's mean x its tint (so bounce light takes the room's colours: green paper, honey floor). Real
Cycles lights (a sun through the window, a sky behind it, lamps inside their shades, area lights) light it; Cycles
path-traces direct + indirect diffuse light (GI, soft shadows, contact shadows, light that wraps). Results:
  - per vertex: the light at every mesh vertex (the BBR colour stream);
  - per probe: light along the six axes (the bookcase's runtime boxes, the hands).
Light -> display: v = shoulder((E x exposure) ^ (1 / gamma)), at most the style's light range (2.0, the PS1 x2, or
4.0). A bake-only shell closes the room (no front wall in the model) and leaves the window open.
Bit-exact only for the same Blender build and sample counts (the style's gi samples / ao_samples).
"""
import math

import bpy
import numpy as np
from mathutils import Vector


def hx(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


def lin(c):
    return tuple((x / 12.92) if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def reset():
    for coll in (bpy.data.objects, bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.worlds, bpy.data.images,
                 bpy.data.collections):
        for x in list(coll):
            coll.remove(x)
    _mats.clear()
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    return sc


_mats = {}


def material(name, albedo, emit=None, translucent=0.0):
    key = (name, tuple(albedo), tuple(emit) if emit else None, translucent)
    if key in _mats:
        return _mats[key]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    d = nt.nodes.new('ShaderNodeBsdfDiffuse')
    d.inputs['Color'].default_value = tuple(albedo) + (1,)
    sh = d.outputs[0]
    if translucent:
        t = nt.nodes.new('ShaderNodeBsdfTranslucent')
        t.inputs['Color'].default_value = tuple(albedo) + (1,)
        mx = nt.nodes.new('ShaderNodeMixShader')
        mx.inputs[0].default_value = translucent
        nt.links.new(sh, mx.inputs[1])
        nt.links.new(t.outputs[0], mx.inputs[2])
        sh = mx.outputs[0]
    if emit:
        e = nt.nodes.new('ShaderNodeEmission')
        e.inputs['Color'].default_value = tuple(lin(hx(emit[0]))) + (1,)
        e.inputs['Strength'].default_value = emit[1]
        ad = nt.nodes.new('ShaderNodeAddShader')
        nt.links.new(sh, ad.inputs[0])
        nt.links.new(e.outputs[0], ad.inputs[1])
        sh = ad.outputs[0]
    nt.links.new(sh, out.inputs['Surface'])
    _mats[key] = m
    return m


def mesh_object(name, faces, mat, smooth=False, rays=True):
    """faces: [[(x, y, z), ...]] world space; one Blender vertex per distinct position (smooth parts share normals)."""
    vid, verts, fidx = {}, [], []
    for f in faces:
        ids = []
        for p in f:
            k = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
            if k not in vid:
                vid[k] = len(verts)
                verts.append(tuple(p))
            ids.append(vid[k])
        fidx.append(ids)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], fidx)
    me.update()
    for p in me.polygons:
        p.use_smooth = smooth
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    if not rays:
        for a in ('visible_diffuse', 'visible_shadow', 'visible_glossy', 'visible_transmission', 'visible_volume_scatter'):
            setattr(ob, a, False)
    return ob


def shell(scn):
    """Bake-only walls just outside the room, with the window open: no light leaks in through the open front."""
    m = material('shell', (0.2, 0.2, 0.2))
    X0, X1, Y0, Y1, Z1 = scn.RX0 - 0.06, scn.RX1 + 0.06, scn.RY0 - 0.06, scn.RY1 + 0.06, scn.RZ + 0.06
    Z0 = -0.07
    wx0, wx1, wz0, wz1 = scn.WIN
    q = []
    q.append([(X0, Y0, Z0), (X1, Y0, Z0), (X1, Y1, Z0), (X0, Y1, Z0)])       # under the floor
    q.append([(X0, Y0, Z1), (X0, Y1, Z1), (X1, Y1, Z1), (X1, Y0, Z1)])       # over the ceiling
    q.append([(X0, Y0, Z0), (X0, Y0, Z1), (X1, Y0, Z1), (X1, Y0, Z0)])       # front
    q.append([(X0, Y0, Z0), (X0, Y1, Z0), (X0, Y1, Z1), (X0, Y0, Z1)])       # left
    q.append([(X1, Y0, Z0), (X1, Y0, Z1), (X1, Y1, Z1), (X1, Y1, Z0)])       # right
    # the back, around the window's opening (the wall's own hole)
    for (a, b, za, zb) in ((X0, wx0, Z0, Z1), (wx1, X1, Z0, Z1), (wx0, wx1, Z0, wz0), (wx0, wx1, wz1, Z1)):
        q.append([(a, Y1, za), (b, Y1, za), (b, Y1, zb), (a, Y1, zb)])
    mesh_object('shell', q, m)


def portal(scn):
    """A Cycles light portal in the window: the world (sky) is sampled through it, not by chance."""
    wx0, wx1, wz0, wz1 = scn.WIN
    ld = bpy.data.lights.new('portal', 'AREA')
    ld.shape = 'RECTANGLE'
    ld.size, ld.size_y = wx1 - wx0, wz1 - wz0
    ld.cycles.is_portal = True
    ob = bpy.data.objects.new('portal', ld)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = Vector(((wx0 + wx1) / 2, scn.RY1 + 0.07, (wz0 + wz1) / 2))
    ob.rotation_euler = Vector((0, -1, 0)).to_track_quat('-Z', 'Y').to_euler()


def lights(sc, g):
    """The style's Cycles lights: world (sky), sun/moon, points (bulbs inside shades), spots, areas."""
    w = bpy.data.worlds.new('w')
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes.get('Background')
    c, s = g.get('world', ['#000000', 0.0])
    bg.inputs['Color'].default_value = tuple(lin(hx(c))) + (1,)
    bg.inputs['Strength'].default_value = s
    for L in g.get('lights', []):
        kind, name = L['type'], L['name']
        ld = bpy.data.lights.new(name, kind)
        ld.color = lin(hx(L['color']))
        ld.energy = L['power']
        if hasattr(ld, 'use_soft_falloff'):
            ld.use_soft_falloff = False
        ob = bpy.data.objects.new(name, ld)
        sc.collection.objects.link(ob)
        if 'pos' in L:
            ob.location = Vector(L['pos'])
        if kind == 'SUN':
            ld.angle = math.radians(L.get('angle', 1.0))
        if kind in ('POINT', 'SPOT'):
            ld.shadow_soft_size = L.get('radius', 0.03)
        if kind == 'SPOT':
            ld.spot_size = math.radians(L.get('cone', 90))
            ld.spot_blend = L.get('blend', 0.5)
        if kind == 'AREA':
            ld.shape = 'RECTANGLE'
            ld.size, ld.size_y = L['size'][0], L['size'][1]
        if 'aim' in L:   # direction the light points (sun: toward where its rays go)
            d = Vector(L['aim']).normalized()
            ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def link(g, objs):
    """Light linking (Cycles): a light with "only": [part names] lights just those parts directly, one with "except":
    [part names] all but those (their bounce light is unchanged). Art direction: the window's light on the
    wallpaper beside it gets its own colour and strength."""
    by_name = {ob.name: ob for ob in objs}
    for L in g.get('lights', []):
        names = L.get('only') or L.get('except')
        if not names:
            continue
        c = bpy.data.collections.new('link-' + L['name'])
        for n in names:
            c.objects.link(by_name[n])
        lo = bpy.data.objects[L['name']]
        lo.light_linking.receiver_collection = c
        if 'except' in L:
            for co in c.collection_objects:
                co.light_linking.link_state = 'EXCLUDE'


def tone(E, gi):
    """Irradiance (linear) -> display light, 1.0 = neutral, at most the style's light range (2.0, the PS1 x2, or 4.0)."""
    x = np.maximum(E, 0) * gi.get('exposure', 1.0)
    v = x ** (1.0 / gi.get('gamma', 2.2))
    top = gi.get('range', 2)                # the light range of the style: 2.0 (PS1 x2) or 4.0
    sh = gi.get('shoulder', top)            # soft roll-off towards the ceiling
    v = sh * np.tanh(v / sh)
    lift = gi.get('lift', 0.0)              # a floor so the darkest corners stay readable
    v = lift + (1 - lift / top) * v
    return np.minimum(v, top)


def falloff(points, gi):
    """The style's baked falloff (an art-directed vignette in the light itself): 1.0 within `radius` metres of
    `center`, easing to `floor` at radius + edge. Ellipsoidal: `scale` stretches the distance per axis (x, y, z).
    None points (sprites) keep 1.0. Absent "falloff" = no change."""
    f = gi.get('falloff')
    out = np.ones(len(points))
    if not f:
        return out
    c, s = np.array(f['center']), np.array(f.get('scale', (1.0, 1.0, 1.0)))
    for i, p in enumerate(points):
        if p is None:
            continue
        d = float(np.linalg.norm((np.array(tuple(p)) - c) * s))
        t = min(max((d - f['radius']) / f['edge'], 0.0), 1.0)
        t = t * t * (3 - 2 * t)
        out[i] = 1.0 - (1.0 - f['floor']) * t
    return out


PROBE_AXES = ((1, 0, 0), (-1, 0, 0), (0, 0, 1), (0, 0, -1), (0, 1, 0), (0, -1, 0))   # BBR +x -x +y -y +z -z


def cube_faces(c, r=0.01):
    """Six outward quads of a small cube at c, in PROBE_AXES order."""
    out = []
    for ax in PROBE_AXES:
        n = Vector(ax)
        t = Vector((0, 0, 1)) if abs(n.z) < 0.5 else Vector((1, 0, 0))
        u = n.cross(t).normalized()
        v = n.cross(u)
        o = Vector(c) + n * r
        out.append([tuple(o + (u * a + v * b) * r) for a, b in ((-1, -1), (-1, 1), (1, 1), (1, -1))])
    return out


def scene(rig_all, style, pieces, albedo_of, scn, threads):
    """One room style's Cycles scene: the closed room, its lights and the mesh as objects with flat materials.
    Returns (scene, the style's gi block, objects, the light key of every object's face corner)."""
    st = rig_all['styles'][style]
    g = st['gi']
    sc = reset()
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = threads
    sc.cycles.max_bounces = g.get('bounces', 6)
    sc.cycles.diffuse_bounces = g.get('bounces', 6)
    sc.cycles.seed = 1
    if 'clamp' in g:
        sc.cycles.sample_clamp_indirect = float(g['clamp'])
    shell(scn)
    lights(sc, g)
    if g.get('portal', True):
        portal(scn)
    by_part, order = {}, []
    for part, corners in pieces:
        nrm = Vector()
        for i in range(len(corners)):   # Newell: the winding's normal; Blender shades by it, so it must face out
            a, b = corners[i][0], corners[(i + 1) % len(corners)][0]
            nrm += Vector(((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)))
        if nrm.dot(sum((c[1] for c in corners), Vector())) < 0:
            corners = corners[::-1]
        if part.name not in by_part:
            by_part[part.name] = (part, [], [])
            order.append(part.name)
        by_part[part.name][1].append([tuple(c[0]) for c in corners])
        by_part[part.name][2].append([c[2] for c in corners])
    objs, kid_loops = [], []
    emits = g.get('emit', {})
    for name in order:
        part, faces, kids = by_part[name]
        sky = part.vcol is not None or name.startswith('sky')
        em = emits.get(part.emit) if part.emit else None
        tr = g.get('translucent', {}).get(name, 0.0)
        ob = mesh_object(name, faces, material(name, albedo_of(part), em, tr), part.smooth, rays=not sky and part.cast)
        objs.append(ob)
        kid_loops.append([k for f in kids for k in f])
    link(g, objs)
    return sc, g, objs, kid_loops


def reference(rig_all, style, pieces, albedo_of, scn, path, threads):
    """A plain Cycles camera render of the style's bake scene from Home (build.py --ref): the light the bake aims
    for, without the PS1 textures. No room file comes of it."""
    sc, g, objs, kid_loops = scene(rig_all, style, pieces, albedo_of, scn, threads)
    sc.view_settings.exposure = math.log2(g.get('exposure', 1.0))
    render_ref(path, (0.05, -2.75, 1.52), (0.1, 1.2, 0.98), 64)


def bake(rig_all, style, pieces, nkeys, albedo_of, probes, scn, samples, ao_samples, threads):
    """Light (linear irradiance, rgb) at every vertex key and probe axis of one room style.
    pieces: [(part, [(pos, n, kid), ...])] the final mesh's polygons; albedo_of(part) -> linear rgb."""
    import hashlib, json, os
    cdir = os.environ.get('GI_CACHE')   # dev aid: reuse an identical bake (tone, falloff, ao strength re-run in seconds)
    if cdir:
        g0 = rig_all['styles'][style]['gi']
        light_keys = {k: g0.get(k) for k in ('lights', 'world', 'portal', 'clamp', 'bounces', 'emit', 'translucent', 'ao_dist')}
        alb = [(p.name, tuple(round(x, 5) for x in albedo_of(p))) for p, _ in pieces]
        h = hashlib.sha256(json.dumps([light_keys, sorted(set(alb)), len(pieces), samples, ao_samples, bool(g0.get('ao', 0) > 0),
                                       [tuple(round(x, 4) for x in p) for p in probes]], sort_keys=True, default=str).encode()).hexdigest()[:16]
        cf = os.path.join(cdir, 'bake-%s.npz' % h)
        if os.path.exists(cf):
            z = np.load(cf)
            print('gi cache hit', style, h, flush=True)
            light = z['light'] * (z['ao'] ** g0['ao'])[:, None] if g0.get('ao', 0) > 0 else z['light']
            return light, z['cnt'], list(z['pl'])
    sc, g, objs, kid_loops = scene(rig_all, style, pieces, albedo_of, scn, threads)
    pob = []
    for i, p in enumerate(probes):
        pob.append(mesh_object('probe%d' % i, cube_faces(p), material('probe', (0.5, 0.5, 0.5)), rays=False))
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.cycles.use_adaptive_sampling = False
    sc.render.bake.target = 'VERTEX_COLORS'
    bpy.ops.object.select_all(action='DESELECT')
    for ob in objs + pob:
        me = ob.data
        a = me.color_attributes.new('light', 'FLOAT_COLOR', 'CORNER')
        me.color_attributes.active_color = a
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.bake(type='DIFFUSE', pass_filter={'DIRECT', 'INDIRECT'}, target='VERTEX_COLORS')
    ao = None
    if g.get('ao', 0) > 0:   # Cycles' AO pass (contact shadows within ao_dist), multiplied in by tone()
        sc.world.light_settings.distance = g.get('ao_dist', 0.35)
        sc.cycles.samples = ao_samples
        for ob in objs:
            me = ob.data
            a2 = me.color_attributes.new('ao', 'FLOAT_COLOR', 'CORNER')
            me.color_attributes.active_color = a2
        bpy.ops.object.select_all(action='DESELECT')
        for ob in objs:
            ob.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        bpy.ops.object.bake(type='AO', target='VERTEX_COLORS')
        aacc, acnt = np.zeros(nkeys), np.zeros(nkeys)
        for ob, kl in zip(objs, kid_loops):
            a2 = ob.data.color_attributes['ao'].data
            col = np.zeros(len(a2) * 4)
            a2.foreach_get('color', col)
            np.add.at(aacc, kl, col.reshape(-1, 4)[:, 0])
            np.add.at(acnt, kl, 1)
        ao = np.where(acnt > 0, aacc / np.maximum(acnt, 1), 1.0)
    acc = np.zeros((nkeys, 3))
    cnt = np.zeros(nkeys)
    for ob, kl in zip(objs, kid_loops):
        a = ob.data.color_attributes['light'].data
        col = np.zeros(len(a) * 4)
        a.foreach_get('color', col)
        col = col.reshape(-1, 4)[:, :3]
        assert len(col) == len(kl), (ob.name, len(col), len(kl))
        np.add.at(acc, kl, col)
        np.add.at(cnt, kl, 1)
    light = acc / np.maximum(cnt, 1)[:, None]
    pl = []
    for ob in pob:
        a = ob.data.color_attributes['light'].data
        col = np.zeros(len(a) * 4)
        a.foreach_get('color', col)
        pl.append(col.reshape(6, 4, 4)[:, :, :3].mean(1))
    if cdir:
        np.savez(cf, light=light, cnt=cnt, pl=np.array(pl), ao=ao if ao is not None else np.ones(nkeys))
    if ao is not None:   # contact shadows: light x ao ^ strength
        light = light * (ao ** g['ao'])[:, None]
    return light, cnt, pl


def render_ref(path, pos, look, fov_deg, samples=64, w=400, h=240):
    """A plain Cycles camera render of the bake scene (flat albedo, no textures): the light the bake aims for."""
    sc = bpy.context.scene
    cam = bpy.data.cameras.new('cam')
    cam.angle = math.radians(fov_deg)
    cam.sensor_fit = 'HORIZONTAL'
    ob = bpy.data.objects.new('cam', cam)
    sc.collection.objects.link(ob)
    ob.location = Vector(pos)
    ob.rotation_euler = (Vector(look) - Vector(pos)).to_track_quat('-Z', 'Y').to_euler()
    sc.camera = ob
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.resolution_percentage = 100
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.view_settings.view_transform = 'Standard'
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
