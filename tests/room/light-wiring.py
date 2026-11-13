#!/usr/bin/env python3
"""The room's light range and dither strength reach the GPU (V3 review F2). The portable half (room_bbr.c) is tested
in check.c; this reads the draw path the host cannot run:
  - draw() sets the vertex shader's lscale to the style's range / 1020 before its first draw call (without it the
    uniform reads 0 and the whole room is black);
  - room.v.pica scales a mesh vertex's colour by lscale (not the old fixed 1/510);
  - load() sets the final pass's dither strength from the room file's `post` anchor before the pass is made;
  - the real boxes take the cubby's light as 1.0 = neutral and put it in the style's unit once (room_lit_rgb);
  - the hands keep their shading under Day's bright probes (room_hands_draw halves warm and scales x4).
"""
from pathlib import Path
import re
import sys

R = Path(__file__).resolve().parents[2]


def body(src, head):
    """the text of the function whose definition starts with head, up to its closing brace at column 0"""
    i = src.index(head)
    return src[i:src.index('\n}\n', i)]


def main():
    scene = (R / 'app/ui/room_scene.c').read_text()
    draw = body(scene, 'static void draw(const room_pose')
    m = re.search(r'set_vec\(S\.u_lscale,\s*S\.f\.lrange / 1020\.0f,', draw)
    assert m, 'draw(): lscale.x must be set to S.f.lrange / 1020'
    first = min(i for i in (draw.find('C3D_Draw'), draw.find('draw_boxes('), draw.find('room_walls_draw(')) if i >= 0)
    assert m.start() < first, 'draw(): lscale is set after the first draw'
    assert 'S.u_lscale = shaderInstanceGetUniformLocation(vs, "lscale")' in scene, 'load(): the lscale uniform is not looked up'

    vs = (R / 'app/ui/room.v.pica').read_text()
    assert re.search(r'^\.fvec lscale\b', vs, re.M), 'room.v.pica: no lscale uniform'
    mesh = vs[vs.index('.else'):vs.index('.end', vs.index('.else'))] if '.else' in vs else ''
    assert re.search(r'mul r5\.xyz, lscale\.xxx, inclr\.xyz', mesh), 'room.v.pica: the mesh path must scale its colour by lscale.x'
    assert not re.search(r'\bkc\.x', vs), 'room.v.pica: the fixed 1/510 scale (kc.x) is back'

    load = body(scene, 'static bool load(void)')
    d = re.search(r'room_dither_set\(post \? post\[0\] : 1\)', load)
    assert d and 'room_anchor(&S.f, "post")' in load, 'load(): the dither strength must come from the post anchor'
    assert d.start() < load.index('final_pass_make();'), 'load(): the dither strength is set after the pass is made'

    boxes = body(scene, 'static int box_mesh(')
    assert 'room_cubby_light_f(&S.f, &g, light)' in boxes and 'room_lit_rgb(&S.f,' in boxes, \
        'box_mesh(): the boxes must take the cubby light as 1.0 = neutral and room_lit_rgb'
    assert not re.search(r'/\s*128', boxes), 'box_mesh(): a fixed 128 = 1.0 is back (half the light in Day)'

    hands = body((R / 'app/ui/room_hands.c').read_text(), 'float room_hands_draw(')
    assert re.search(r'int wscale = wmax \* \.56f > 1 \? 4 : 2;', hands) and 'warm[0] * 2 / wscale' in hands, \
        'room_hands_draw(): bright warm light must be halved'
    assert 'wscale == 4 ? GPU_TEVSCALE_4 : GPU_TEVSCALE_2' in hands, 'room_hands_draw(): the combiner must undo the halving'
    print('room light wiring PASS: lscale before the first draw, the shader scales by it, dither strength before the '
          'final pass, boxes and hands in the style\'s unit')


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, ValueError) as e:
        print('FAIL:', e)
        sys.exit(1)
