// The A-Z index: letters, "#", empty letters skipped, one game, and the ends stop (no wrap).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "library_model.h"
#include "../../app/ui/library_letters.c"
#include "shelf_input.h"
static bb_library_game games[16];
static bb_library_row row={.box=games};
static void titles(const char *const *t, int n)
{
 memset(games,0,sizeof(games));row.n=n;
 for(int i=0;i<n;i++)snprintf(games[i].title,sizeof(games[i].title),"%s",t[i]);
}
int main(void)
{
 assert(bb_library_letter("ape")==1 && bb_library_letter("Zork")==26 && bb_library_letter("007 Racing")==BB_LETTER_OTHER);
 assert(bb_library_letter("[Hidden]")==BB_LETTER_OTHER && bb_library_letter("")==BB_LETTER_OTHER && bb_library_letter("\xe3\x81\x82")==BB_LETTER_OTHER);
 /* A sorted list with gaps (no B, D-Z only W) and a "#" group in front. */
 static const char *const list[]={"007 Racing","3Xtreme","Alundra","Ape Escape","Castlevania","Crash Bandicoot","Crash Team Racing","Wipeout"};
 titles(list,8);
 int first[BB_LETTERS];bb_library_letters(&row,first);
 assert(first[0]==0 && first[1]==2 && first[2]==-1 && first[3]==4 && first[23]==7 && first[26]==-1);
 assert(bb_library_letter_jump(&row,0,1)==2);    /* # -> A */
 assert(bb_library_letter_jump(&row,3,1)==4);    /* A (second game) -> C: B is empty */
 assert(bb_library_letter_jump(&row,5,1)==7);    /* C -> W: D-V are empty */
 assert(bb_library_letter_jump(&row,7,1)==7);    /* W is the last letter: stop */
 assert(bb_library_letter_jump(&row,7,-1)==4);   /* W -> C */
 assert(bb_library_letter_jump(&row,6,-1)==2);   /* C (inside) -> A */
 assert(bb_library_letter_jump(&row,2,-1)==0);   /* A -> # */
 assert(bb_library_letter_jump(&row,1,-1)==1);   /* # is the first letter: stop */
 /* One game: nowhere to go either way. */
 static const char *const one[]={"Kirby"};titles(one,1);
 assert(bb_library_letter_jump(&row,0,1)==0 && bb_library_letter_jump(&row,0,-1)==0);
 /* No games, a bad selection, no direction. */
 row.n=0;bb_library_letters(&row,first);for(int i=0;i<BB_LETTERS;i++)assert(first[i]<0);
 assert(bb_library_letter_jump(&row,0,1)==0 && bb_library_letter_jump(NULL,3,1)==3);
 titles(list,8);assert(bb_library_letter_jump(&row,4,0)==4 && bb_library_letter_jump(&row,-1,1)==-1);
 /* L / R: the press acts at once, a hold repeats after .4 s every .1 s, both together or neither nothing. */
 shelf_repeat r={0};
 assert(shelf_shoulder_step(&r,KEY_R,0,0)==KEY_R && !shelf_shoulder_step(&r,0,KEY_R,.2f) && shelf_shoulder_step(&r,0,KEY_R,.41f)==KEY_R);
 assert(!shelf_shoulder_step(&r,0,KEY_R,.45f) && shelf_shoulder_step(&r,0,KEY_R,.52f)==KEY_R);
 assert(shelf_shoulder_step(&r,KEY_L,KEY_R,.53f)==0 && !shelf_shoulder_step(&r,0,0,.6f) && shelf_shoulder_step(&r,KEY_L,0,.61f)==KEY_L);
 assert(shelf_shoulder_step(&r,KEY_L,0,.62f)==KEY_L);   /* a new press is never held back by the repeat */
 puts("PASS: A-Z index: #, letters, empty letters skipped, one game, both ends stop");
 return 0;
}
