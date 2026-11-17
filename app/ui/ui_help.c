// The shared long-help pager.
#include <string.h>
#include <stdio.h>
#include <citro2d.h>
#include <3ds.h>
#include "ui.h"
#include "ui_pause.h"
#include "ui_style.h"
#include "cold_code.h"

/* Shared long-help dialog. Bound each page and split only at a word boundary. */
PSX3D_COLD void ui_help_pages(const char *title,const char *text,bool show_game)
{
 size_t offsets[UI_HELP_MAX_PAGES+1]={0};unsigned pages=0;
 size_t length=strlen(text),position=0;
 do {
  size_t end=position+UI_HELP_PAGE_CHARS;
  if(end>length)end=length;
  else {
   size_t word=end;while(word>position && text[word]!=' ' && text[word]!='\n')word--;
   if(word>position)end=word;
   else while(end>position && ((unsigned char)text[end]&UI_TEXT_UTF8_MASK)==UI_TEXT_UTF8_CONT)end--;
  }
  offsets[++pages]=end;position=end;
 } while(position<length && pages<UI_HELP_MAX_PAGES);
 for(unsigned page=0;page<pages;) {
  char body[UI_HELP_PAGE_CHARS+1],head[160];
  size_t n=offsets[page+1]-offsets[page];memcpy(body,text+offsets[page],n);body[n]=0;
  snprintf(head,sizeof(head),"%s %u/%u",title,page+1,pages);
  bool next=ui_confirm_choice(head,body,page+1==pages?"A  Done":"A  Next",page?"B  Back":"B  Close",show_game);
  if(!aptMainLoop())return;
  if(next)page++;else if(page)page--;else return;
 }
}
