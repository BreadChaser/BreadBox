#!/usr/bin/env python3
"""The room styles (theme.c) and the menu backdrop (scene.c) under ASan/UBSan, replacing SDK includes only."""
SPLIT_HEADERS=['app_paths.h', 'cold_code.h', 'app_log.h', 'app_config.h', 'settings_ui.h', 'last_picture.h', 'bb_platform.h', 'core_options.h', 'platform_video.h', 'core_hooks.h', 'dev_switches.h', 'pmu.h', 'csnd_out.h', 'rewind.h', 'gamecfg.h', 'cheat_codes.h', 'dsp_firmware.h', 'memory_cards.h', 'emu.h', 'saveid.h', 'savestates.h', 'stereo.h', 'audio.h', 'app_input.h', 'presets.h', 'ui_list.h', 'ui_pause.h', 'ui.h', 'library.h', 'covers.h', 'uisound.h', 'libui.h', 'share_about.h', 'play_together.h', 'playlog.h', 'hand_styles.h', 'home.h', 'games_dir.h', 'browser.h', 'app_main.h']
import os,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='psx3d-scenes-') as tmp:
 tmp=pathlib.Path(tmp)
 for name in ('theme','scene','look_theme'):
  source=(root/f'app/ui/{name}.c').read_text()
  for header in ('scene.h','devtools.h','shelf.h','ui.h','list_look.h',*SPLIT_HEADERS):
   source=source.replace(f'#include "{header}"','')
  # psx3d.h used to bring the SDK; this test fakes it in check.c
  source=source.replace('#include <3ds.h>\n','').replace('#include <citro2d.h>\n','').replace('#include "libretro.h"\n','')
  (tmp/f'{name}-body.c').write_text(source)
 for app in ([],):
  cmd=['cc','-std=gnu11','-g','-fsanitize=address,undefined','-Wno-pointer-to-int-cast',*app,'-I'+str(tmp),'-I'+str(root/'tests/app/host'),'-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(root/'tests/themes/check.c'),'-lm','-o',str(tmp/'check')]
  subprocess.run(cmd,check=True);subprocess.run([str(tmp/'check')],check=True)
