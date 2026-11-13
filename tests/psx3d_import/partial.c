/* Optional integration gate on a COPY of the Old's 16410 partial destination.
 * No fixture is downloaded or edited in place. argv[3] is remaining copies. */
#define main fixture_main
#include "check.c"
#undef main
int main(int argc,char **argv)
{
 assert(argc==4);unsigned expected=atoi(argv[3]);
 for(unsigned pass=0;pass<2;pass++){
  psx_import_cursor *c=psx_import_open(argv[1],argv[2],false);assert(c);
  psx_import_result r;unsigned ticks=0;while(!psx_import_step(c,&r))assert(++ticks<1000000);
  assert(!r.failed && !r.rejected && !r.interrupted);
  printf("Partial resume pass=%u copied=%u unchanged=%u conflicts=%u rejected=%u failed=%u\n",pass,r.copied,r.unchanged,r.conflicts,r.rejected,r.failed);
  assert(r.copied==(pass?0:expected));assert(psx_import_finish(c));psx_import_close(c);
 }
}
