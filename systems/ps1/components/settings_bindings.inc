static int binding_bar(settings_ui_t *s,ui_button_t *bar)
{
 int n=0;bar[n++]=(ui_button_t){KEY_B,"Back",UI_ICON_BACK,false};
 if(s->game)bar[n++]=(ui_button_t){KEY_X,"Inherit",UI_ICON_RESET,false};
 bar[n++]=(ui_button_t){KEY_Y,"Reset",UI_ICON_RESET,false};
 bar[n++]=(ui_button_t){KEY_A,"Change",UI_ICON_CHANGE,false};return n;
}
/* PlayStation binding data, shared card/tag presentation. */
static void binding_row(int index,void *ud,ui_card_row_t *out)
{
 settings_ui_t *s=ud;int item=s->items[index],row=item-CONTROL_ROW;
 *out=(ui_card_row_t){.label=input_control_label(row,NULL),.value=input_control_value(row,NULL),
  .arrows=true,.source=config_control_overridden(row)?BB_SOURCE_CHANGED:preset_row(s,item)?BB_SOURCE_RECOMMENDED:BB_SOURCE_SYSTEM};
}
static void binding_bottom(void *ud)
{
 settings_ui_t *s=ud;
 ui_card_t card={.eyebrow=s->game?"THIS GAME":"PLAYSTATION",.title="Button mappings",
  .notice=s->game?"Left/Right Change · X Inherit · Y Reset controls":"Left/Right Change · Y Reset controls"};
 ui_card_draw(&s->list,s->count,s->selected[CAT_BINDINGS],binding_row,s,&card);
 ui_button_t bar[UI_BAR_MAX];int nbar=binding_bar(s,bar);ui_bar_draw(bar,nbar);
}
static void binding_top(void *ud)
{
 /* no tag: it repeated the row's name (owner 2026-11-05) */
 (void)ud;bb_settings_child_top();
}
