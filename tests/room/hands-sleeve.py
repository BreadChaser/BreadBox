#!/usr/bin/env python3
"""Review N5: the room's hands read the sleeve texture (tex[style_tex]) after the hands file loads; a file without
one (case-motion.bbh's compact tracks have none) must be refused first. tests/room/hands.c tests hands_has_sleeve;
this checks room_hands.c asks it before any tex[] use."""
import pathlib, re
src = (pathlib.Path(__file__).resolve().parents[2] / 'app/ui/room_hands.c').read_text()
guard = src.find('if (!hands_has_sleeve(&H.f))')
assert guard >= 0, 'room_hands.c must refuse a hands file without a sleeve texture'
uses = [m.start() for m in re.finditer(r'H\.f\.tex\[', src)]
assert uses, 'room_hands.c no longer reads the sleeve texture: drop this check'
load = src.index('case 0: {')
assert load < guard < min(u for u in uses if u > load), 'the sleeve check must come before the first tex[] read'
print('room hands: a hands file without its sleeve texture is refused before tex[] is read PASS')
