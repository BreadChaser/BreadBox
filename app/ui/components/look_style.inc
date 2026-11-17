// The list looks' shared paint: title and section rules, the selection, the bar's words (look_style.h).
PSX3D_COLD void look_rule(float x,float y,float right,float stroke,float height)
{
 const bb_look_style *s=look_style();
 for(int c=UI_LOOK_ZERO;c<UI_LOOK_RULE_COLORS;c++)C2D_DrawRectSolid(x+c*(stroke+UI_LOOK_RULE_GAP),y,UI_LOOK_RULE_Z,stroke,height,s->rule[c]);
 float tail=x+UI_LOOK_RULE_COLORS*(stroke+UI_LOOK_RULE_GAP)+UI_LOOK_RULE_GAP;
 if(right>tail)C2D_DrawRectSolid(tail,y,UI_LOOK_RULE_Z,right-tail,UI_LOOK_RULE_HEIGHT,theme()->section_line);
}
PSX3D_COLD void look_select(float x,float y,float w,float h,float z)
{
 const theme_t *t=theme();
 C2D_DrawRectangle(x,y,z,w,h,t->select_top,t->select_bottom,t->select_top,t->select_bottom);
 C2D_DrawRectSolid(x,y,z+UI_Z_STEP,UI_LOOK_FOCUS_EDGE,h,look_style()->row_edge);
}
PSX3D_COLD void look_section(float x,float y,float w,float row_h,const char *label,u8 alpha)
{
 ui_text_ex(NULL,x+UI_LOOK_ROW_INSET,y+(row_h-UI_LOOK_TEXT_EM*UI_LIST_TEXT)/UI_LOOK_TWO-UI_LOOK_SECTION_RISE,UI_LIST_TEXT,
  theme_alpha(theme()->section,alpha),UI_TEXT_ALIGN_LEFT,ui_fit(label,UI_LIST_TEXT,w-UI_LOOK_ROW_INSETS));
 look_rule(x+UI_LOOK_ROW_INSET,y+row_h-UI_LOOK_RULE_BOTTOM,x+w-UI_LOOK_ROW_INSET,UI_LOOK_RULE_WIDTH,UI_LOOK_RULE_HEIGHT);
}
PSX3D_COLD void look_bar_word(const char *label,float cx,float w,u32 ink)
{
 if(label && *label)ui_text_ex(NULL,cx,UI_LOOK_BAR_WORD_Y,BAR_CAPTION,ink,C2D_AlignCenter,ui_fit(label,BAR_CAPTION,w-UI_LOOK_FOOTER_INSET));
}
PSX3D_COLD const char *look_bar_label(const ui_button_t *button)
{
 return button->label ? button->label : button->key==KEY_A ? "OK" : "";
}
PSX3D_COLD int look_bar_cells(const ui_button_t *buttons,const int *order,int count,float *x,float *w)
{
 float need[UI_BAR_MAX],total=UI_LOOK_ZERO,at=UI_LOOK_ZERO;
 for(int i=UI_LOOK_ZERO;i<count;i++) {
  float word=ui_text_width(NULL,BAR_CAPTION,look_bar_label(&buttons[order[i]]));
  need[i]=(word>BAR_ICON_R*UI_LOOK_TWO ? word : BAR_ICON_R*UI_LOOK_TWO)+UI_LOOK_FOOTER_INSET;
  total+=need[i];
 }
 float extra=count ? (UI_BOTTOM_SCREEN_WIDTH_F-total)/count : UI_LOOK_ZERO;
 for(int i=UI_LOOK_ZERO;i<count;i++){x[i]=at;w[i]=need[i]+extra;at+=w[i];}
 return count;
}
PSX3D_COLD void look_notice(const char *text,u32 ink)
{
 C2D_DrawRectSolid(UI_LOOK_ZERO,UI_LOOK_NOTICE_Y,UI_CARD_Z_NOTICE_LINE,UI_BOTTOM_SCREEN_WIDTH,UI_LOOK_NOTICE_H,theme()->card_top);
 ui_text_ex(NULL,UI_LOOK_TILE_INSET,UI_LOOK_NOTICE_Y+(UI_LOOK_NOTICE_H-UI_LOOK_TEXT_EM*UI_TEXT_FLOOR)/UI_LOOK_TWO,UI_TEXT_FLOOR,ink,UI_TEXT_ALIGN_LEFT,
  ui_fit(text,UI_TEXT_FLOOR,UI_BOTTOM_SCREEN_WIDTH-UI_LOOK_TILE_INSET*UI_LOOK_TWO));
}
