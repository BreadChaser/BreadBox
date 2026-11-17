// Shared theme_picker presentation: the room styles, each with its picture (sun, moon) where
// the row's value goes; content/state are provided by its controller.
static PSX3D_COLD void theme_picker_paint(const flist_t *list, int selected, int committed, bool look, int count) {
 const theme_t *th = theme();
 ui_trace_picker(list,count,selected,look,name);
 scene_draw(true);
 ui_list_draw(list, count, selected, name, NULL, look ? (void *)list : NULL, th->accent[3], NULL);
 for(int i=0;i<count;i++) {
  float y=UI_LIST_TOP+(i-list->scroll)*UI_LIST_ROW;
  if(y<UI_LIST_TOP || y+UI_LIST_ROW>196)continue;
  u32 ink=i==selected?theme_row(th,1,UI_ALPHA_OPAQUE):theme_row(th,0,UI_ALPHA_OPAQUE);
  if(!look)ui_icon_draw(theme_get(i)->icon,286+7,y+UI_LIST_ROW/2,24,ink,th->card_bottom);
 }
 ui_list_context(look ? "Look" : "Room style",selected==committed?"Current":"Preview");
    ui_button_t bar[] = {{KEY_B, "Cancel", 0, false}, {KEY_A, "Select", UI_ICON_CHECK, false}};
    ui_bar_draw(bar, 2);
}
