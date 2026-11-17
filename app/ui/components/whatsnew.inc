/* Release notes use the shared ruled card and its ordinary action bar. */
static PSX3D_COLD void notes_paper(flist_t *list,int rows,int selected,const ui_card_t *card,
 const ui_button_t *buttons,int nbuttons)
{
 ui_card_draw(list,rows,selected,row,current,card);
 ui_bar_draw(buttons,nbuttons);
}
