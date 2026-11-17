"""Build the production shelf hit geometry for recording-renderer tests."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'look_equivalence'))
from extract import function
root=Path(__file__).resolve().parents[2]
src=(root/'app/ui/library_touch.c').read_text()
print('#define X0 40.0f')
for name in ('bb_library_strip_length','shelf_scene_strip_position','shelf_scene_strip_hit','home_row_y','shelf_scene_card_hit','shelf_room_letters','shelf_room_letter_hit'):
 print(function(src,name))
