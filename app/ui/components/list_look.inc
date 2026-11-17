// Shared list components: content and actions come from the home/library controllers.
/* The tiles as PSX3D v0.5.2's main menu: a tall first tile (Continue, or Collection without it), Collection under it,
   then the other places two by two. */
static PSX3D_COLD void list_home_rect(const bb_home_picture *h, int i, float *x, float *y, float *w, float *height)
{
 /* Without Continue the first tile also takes Continue's row (PSX3D v0.5.2's tall Library). */
 float hero=h->have_continue ? UI_LOOK_HERO_H : UI_LOOK_HERO_H+UI_LOOK_MAIN_H+UI_LOOK_TILE_GAP;
 *x=UI_LOOK_TILE_X;*y=UI_LOOK_TILE_Y;*w=UI_LOOK_TILE_W;*height=hero;
 if(i==UI_LOOK_ZERO)return;
 *y+=hero+UI_LOOK_TILE_GAP;
 int full=h->have_continue ? UI_LOOK_TWO : UI_LOOK_ONE;   /* the tiles that take a whole row */
 if(i<full){*height=UI_LOOK_MAIN_H;return;}
 if(full==UI_LOOK_TWO)*y+=UI_LOOK_MAIN_H+UI_LOOK_TILE_GAP;
 int cell=i-full;
 *y+=(UI_LOOK_SMALL_H+UI_LOOK_TILE_GAP)*(cell/UI_LOOK_TWO);
 *w=(UI_LOOK_TILE_W-UI_LOOK_TILE_GAP)/UI_LOOK_TWO;
 *x+=(cell%UI_LOOK_TWO)*(*w+UI_LOOK_TILE_GAP);
 *height=UI_LOOK_SMALL_H;
}
PSX3D_COLD void list_look_backdrop(bool bottom)
{
 const theme_t *t=theme();
 /* PSX3D's top screen is its moving scene; without memory for it, and everywhere else, the plain page colour. */
 if(!bottom && theme_look_get(theme_look())->artwork && list_scene_draw())return;
 C2D_DrawRectSolid(UI_LOOK_ZERO,UI_LOOK_ZERO,UI_LOOK_TOP_Z,bottom ? UI_BOTTOM_SCREEN_WIDTH : UI_TOP_SCREEN_WIDTH,UI_SCREEN_HEIGHT,t->surface);
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
 else if(p && !icons_draw(ICON_SLOT_TOP,place_picture(p),UI_LOOK_TOP_CENTER,UI_LOOK_ICON_Y,UI_LOOK_PICTURE_ICON,true,UI_LOOK_ONE,UI_LOOK_IMAGE_Z))
  ui_icon_draw(p->icon,UI_LOOK_TOP_CENTER,UI_LOOK_ICON_Y,UI_LOOK_ICON_SIZE,theme()->heading,theme()->surface);
 list_title(h->title,h->played);
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
  float tx=x+UI_LOOK_TILE_INSET,ty=y+(height-UI_LOOK_TILE_LINE)/UI_LOOK_TWO;
  const char *sub[UI_LOOK_TWO]={NULL,NULL};
  if(p->action==E_CONTINUE) {   /* the picture, "Continue", the game and when it was left */
   float py=y+(height-UI_LOOK_THUMB_H)/UI_LOOK_TWO;
   C2D_DrawRectSolid(tx-UI_LOOK_BORDER,py-UI_LOOK_BORDER,UI_LOOK_MARK_Z,UI_LOOK_THUMB_W+UI_LOOK_TWO,UI_LOOK_THUMB_H+UI_LOOK_TWO,look_style()->continue_edge);
   if(h->picture)list_picture(h->picture,tx,py,UI_LOOK_THUMB_W,UI_LOOK_THUMB_H);
   tx+=UI_LOOK_THUMB_W+UI_LOOK_THUMB_GAP;sub[UI_LOOK_ZERO]=h->continue_title;sub[UI_LOOK_ONE]=h->continue_when;
  } else if(p->action==E_LIBRARY) {   /* a stack of covers and the count */
   for(int c=UI_LOOK_TWO;c>=UI_LOOK_ZERO;c--)list_cover(c<h->covers?h->cover[c]:-UI_LOOK_ONE,tx+UI_LOOK_STACK_CENTER+c*UI_LOOK_STACK_STEP,y+(height-UI_LOOK_STACK_SIZE)/UI_LOOK_TWO,UI_LOOK_STACK_SIZE,c!=UI_LOOK_ZERO);
   tx+=UI_LOOK_STACK_RESERVE;sub[UI_LOOK_ZERO]=h->games;
  } else {   /* the place's icon beside its name */
   if(!icons_draw(on?ICON_SLOT_TILE:ICON_STILL,place_picture(p),tx+UI_LOOK_TILE_ICON/UI_LOOK_TWO,y+height/UI_LOOK_TWO,UI_LOOK_TILE_ICON,on,UI_LOOK_ONE,UI_LOOK_IMAGE_Z))
    ui_icon_draw(p->icon,tx+UI_LOOK_TILE_ICON/UI_LOOK_TWO,y+height/UI_LOOK_TWO,UI_LOOK_TILE_ICON,on?t->text_bright:look_style()->icon,on?t->select_bottom:t->card_top);
   tx+=UI_LOOK_TILE_ICON+UI_LOOK_THUMB_GAP;
  }
  int lines=UI_LOOK_ONE+(sub[UI_LOOK_ZERO]&&*sub[UI_LOOK_ZERO])+(sub[UI_LOOK_ONE]&&*sub[UI_LOOK_ONE]);
  ty=y+(height-lines*UI_LOOK_TILE_LINE)/UI_LOOK_TWO;
  float room=w-(tx-x)-UI_LOOK_TILE_INSET;
  ui_text_ex(NULL,tx,ty,UI_TEXT_FLOOR,t->text_bright,UI_LOOK_ZERO,ui_fit(p->name,UI_TEXT_FLOOR,room));
  for(int k=UI_LOOK_ZERO;k<UI_LOOK_TWO;k++)if(sub[k] && *sub[k]){ty+=UI_LOOK_TILE_LINE;ui_text_ex(NULL,tx,ty,UI_TEXT_FLOOR,t->game_sub,UI_LOOK_ZERO,ui_fit(sub[k],UI_TEXT_FLOOR,room));}
 }
 /* No action bar: the tiles take the whole screen and a tap opens one (PSX3D v0.5.2's main menu). */
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
}
PSX3D_COLD void list_look_library_bottom(const shelf_view *v, const ui_button_t *actions, int count)
{
 list_look_backdrop(true);
 flist_t *f=v->level==SHELF_ALL ? &systems_list : &games_list;
 int n=v->level==SHELF_ALL ? v->nrows : v->rows[v->row].n;
 int sel=v->level==SHELF_ALL ? v->hsel : v->sel[v->row];
 letter_strip_place letters;
 int strip=list_letters(v,&letters);   /* A to Z: the letters' own row under the section's */
 int visible_rows=UI_LIST_ROWS-UI_LOOK_ONE-strip-(list_notice(v) ? UI_LOOK_ONE : UI_LOOK_ZERO),top=UI_LIST_TOP+UI_LIST_ROW*(UI_LOOK_ONE+strip);
 look_section(UI_LOOK_LIST_X,UI_LIST_TOP,UI_LOOK_LIST_W,UI_LIST_ROW,section_label(v),UI_ALPHA_OPAQUE);
 if(strip)letter_strip_draw(&v->rows[v->row],sel,&letters);
 list_scroll(f,sel,n,visible_rows);
 flist_draw_sections(f,n,sel,UI_LOOK_LIST_X,top,UI_LOOK_LIST_W,UI_LIST_ROW,visible_rows,
  row_label,row_value,(void *)v,look_style()->row_edge,NULL);
 if(n>visible_rows)
  C2D_DrawRectSolid(UI_LOOK_SCROLL_X,top+UI_LIST_ROW*visible_rows*f->scroll/n,UI_LOOK_PANEL_Z,UI_LOOK_SCROLL_W,
   UI_LIST_ROW*visible_rows*visible_rows/(float)n,theme()->scrollbar);
 if(!n)ui_text_ex(NULL,UI_LOOK_TILE_INSET,UI_LOOK_EMPTY_Y,UI_TEXT_FLOOR,theme()->text_bright,UI_LOOK_ZERO,"No games here");
 if(list_notice(v)) {
  C2D_DrawRectSolid(UI_LOOK_ZERO,UI_LOOK_NOTICE_Y,UI_LOOK_IMAGE_Z,UI_BOTTOM_SCREEN_WIDTH,UI_LOOK_NOTICE_H,theme()->card_top);
  ui_text_ex(NULL,UI_LOOK_TILE_INSET,UI_LOOK_NOTICE_Y,UI_TEXT_FLOOR,theme()->text_bright,UI_LOOK_ZERO,ui_fit(v->notice,UI_TEXT_FLOOR,UI_LOOK_TILE_W));
 }
 ui_bar_draw(actions,count);
}
