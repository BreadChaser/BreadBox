"""A room style's rig from lights.json, and the colours of the parts no light reaches (runs in Blender).

The light itself is Cycles' (gi.py: the style's "gi" block). This holds what the bake does not: the flat tints of
untextured parts, the self-light of the lamp shade and the candle (emit), and the colours of unlit parts (the
window's sky gradient; other unlit parts keep their tint).
"""
from mathutils import Vector


def hx(h, s=1.0):
    h = h.lstrip('#')
    return Vector(tuple(int(h[i:i + 2], 16) / 255 * s for i in (0, 2, 4)))


class Rig:
    def __init__(self, st):
        self.st = st
        self.tints = {k: hx(v) for k, v in st['tint'].items()}
        self.emit = st.get('emit', {})

    def tint(self, key):
        return self.tints[key] if key else Vector((1, 1, 1))


def unlit_colour(rig, part, p, sky):
    """An unlit part's colour x tint at world point p: the sky's gradient (part.vcol) or white, clamped to 2.0."""
    if part.vcol:
        _, zt, zb = part.vcol
        f = min(((zt - p.z) / (zt - zb)) / sky['span'] * (len(sky['stops']) - 1), len(sky['stops']) - 1 - 1e-6)
        i = int(f)
        c = sky['stops'][i].lerp(sky['stops'][i + 1], f - i)
    else:
        c = Vector((1, 1, 1))
    t = rig.tint(part.tint)
    return (min(c.x * t.x, 2.0), min(c.y * t.y, 2.0), min(c.z * t.z, 2.0))
