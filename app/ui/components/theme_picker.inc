// Shared theme picker presentation: every theme, the Room's lighting with its picture (sun, moon) where
// the row's value goes; content/state are provided by its controller.
PSX3D_COLD void theme_picker_draw(const flist_t *list, int selected, int committed)
{
 const theme_t *th = theme();
 int count = theme_count();
 (void)committed;
 ui_trace_picker(list,count,selected,name);
 scene_draw(true);
 ui_list_draw(list, count, selected, name, NULL, NULL, th->accent[3], NULL);
 for(int i=0;i<count;i++) {
  float y=UI_LIST_TOP+(i-list->scroll)*UI_LIST_ROW;
  if(y<UI_LIST_TOP || y+UI_LIST_ROW>BAR_TOP || !theme_get(i)->icon)continue;
  u32 ink=i==selected?theme_row(th,1,UI_ALPHA_OPAQUE):theme_row(th,0,UI_ALPHA_OPAQUE);
  ui_icon_draw(theme_get(i)->icon,UI_PICKER_ICON_X,y+UI_LIST_ROW/2,UI_PICKER_ICON_SIZE,ink,th->card_bottom);
 }
 static const ui_button_t bar[] = {{KEY_B, "Cancel", UI_ICON_BACK, false}, {KEY_A, "Select", UI_ICON_CHECK, false}};
 ui_bar_draw(bar, 2);
}
