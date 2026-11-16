#!/usr/bin/env python3
"""System switches release everything: the source contracts the Old 3DS
cross-system memory fix rests on (the PS1 state-load-after-GBA regression)."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[2]
def text(p):return (root/p).read_text()
errors=[]
run=re.search(r'static ADAPTER bb_run_result run\(.*?\n\}',text('systems/ps1/sys_ps1.c'),re.S)[0]
if not re.search(r'emu_run_game\(.*?\bcards_end\(\);',run,re.S):errors.append('PS1 run(): cards_end() after the game, or its card shadows stay mid-heap')
gba=text('systems/gba/gba.c');tear=gba[gba.index('phase=teardown begin'):]
if not tear.index('C3D_TexDelete(&texture)')<tear.index('gpsp_retro_unload_game()'):errors.append('GBA teardown: linear textures before the cartridge cache')
main=text('app/ui/main.c')
if not 0<=main.find('playlog_init();')<main.find('bb_index_start(g_cfg.games_dir'):errors.append('playlog tables must be made at start')
mk=text('build/3ds/Makefile')
if not re.search(r'ifneq \(\$\(CORE_SYSTEMS\),\)\nLDFLAGS \+= -Wl,--wrap=retro_init,--wrap=retro_deinit\n(#.*\n)*LDFLAGS \+= -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc',mk):errors.append('BreadBox link must route tiny blocks to the small pool')
plat=text('platform/3ds/bb_platform.c')+text('platform/3ds/bb_menu_release.c')
for where in ('menu released, system starting','system ended'):
 if where not in plat:errors.append('switch memory log missing: '+where)
if errors:sys.exit('\n'.join(errors))
print('System switch contracts: PS1 cards freed at game end, GBA linear before cache, playlog at start, small pool linked, switch memory logged PASS')
