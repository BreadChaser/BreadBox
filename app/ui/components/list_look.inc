// Shared list components: content and actions come from the home/library controllers.
static PSX3D_COLD void list_home_rect(const bb_home_picture *h, int i, float *x, float *y, float *w, float *height)
{
 (void)h;
 *x=UI_LOOK_TILE_X;*y=UI_LOOK_TILE_Y;*w=UI_LOOK_TILE_W;*height=UI_LOOK_HERO_H;
 if(i==UI_LOOK_ZERO)return;
 *y+=UI_LOOK_HERO_H+UI_LOOK_TILE_GAP;*height=UI_LOOK_MAIN_H;
 if(i==UI_LOOK_ONE)return;
 int cell=i-UI_LOOK_TWO;
 *y+=(UI_LOOK_MAIN_H+UI_LOOK_TILE_GAP)*(UI_LOOK_ONE+cell/UI_LOOK_TWO);
 *w=(UI_LOOK_TILE_W-UI_LOOK_TILE_GAP)/UI_LOOK_TWO;
 *x+=(cell%UI_LOOK_TWO)*(*w+UI_LOOK_TILE_GAP);
 *height=UI_LOOK_SMALL_H;
}
PSX3D_COLD void list_look_backdrop(bool bottom)
{
 const theme_t *t=theme();
 C2D_DrawRectSolid(UI_LOOK_ZERO,UI_LOOK_ZERO,UI_LOOK_TOP_Z,bottom ? UI_BOTTOM_SCREEN_WIDTH : UI_TOP_SCREEN_WIDTH,UI_SCREEN_HEIGHT,t->surface);
 if(!bottom && backdrop && theme_look_get(theme_look())->artwork)
  C2D_DrawImageAt(C2D_SpriteSheetGetImage(backdrop,UI_LOOK_ZERO),UI_LOOK_ZERO,UI_LOOK_ZERO,UI_LOOK_TOP_Z,NULL,UI_LOOK_ONE,UI_LOOK_ONE);
}
static PSX3D_COLD void list_title(const char *title, const char *line)
{
 const theme_t *t=theme();
 float size;ui_fit_line lines[UI_LOOK_TITLE_LINES];
 int count=ui_fit_lines(libui_font(),title,UI_LOOK_TITLE_SIZE,UI_TEXT_FLOOR,UI_LOOK_TOP_TEXT_WIDTH,UI_LOOK_TITLE_LINES,lines,&size);
 for(int i=UI_LOOK_ZERO;i<count;i++)
  ui_text_ex(libui_font(),UI_LOOK_TOP_CENTER,UI_LOOK_TITLE_Y+i*UI_LOOK_TITLE_STEP,size,t->game_title,C2D_AlignCenter,lines[i]);
 ui_text_ex(NULL,UI_LOOK_TOP_CENTER,UI_LOOK_PLAYED_Y,UI_LOOK_PLAYED_SIZE,t->game_sub,C2D_AlignCenter,
  ui_fit(line ? line : "",UI_LOOK_PLAYED_SIZE,UI_LOOK_TOP_TEXT_WIDTH));
}
static PSX3D_COLD void list_cover(int cover, float cx, float y, float size, bool dim)
{
 Tex3DS_SubTexture sub;
 C3D_Tex *tex=cover>=UI_LOOK_ZERO ? covers_get(cover,&sub) : NULL;
 if(!tex) {
  C2D_DrawRectSolid(cx-size/UI_LOOK_TWO,y,UI_LOOK_PANEL_Z,size,size,theme()->card_top);
  ui_icon_draw(UI_ICON_DISC,cx,y+size/UI_LOOK_TWO,UI_LOOK_ICON_SIZE,theme()->text_muted,theme()->card_top);
  return;
 }
 float scale=fminf(size/sub.width,size/sub.height),w=sub.width*scale,h=sub.height*scale;
 C2D_ImageTint tint;
 C2D_PlainImageTint(&tint,theme()->cover_dim ? theme()->cover_dim : theme()->surface,dim ? UI_LOOK_COVER_DIM : UI_LOOK_ZERO);
 C2D_DrawImageAt((C2D_Image){tex,&sub},cx-w/UI_LOOK_TWO,y+(size-h)/UI_LOOK_TWO,UI_LOOK_IMAGE_Z,&tint,scale,scale);
}
static PSX3D_COLD void list_cover_row(const int *covers, int n)
{
 // Queue the selection first; the neighbors are painted behind it.
 if(n && covers[UI_LOOK_ZERO]>=UI_LOOK_ZERO)covers_get(covers[UI_LOOK_ZERO],NULL);
 if(n>UI_LOOK_ONE && covers[UI_LOOK_ONE]!=UI_LOOK_NO_NEIGHBOR)list_cover(covers[UI_LOOK_ONE],UI_LOOK_TOP_CENTER-UI_LOOK_COVER_STEP,UI_LOOK_SIDE_Y,UI_LOOK_SIDE_SIZE,true);
 if(n>UI_LOOK_TWO && covers[UI_LOOK_TWO]!=UI_LOOK_NO_NEIGHBOR)list_cover(covers[UI_LOOK_TWO],UI_LOOK_TOP_CENTER+UI_LOOK_COVER_STEP,UI_LOOK_SIDE_Y,UI_LOOK_SIDE_SIZE,true);
 list_cover(n ? covers[UI_LOOK_ZERO] : -UI_LOOK_ONE,UI_LOOK_TOP_CENTER,UI_LOOK_COVER_Y,UI_LOOK_COVER_SIZE,false);
}
static PSX3D_COLD void list_picture(C3D_Tex *tex, float x, float y, float w, float h)
{
 static const Tex3DS_SubTexture sub={UI_LOOK_PICTURE_TEX,UI_LOOK_PICTURE_ROWS,UI_LOOK_ZERO,UI_LOOK_ONE,UI_LOOK_ONE,
  UI_LOOK_ONE-(float)UI_LOOK_PICTURE_ROWS/UI_LOOK_PICTURE_TEX};
 C2D_DrawImageAt((C2D_Image){tex,&sub},x,y,UI_LOOK_IMAGE_Z,NULL,w/UI_LOOK_PICTURE_TEX,h/UI_LOOK_PICTURE_ROWS);
}
PSX3D_COLD void list_look_home_top(const bb_home_picture *h)
{
 list_look_backdrop(false);
 const bb_home_place *p=bb_home_find(h->action);
 if(h->action==E_CONTINUE && h->picture)
  list_picture(h->picture,UI_LOOK_TOP_CENTER-UI_LOOK_PICTURE_W/UI_LOOK_TWO,UI_LOOK_COVER_Y,UI_LOOK_PICTURE_W,UI_LOOK_PICTURE_H);
 else if(h->action==E_LIBRARY || h->action==E_CONTINUE)list_cover_row(h->cover,h->covers);
 else if(p)ui_icon_draw(p->icon,UI_LOOK_TOP_CENTER,UI_LOOK_ICON_Y,UI_LOOK_ICON_SIZE,theme()->heading,theme()->surface);
 list_title(h->title,h->played);
}
PSX3D_COLD void list_look_footer(const ui_button_t *actions, int count)
{
 const theme_t *t=theme();
 C2D_DrawRectSolid(UI_LOOK_ZERO,BAR_TOP,UI_LOOK_PANEL_Z,UI_BOTTOM_SCREEN_WIDTH,UI_SCREEN_HEIGHT-BAR_TOP,t->bar ? t->bar : t->surface);
 for(int i=UI_LOOK_ZERO;i<count;i++) {
  float x=(float)UI_LOOK_FOOTER_WIDTH*i/count,w=(float)UI_LOOK_FOOTER_WIDTH/count;
  C2D_DrawRectSolid(x+UI_LOOK_BORDER,BAR_TOP+UI_LOOK_BORDER,UI_LOOK_PANEL_Z,w-UI_LOOK_TWO,UI_SCREEN_HEIGHT-BAR_TOP-UI_LOOK_TWO,t->card_top);
  if(actions[i].key==KEY_A)C2D_DrawRectSolid(x,BAR_TOP,UI_LOOK_MARK_Z,w,UI_SCREEN_HEIGHT-BAR_TOP,t->select_bottom);
  const char *keys=list_key(actions[i].key);
  ui_icon_draw(actions[i].icon,x+w/UI_LOOK_TWO,BAR_TOP+UI_LOOK_FOOTER_ICON_DY,UI_LOOK_FOOTER_ICON_SIZE,look_style()->icon,t->card_top);
  ui_text_ex(NULL,x+UI_LOOK_FOOTER_ICON_DY,UI_LOOK_FOOTER_GLYPH_Y,UI_LOOK_FOOTER_KEY_SIZE,t->text_bright,C2D_AlignCenter,keys);
  ui_text_ex(NULL,x+w/UI_LOOK_TWO,UI_LOOK_FOOTER_WORD_Y,UI_LOOK_FOOTER_FONT,t->text_bright,C2D_AlignCenter,
   ui_fit(actions[i].label,UI_LOOK_FOOTER_FONT,w-UI_LOOK_FOOTER_INSET));
 }
}
PSX3D_COLD void list_look_home_bottom(const bb_home_picture *h)
{
 list_look_backdrop(true);
 const theme_t *t=theme();
 for(int i=UI_LOOK_ZERO;i<home_count(h);i++) {
  const bb_home_place *p=home_place(h,i);
  float x,y,w,height;
  list_home_rect(h,i,&x,&y,&w,&height);
  bool on=p->action==h->action;
  C2D_DrawRectSolid(x,y,UI_LOOK_PANEL_Z,w,height,on ? t->select_bottom : t->card_top);
  C2D_DrawRectSolid(x,y,UI_LOOK_MARK_Z,on ? UI_LOOK_FOCUS_EDGE : UI_LOOK_BORDER,height,on ? look_style()->tile_edge : t->touch_border);
  C2D_DrawRectSolid(x,y,UI_LOOK_MARK_Z,w,UI_LOOK_BORDER,t->touch_border);
  C2D_DrawRectSolid(x,y+height-UI_LOOK_BORDER,UI_LOOK_MARK_Z,w,UI_LOOK_BORDER,t->touch_border);
  C2D_DrawRectSolid(x+w-UI_LOOK_BORDER,y,UI_LOOK_MARK_Z,UI_LOOK_BORDER,height,t->touch_border);
  float tx=x+UI_LOOK_TILE_INSET;
  if(p->action==E_CONTINUE && h->picture) {
   C2D_DrawRectSolid(tx-UI_LOOK_BORDER,y+UI_LOOK_TILE_TEXT_Y-UI_LOOK_BORDER,UI_LOOK_MARK_Z,UI_LOOK_THUMB_W+UI_LOOK_TWO,UI_LOOK_THUMB_H+UI_LOOK_TWO,look_style()->continue_edge);
   list_picture(h->picture,tx,y+UI_LOOK_TILE_TEXT_Y,UI_LOOK_THUMB_W,UI_LOOK_THUMB_H);
   tx+=UI_LOOK_THUMB_W+UI_LOOK_THUMB_GAP;
  }
  if(p->action==E_LIBRARY) {
   for(int c=UI_LOOK_TWO;c>=UI_LOOK_ZERO;c--)list_cover(c<h->covers?h->cover[c]:-UI_LOOK_ONE,tx+UI_LOOK_STACK_CENTER+c*UI_LOOK_STACK_STEP,y+UI_LOOK_STACK_TOP,UI_LOOK_STACK_SIZE,c!=UI_LOOK_ZERO);
   tx+=UI_LOOK_STACK_RESERVE;
  }
  float size;
  const char *title=ui_fit_ex(libui_font(),p->name,UI_LOOK_TILE_TEXT_SIZE,UI_TEXT_FLOOR,w-(tx-x)-UI_LOOK_TILE_INSET,&size);
  ui_text_ex(libui_font(),tx,y+UI_LOOK_TILE_TEXT_Y,size,t->text_bright,UI_LOOK_ZERO,title);
  if(i==UI_LOOK_ZERO && p->action==E_CONTINUE)
   ui_text_ex(NULL,tx,y+UI_LOOK_TILE_SUB_Y,UI_LOOK_TILE_SUB_SIZE,t->text_bright,UI_LOOK_ZERO,
    ui_fit(h->continue_title,UI_LOOK_TILE_SUB_SIZE,w-(tx-x)-UI_LOOK_TILE_INSET));
 }
 const ui_button_t action={KEY_A,"Open",UI_ICON_OPEN,false};
 list_look_footer(&action,UI_LOOK_ONE);
}
PSX3D_COLD void list_look_library_top(const shelf_view *v)
{
 list_look_backdrop(false);
 const shelf_box *b=focus_game(v);
 int row=v->level==SHELF_ALL ? v->hsel : v->row;
 const shelf_row *r=&v->rows[row];
 int covers[UI_LOOK_THREE],n=UI_LOOK_ZERO;
 if(b) {
  int i=(int)(b-r->box);
  covers[n++]=b->cover;
  covers[n++]=i>UI_LOOK_ZERO ? r->box[i-UI_LOOK_ONE].cover : UI_LOOK_NO_NEIGHBOR;
  covers[n++]=i+UI_LOOK_ONE<r->n ? r->box[i+UI_LOOK_ONE].cover : UI_LOOK_NO_NEIGHBOR;
 }
 list_cover_row(covers,n);
 char line[BB_LIBRARY_TITLE];
 if(v->level==SHELF_ALL)ui_format_games(line,sizeof(line),r->n);
 else played_line(b,line,sizeof(line));
 list_title(v->level==SHELF_ALL ? r->name : b ? b->title : r->name,line);
 if(v->level==SHELF_ONE && v->pto==UI_LOOK_ONE && v->side) {
  static const char *const side[]={"Front","Spine","Back","Edge"};
  ui_text_ex(NULL,UI_LOOK_TOP_CENTER,UI_LOOK_SIDE_LABEL_Y,UI_TEXT_FLOOR,theme()->game_sub,C2D_AlignCenter,side[v->side]);
 }
}
static PSX3D_COLD void list_held_strip(const shelf_view *v)
{
 const shelf_row *row=&v->rows[v->row];
 C2D_DrawRectSolid(UI_LOOK_TILE_X,UI_LOOK_STRIP_Y,UI_LOOK_PANEL_Z,UI_LOOK_TILE_W,UI_LOOK_STRIP_H,theme()->card_top);
 int previous=-UI_LOOK_ONE;
 for(int i=UI_LOOK_ZERO;i<row->n;i++) {
  int x=(int)shelf_scene_strip_position(row,row->box[i].x);
  bool selected=i==v->sel[v->row];
  if(x==previous && !selected)continue;
  previous=x;
  C2D_DrawRectSolid(x,UI_LOOK_STRIP_Y+(selected ? UI_LOOK_ZERO : UI_LOOK_STRIP_INSET),UI_LOOK_MARK_Z,
   selected ? UI_LOOK_FOCUS_EDGE : UI_LOOK_BORDER,UI_LOOK_STRIP_H-UI_LOOK_STRIP_INSET,
   selected ? look_style()->row_edge : theme()->text_muted);
 }
}
PSX3D_COLD void list_look_library_bottom(const shelf_view *v, const ui_button_t *actions, int count)
{
 list_look_backdrop(true);
 if(v->level==SHELF_ONE && (v->pto==UI_LOOK_ONE || v->pull>UI_LOOK_OUT_THRESHOLD)) {
  const shelf_box *b=focus_game(v);
  shelf_card card={.title=b ? b->title : "",.hint=UI_HINT_LR_TURN};
  shelf_scene_card(&card);list_held_strip(v);list_look_footer(actions,count);return;
 }
 flist_t *f=v->level==SHELF_ALL ? &systems_list : &games_list;
 int n=v->level==SHELF_ALL ? v->nrows : v->rows[v->row].n;
 int sel=v->level==SHELF_ALL ? v->hsel : v->sel[v->row];
 int visible_rows=UI_LIST_ROWS-(list_notice(v) ? 1 : 0);
 list_scroll(f,sel+UI_LOOK_ONE,n+UI_LOOK_ONE,visible_rows);
 look_list_draw(f,n+UI_LOOK_ONE,sel+UI_LOOK_ONE,UI_LOOK_LIST_X,UI_LIST_TOP,UI_LOOK_LIST_W,UI_LIST_ROW,visible_rows,
  row_label,row_value,(void *)v,row_heading);
 if(!n)ui_text_ex(NULL,UI_LOOK_TILE_INSET,UI_LOOK_EMPTY_Y,UI_TEXT_FLOOR,theme()->text_bright,UI_LOOK_ZERO,"No games here");
 if(list_notice(v)) {
  C2D_DrawRectSolid(UI_LOOK_ZERO,UI_LOOK_NOTICE_Y,UI_LOOK_IMAGE_Z,UI_BOTTOM_SCREEN_WIDTH,UI_LOOK_NOTICE_H,theme()->card_top);
  ui_text_ex(NULL,UI_LOOK_TILE_INSET,UI_LOOK_NOTICE_Y,UI_TEXT_FLOOR,theme()->text_bright,UI_LOOK_ZERO,ui_fit(v->notice,UI_TEXT_FLOOR,UI_LOOK_TILE_W));
 }
 list_look_footer(actions,count);
}
