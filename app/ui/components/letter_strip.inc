// The letter strip's drawing (letter_strip.h): the letters in view, the current one lit, the empty ones dimmed.
PSX3D_COLD void letter_strip_draw(const bb_library_row *row, int selected, const letter_strip_place *at)
{
 if(!row || selected<UI_LOOK_ZERO || selected>=row->n)return;
 int first[BB_LETTERS],current=bb_library_letter(row->box[selected].title),from=window_first(current,at->shown);
 bb_library_letters(row,first);
 float cell=at->w/at->shown;
 for(int i=UI_LOOK_ZERO;i<at->shown;i++) {
  int letter=from+i;
  char name[UI_LOOK_TWO]={letter==BB_LETTER_OTHER ? '#' : (char)('A'+letter-UI_LOOK_ONE),UI_LOOK_ZERO};
  float x=at->x+cell*i;
  if(letter==current) {
   if(at->list) {   /* lit and underlined (the early library concept) */
    C2D_DrawRectSolid(x,at->y,at->z,cell,at->h,theme()->select_top);
    C2D_DrawRectSolid(x,at->y+at->h-UI_LOOK_HEAD_RULE_H,at->z,cell,UI_LOOK_HEAD_RULE_H,look_style()->row_edge);
   }
   else {   // the Room: a ring of ink round the letter
    C2D_DrawRectSolid(x,at->y,at->z,cell,UI_LOOK_BORDER,at->ink);C2D_DrawRectSolid(x,at->y+at->h-UI_LOOK_BORDER,at->z,cell,UI_LOOK_BORDER,at->ink);
    C2D_DrawRectSolid(x,at->y,at->z,UI_LOOK_BORDER,at->h,at->ink);C2D_DrawRectSolid(x+cell-UI_LOOK_BORDER,at->y,at->z,UI_LOOK_BORDER,at->h,at->ink);
   }
  }
  at->text(NULL,x+cell/UI_LOOK_TWO,at->text_y,at->size,first[letter]<UI_LOOK_ZERO ? at->dim : at->ink,C2D_AlignCenter,name);
 }
}
