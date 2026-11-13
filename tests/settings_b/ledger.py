from pathlib import Path
r=Path(__file__).resolve().parents[2]
p=(r/'systems/ps1/ps1_settings_ui.c').read_text()
assert 'CAT_DISPLAY,CAT_CONTROLS,CAT_SAVES,CAT_ADVANCED,CAT_DEBUG' in p
assert 'model_items[model_count]=CATEGORY_ROW+CAT_CHEATS' in p and 'MODEL_HOTKEY' not in p
for key in ['memcard1','memcard2','analog_combo','analog_axis_modifier']:
 assert 'pcsx_rearmed_'+key in p,key
assert 'scope==BB_RESET_CONTROLS' in p and 'else if(model.game)' in p
assert 'ps1_settings_identity' in p and 'config_game_begin(name)' in p
shell=(r/'app/settings/settings_ui.c').read_text()
assert 'pcsx_rearmed_' not in shell and 'game_t' not in shell
assert 'for(unsigned i=0;i<bb_system_count' in shell
assert 'context->game?push(s,PAGE_SYSTEM,system)' in shell and 'ui_tabs' not in shell
assert 's->context.game?"THIS GAME"' in (r/'app/ui/components/settings_ui.inc').read_text()
gba=(r/'systems/gba/gba_settings_model.c').read_text()
for text in ['GBA_SET_SOLAR','GBA_SETTING_FIELDS','bb_profile_clear','gba_touch_setting_clear','U3I','U32','U33','GBA_SETTINGS_DEFAULT']:
 assert text in gba,text
assert '.cheats=&bb_legacy_cheats' in (r/'systems/ps1/sys_ps1.c').read_text()
assert 'room.h' not in p and 'breadbox_import.h' not in p and 'run_settings(' not in p
print('Settings B migration ledger: PS1 constructed pages + dynamic controls/cheats/theme; GBA fields + touch/profile + solar PASS')
