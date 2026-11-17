PSX3D_COLD void look_list_draw(const flist_t *f,int n,int selected,float x,float y,float w,float rh,int visible,
 const char *(*label)(int,void *),const char *(*value)(int,void *),void *ud,bool (*heading)(int,void *))
{
 const theme_t *t=theme();const bb_look_style *s=look_style();
 C2D_Font font=libui_font();
 int first=(int)floorf(f->scroll);
 for(int i=first<UI_LOOK_ZERO?UI_LOOK_ZERO:first;i<n && i<=first+visible;i++) {
  float yy=roundf(y+(i-f->scroll)*rh);
  if(yy<y || yy+rh>y+visible*rh)continue;
  if(heading && heading(i,ud)) {
   ui_text_ex(NULL,x+UI_LOOK_ROW_INSET,yy-UI_LOOK_SECTION_RISE,UI_LIST_TEXT,t->section,UI_TEXT_ALIGN_LEFT,ui_fit(label(i,ud),UI_LIST_TEXT,w-UI_LOOK_ROW_INSETS));
   for(int c=UI_LOOK_ZERO;c<UI_LOOK_RULE_COLORS;c++)C2D_DrawRectSolid(x+UI_LOOK_ROW_INSET+c*UI_LOOK_RULE_STEP,yy+rh-UI_LOOK_RULE_BOTTOM,UI_LOOK_RULE_Z,UI_LOOK_RULE_WIDTH,UI_LOOK_RULE_HEIGHT,s->rule[c]);
   C2D_DrawRectSolid(x+UI_LOOK_RULE_TAIL,yy+rh-UI_LOOK_RULE_BOTTOM,UI_LOOK_RULE_Z,w-UI_LOOK_RULE_TAIL_INSET,UI_LOOK_RULE_HEIGHT,t->section_line);
   continue;
  }
  bool on=i==selected;
  if(on) {
   C2D_DrawRectangle(x,yy,UI_LIST_FOCUS_Z,w,rh,t->select_top,t->select_bottom,t->select_top,t->select_bottom);
   C2D_DrawRectSolid(x,yy,UI_LIST_EDGE_Z,UI_LOOK_FOCUS_EDGE,rh,s->row_edge);
  }
  const char *right=value?value(i,ud):NULL;
  float reserve=right && *right?ui_text_width(NULL,UI_LIST_TEXT,right)+UI_LOOK_VALUE_PAD:UI_LOOK_ZERO;
  if(reserve>w/UI_LOOK_TWO)reserve=w/UI_LOOK_TWO;
  float tx=x+UI_LOOK_ROW_INSET+(on?UI_LOOK_SELECTED_INDENT:UI_LOOK_ZERO),size=UI_LIST_TEXT;
  const char *text=ui_fit_ex(font,label(i,ud),size,size,w-(tx-x)-UI_LOOK_VALUE_INSET-reserve,&size);
  ui_text_ex(font,tx,yy+(rh-UI_LOOK_TEXT_EM*size)/UI_LOOK_TWO,size,on?t->text_bright:theme_row(t,UI_LOOK_ZERO,UI_ALPHA_OPAQUE),UI_TEXT_ALIGN_LEFT,text);
  if(right && *right)ui_text_ex(NULL,x+w-UI_LOOK_VALUE_INSET,yy+(rh-UI_LOOK_TEXT_EM*UI_LIST_TEXT)/UI_LOOK_TWO,UI_LIST_TEXT,t->value,C2D_AlignRight,ui_fit(right,UI_LIST_TEXT,reserve-UI_LOOK_VALUE_INSET));
 }
}
