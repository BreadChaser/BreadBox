#!/usr/bin/env python3
"""Enforce the controller/component/style boundary in both application builds."""
from pathlib import Path
import re,sys,json,hashlib
root=Path(__file__).resolve().parents[2];src=root/'app/ui'
# Shell sources plus every system's own folder: moving a file does not leave the guard.
shell=[root/d for d in ('app/ui','app/settings','app/services','platform/3ds','include/breadbox')]
sources=lambda ext:sorted(p for d in shell for p in d.glob('*'+ext))+sorted((root/'systems').glob('*/*'+ext))
# Rendering a game frame/backdrop/asset is distinct from drawing frontend UI.
# Hashes make these exceptions closed to new drawing without explicit review.
exceptions={'video.c','scene.c','icons.c','ui_mark.c','statsview.c','stereo.c','shelf_scene.c','room_scene.c'}
lex=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
def clean(s):return lex.sub(lambda m:' ' if m[0][0]=='"' or m[0][0]=="'" else '\n'*m[0].count('\n'),s)
primitive=re.compile(r'\b(?:C2D_Draw\w+|ui_text(?:_ex|_wrap|_pixels)?|ui_rect|ui_help_panel|icons_draw|ui_star|ui_menu_header|ui_bar_draw|flist_draw(?:_sections)?)\s*\(')
errors=[];reviewed={}
for p in sources('.c'):
 if p.name=='ui.c':continue
 s=clean(p.read_text())
 if p.name in exceptions:
  reviewed[p.name]=hashlib.sha256(s.encode()).hexdigest();continue
 # The GBA adapter owns only the game frame, not a frontend surface.
 if p.name in ('gba.c','retro_ctr.c'):
  match=re.search(r'static void (?:draw_top|draw)\(void \*unused\)\s*\{.*?\n\}',s,flags=re.S)
  if match:reviewed[p.name+':draw_top']=hashlib.sha256(match[0].encode()).hexdigest()
  s,n=re.subn(r'static void (?:draw_top|draw)\(void \*unused\)\s*\{.*?\n\}', '',s,flags=re.S)
  if n!=1:errors.append(p.name+': game-frame exception no longer matches')
 if primitive.search(s):errors.append(p.name+': screen/controller must use a shared component')
component_paths=sorted((src/'components').glob('*.inc'))+sorted((root/'systems').glob('*/components/*.inc'))
for p in [src/'ui.c',*component_paths]:
 s=clean(p.read_text())
 if p.suffix=='.inc':reviewed[str(p.relative_to(src) if p.is_relative_to(src) else p.relative_to(root))]=hashlib.sha256(s.encode()).hexdigest()
 s=re.sub(r'^\s*#.*','',s,flags=re.M)
 if re.search(r'(?<![\w.])(?:0x[\da-fA-F]+|\d+(?:\.\d*)?|\.\d+)[fFuUlL]*(?![\w.])',s):errors.append(str(p.relative_to(root))+': literal metric/color; add a style token in ui_style.h')
 # Raw RGB color constructors cannot replace theme roles.
 if p.name!='stereo.inc' and re.search(r'\bC2D_Color32\(\s*UI_METRIC_',s):errors.append(p.name+': use a theme role')
# Controllers cannot create a local palette or local UI metric macro.
for p in sources('.c'):
 if p.name in exceptions|{'ui.c','theme.c'}:continue
 if re.search(r'^\s*#define\s+\w*(?:FONT_SIZE|ROW_HEIGHT|HEADER_HEIGHT|BAR_HEIGHT|UI_COLOR)\w*\s+[.\d]',p.read_text(),re.M):errors.append(p.name+': controller-owned style token')
for p in sources('.h'):
 if p.name!='ui_style.h' and primitive.search(clean(p.read_text())):
  reviewed[p.name]=hashlib.sha256(clean(p.read_text()).encode()).hexdigest()
manifest=Path(__file__).with_name('surfaces.json')
if '--record' in sys.argv:manifest.write_text(json.dumps(reviewed,indent=2,sort_keys=True)+'\n')
if reviewed!=json.loads(manifest.read_text()):errors.append('Reviewed game/backdrop exceptions changed; inspect and record intentional artwork changes')
contracts={'gba_settings_ui':'ui_list_draw','settings_ui':'ui_card_draw','browser':'ui_list_draw','savestates':'ui_list_draw','gba':'ui_progress_draw','bb_pause':'ui_dialog_draw','emu':'bb_pause_present_card'}   # PS1 draws its pause card through the shared one
for module,call in contracts.items():
 p=src/'components'/(module+'.inc')
 if call+'(' not in p.read_text():errors.append(module+': missing '+call)
if errors:sys.exit('\n'.join(errors))
print('UI boundary PASS: controllers contain no raw drawing; components use only style tokens; reviewed game/backdrop exceptions unchanged')
