"""Build props.bbr (the game-start PS1 jewel case and GBA box the hands hold). Any Python 3 with NumPy:
tools/room/build.sh --hands runs it with the pinned Blender's Python.

    python3 tools/room/source/props/build.py OUT_DIR
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import props  # noqa: E402

out = sys.argv[sys.argv.index('--') + 1] if '--' in sys.argv else sys.argv[1]
os.makedirs(out, exist_ok=True)
print('props.bbr', props.build(out), flush=True)
