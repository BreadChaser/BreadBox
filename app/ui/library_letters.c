// The A-Z index of a system's games: every library presentation jumps and draws its letters from here.
#include "library_model.h"
#include "cold_code.h"

PSX3D_COLD int bb_library_letter(const char *title)
{
 unsigned char c=title ? (unsigned char)title[0] : 0;
 if(c>='a' && c<='z')c-='a'-'A';
 return c>='A' && c<='Z' ? c-'A'+1 : BB_LETTER_OTHER;
}
PSX3D_COLD void bb_library_letters(const bb_library_row *row, int first[BB_LETTERS])
{
 for(int i=0;i<BB_LETTERS;i++)first[i]=-1;
 for(int i=0;row && i<row->n;i++) {
  int letter=bb_library_letter(row->box[i].title);
  if(first[letter]<0)first[letter]=i;
 }
}
PSX3D_COLD int bb_library_letter_jump(const bb_library_row *row, int selected, int direction)
{
 if(!row || selected<0 || selected>=row->n || !direction)return selected;
 int first[BB_LETTERS];
 bb_library_letters(row,first);
 for(int letter=bb_library_letter(row->box[selected].title)+direction;letter>=0 && letter<BB_LETTERS;letter+=direction)
  if(first[letter]>=0)return first[letter];
 return selected;   // the first or last letter: stay (no wrap)
}
