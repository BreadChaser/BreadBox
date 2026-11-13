#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gba_boxart.h"
int main(int argc,char **argv)
{
 char names[BOXART_MAX_CANDIDATES][BOXART_NAME];
 if(argc>1) {int n=gba_boxart_candidates(argv[1],argc>2?argv[2]:"",names,BOXART_MAX_CANDIDATES);for(int i=0;i<n;i++)puts(names[i]);return 0;}
 int n=gba_boxart_candidates("roms/Kirby & The Amazing Mirror (USA).gba","",names,3);
 assert(n==1 && !strcmp(names[0],"Kirby _ The Amazing Mirror (USA)"));
 n=gba_boxart_candidates("Classic NES Series - Super Mario Bros. (USA, Europe).gba","",names,3);
 assert(n==1 && !strcmp(names[0],"Classic NES Series - Super Mario Bros. (USA, Europe)"));
 n=gba_boxart_candidates("Classic NES Series - The Legend of Zelda (USA, Europe).gba","",names,3);
 assert(n==1 && !strcmp(names[0],"Classic NES Series - The Legend of Zelda (USA, Europe)"));
 n=gba_boxart_candidates("renamed.gba","B8KE",names,3);
 assert(n>1 && strstr(names[1],"Kirby"));
 n=gba_boxart_candidates(getenv("GBA_TEST_ZIP"),"",names,3);
 assert(n==2 && !strcmp(names[0],"Kirby _ The Amazing Mirror (USA)") && !strcmp(names[1],"archive"));
 puts("GBA cover mapping PASS: punctuation, Classic NES, cartridge code, ZIP inner name");
}
