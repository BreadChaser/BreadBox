/* Shared binding and cheat child cards. */
static bb_setting_source feature_source(int index)
{
 if(!controls)return BB_SOURCE_NONE;
 unsigned row=bb_controls_row_at(controls,(unsigned)index);
 return controls->profile.game_path[0] && (controls->profile.overrides&(1<<row))?BB_SOURCE_CHANGED:BB_SOURCE_SYSTEM;
}
static void feature_top(void *unused)
{
 (void)unused;
 /* from the pause card: the paused game, the focused cheat's whole name (or a notice) on its tag */
 if(feature_game && bb_pause_scene_top(feature_notice?feature_notice:feature_full()))return;
 if(!bb_settings_child_top())libui_draw_backdrop();
 /* a tag only for a real notice (no repeated row name, no helper lines) */
 if(!bb_settings_child_tag_visible() || !feature_notice)return;
 ui_tag_t tag={.title=feature_notice,.hot=0};
 ui_tag(&tag,UI_SETTINGS_TAG_X,UI_SETTINGS_TAG_Y);
}
static void feature_card_row(int index,void *unused,ui_card_row_t *out)
{
 (void)unused;*out=(ui_card_row_t){.label=feature_label(index,NULL),.value=feature_value(index,NULL),
  .source=feature_source(index),.arrows=controls || index<cheat_ops->count()};
}
static void feature_bottom(void *unused)
{
 (void)unused;ui_begin();
 /* no standing helper line: the bar says what A and X do (it contradicted them, and was cut at the foot) */
 ui_card_t card={.eyebrow="SETTINGS",.title=controls?"Button mappings":"Cheats",.notice=feature_notice};
 ui_card_draw(&feature_list,controls?(int)bb_controls_rows_shown(controls):cheat_ops->count()+1,feature_row,feature_card_row,NULL,&card);
 ui_bar_draw(feature_bar,3);
}
