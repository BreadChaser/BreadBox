/* The Old 3DS has no ZL / ZR: the shared Controls list leaves those two rows out there, the New keeps them. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bb_controls.h"

int main(void)
{
 static bb_controls c;
 memset(&c,0,sizeof(c));
 for(unsigned i=0;i<BB_CONTROL_ROWS;i++)c.schema[i].label=i==6?"ZL button":i==7?"ZR button":"other";
 c.profile.schema=c.schema;

 c.profile.model=BB_NEW;
 assert(bb_controls_rows_shown(&c)==BB_CONTROL_ROWS);
 for(unsigned i=0;i<BB_CONTROL_ROWS;i++)assert(bb_controls_row_at(&c,i)==i);

 c.profile.model=BB_OLD;
 assert(bb_controls_rows_shown(&c)==BB_CONTROL_ROWS-2);
 assert(!bb_controls_row_shown(&c,6) && !bb_controls_row_shown(&c,7));
 for(unsigned i=0;i<bb_controls_rows_shown(&c);i++) {
  unsigned row=bb_controls_row_at(&c,i);
  assert(row!=6 && row!=7 && row<BB_CONTROL_ROWS);
  assert(strcmp(c.schema[row].label,"ZL button") && strcmp(c.schema[row].label,"ZR button"));
  assert(row==(i<6?i:i+2));          /* A..R, then Start on: the order and every other row are unchanged */
 }
 assert(bb_controls_row_at(&c,bb_controls_rows_shown(&c)-1)==BB_CONTROL_LOAD);   /* the hotkey rows stay reachable */
 puts("bb_controls_rows: ok");
 return 0;
}
