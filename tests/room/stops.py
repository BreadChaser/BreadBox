#!/usr/bin/env python3
"""The room's D-pad stops only at places that open something (deep UI test F02/F18).

The poster and the corkboard are looked at from the room; nothing on them opens, so room_input()
must never send the camera there and the bottom card has no page for them. Reads home.c / home.inc.
"""
from pathlib import Path
import re, sys
r = Path(__file__).resolve().parents[2]
home = (r / 'app/ui/home.c').read_text()
fn = home[home.index('static int room_input(u32 k)'):]
fn = fn[:fn.index('\n}\n')]
card = (r / 'app/ui/components/home.inc').read_text()
bad = []
if re.search(r'(room|home)_view_go\([^)]*ROOM_(BOARD|POSTER)', fn): bad.append('room_input still glides to the poster or the corkboard')
if re.search(r'case ROOM_(BOARD|POSTER):', card): bad.append('home.inc still has a card for a place nothing opens')
if 'Postcards and tickets' in card: bad.append('the dead corkboard card is back')
if bad: sys.exit('\n'.join(bad))
print('room stops PASS: the D-pad stops only at places that open something')
