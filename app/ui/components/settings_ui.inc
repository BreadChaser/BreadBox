/* Shared card/tag composition. Settings adapters never draw frontend surfaces. */
static void card_row(int index,void *ud,ui_card_row_t *out)
{
 settings_state *s=ud;bb_settings_row *r=&s->rows[index].row;bool head=r->kind==BB_ROW_HEADING;
 *out=(ui_card_row_t){.label=r->label,.value=head?NULL:r->value,.source=r->source,.tone=UI_TONE_SOFT,
  .heading=head,.arrows=r->kind==BB_ROW_VALUE};
}
/* The card's frame: the current page's title on the red rule (a game's own Settings: the game). */
static void settings_card(settings_state *s,ui_card_t *card)
{
 *card=(ui_card_t){.eyebrow=s->context.game?"THIS GAME":"SETTINGS",.title=page_title(s),
  .notice=s->count && *s->rows[level(s)->selected].row.notice?s->rows[level(s)->selected].row.notice:""};   /* a real notice only */
}
static void draw_bottom(void *ud)
{
 settings_state *s=ud;settings_level *l=level(s);
 ui_card_t card;settings_card(s,&card);
 ui_card_draw(&s->list,s->count,l->selected,card_row,s,&card);
 ui_button_t bar[UI_BAR_MAX];int nbar=settings_bar(s,bar);ui_bar_draw(bar,nbar);
}
/* Where you are, on the oscilloscope's glass in the room: "Settings", "Customize", "PlayStation · Picture"
   (on a system's list, the section of the focused row: its heading may have scrolled away). With short names
   the system is its abbreviation ("GBA · Cartridge"): the glass takes about 22 characters. */
static const char *settings_path(settings_state *s,bool short_names)
{
 const settings_level *l=level(s);
 const char *section=s->count && l->selected<s->count?s->rows[l->selected].row.section:"";
 bool named=section && *section && strcmp(section,"More");
 const char *name=l->system?(short_names && l->system->short_name?l->system->short_name:l->system->name):"";
 if(l->page==PAGE_SYSTEM && l->system && named)snprintf(s->scope,sizeof(s->scope),"%s · %s",name,section);
 else if(l->page==PAGE_ADVANCED && l->system)snprintf(s->scope,sizeof(s->scope),"%s · %s",name,named?section:"Advanced");
 else snprintf(s->scope,sizeof(s->scope),"%s",page_title(s));
 return s->scope;
}
/* The path for a glass gw wide: the full names when they fit at the context floor, else the short ones. */
static const char *settings_path_fit(settings_state *s,float gw)
{
 const char *path=settings_path(s,false);
 return ui_text_width(NULL,UI_FONT_CONTEXT,path)<=gw?path:settings_path(s,true);
}
static void settings_tag(void *ud)
{
 settings_state *s=ud;
 bool scope=!((s->context.game && !s->context.running) || (s->scene && s->scene_visibility<=UI_METRIC_0));
 if(scope && s->scene) {
  float gx=UI_SETTINGS_SCOPE_X,gy=UI_SETTINGS_SCOPE_Y,gw=UI_SETTINGS_SCOPE_W,cx,cy,w;
  if(room_scene_anchor_screen("slabel",&cx,&cy,&w)){gx=cx;gy=cy;gw=fminf(w*UI_SETTINGS_SCOPE_SPAN,UI_SETTINGS_SCOPE_MAX_W);}
  const char *path=settings_path_fit(s,gw);
  float size=UI_SETTINGS_SCOPE_SIZE,sw=ui_text_width(NULL,size,path);
  if(sw>gw)size=fmaxf(UI_FONT_CONTEXT,size*gw/sw);
  ui_text_ex(NULL,gx,gy-size*UI_METRIC_15,size,
   theme_alpha(UI_COLOR_SCOPE_PHOSPHOR,(int)(UI_METRIC_255*s->scene_visibility)),C2D_AlignCenter,
   ui_fit(path,size,gw));
 }
 /* No hang tag: it repeated the focused row's name and a sentence saying the same (owner 2026-11-05).
    START (or the Help row) opens the full explanations. Only a row the card cuts ("...": a long folder,
    a key combo) shows its whole text here (review 2026-11-05). */
 if(s->count<=UI_METRIC_0)return;
 ui_card_row_t r;card_row(level(s)->selected,s,&r);
 if(ui_card_row_cut(&r))ui_value_card(r.label,r.value,s->value_page);
}
static void draw_top(void *ud)
{
 settings_state *s=ud;ui_destination_draw_top(s->scene?s->scene:&s->backdrop,settings_tag,s);
}
static void appearance_bottom(void *ud)
{
 settings_state *s=ud;theme_picker_draw(s->appearance_list,s->appearance_selected,s->appearance_previous);
 static const ui_button_t bar[]={{KEY_B,"Cancel",UI_ICON_BACK,false},{KEY_A,"Select",UI_ICON_CHECK,false}};
 ui_bar_draw(bar,UI_METRIC_2);
}
void settings_appearance_present(settings_state *s,flist_t *list,int selected,int previous)
{
 s->appearance_list=list;s->appearance_selected=selected;s->appearance_previous=previous;
 ui_begin();video_present_vsync(false,draw_top,appearance_bottom,s);
}
