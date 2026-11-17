#!/usr/bin/env python3
"""The real boxes' mesh cache keys on the room file's light generation (room_file.gen, new on every
room_style_use, which room_parse calls; tests/room/check.c covers both). This checks the rest of the wiring in
room_scene.c: draw_boxes tests and records that same generation, and no private counter is back."""
import pathlib, re
root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'app/ui/room_scene.c').read_text()
body = src[src.index('static void draw_boxes('):]
body = body[:body.index('\n}\n')]
assert re.search(r'room_mesh_key_stale\(&key, S\.f\.gen, lib->cur, nb, same\)', body), 'draw_boxes must test S.f.gen'
assert re.search(r'room_mesh_key_set\(&key, S\.f\.gen, lib->cur, nb\)', body), 'draw_boxes must record S.f.gen'
assert 'box_gen' not in src, 'a private generation is back: the room file\'s is the one tested'
style = src[src.index('void room_scene_style('):]
style = style[:style.index('\n}\n')]
assert 'room_style_use(&S.f, i);' in style, 'a style change must go through room_style_use (it makes the new generation)'
print('room boxes: the mesh cache keys on the room file\'s light generation PASS')
