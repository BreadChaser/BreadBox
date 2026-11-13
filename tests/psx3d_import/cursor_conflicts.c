/* Exercise conflict/recovery policy through the production yielding cursor. */
#define main fixture_main
#include "check.c"
#undef main
static void *decoy;
static psx_import_result run_cursor(void)
{
 psx_import_cursor *c=psx_import_open(src,dst,false);assert(c);psx_import_result r;
 unsigned ticks=0;while(!psx_import_step(c,&r)){assert(++ticks<100000);assert(state_trailer(decoy,0x440000,NULL));}
 assert(!r.failed && psx_import_finish(c));psx_import_close(c);return r;
}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));strcpy(src,"source");setup(src);
 decoy=state(999);void *old=state(100),*new=state(200);put(src,"states/G.sta",new,0x440000);thumbnail_file(src,"states/G.sta.thm",200);
 for(int mode=0;mode<3;mode++) {
  snprintf(dst,sizeof(dst),"dest-%d",mode);setup(dst);
  put(dst,"states/G.sta",mode==0?old:new,0x440000);thumbnail_file(dst,"states/G.sta.thm",mode==0?100:200);
  if(mode==1){void *newer=state(300);put(dst,"states/G.sta",newer,0x440000);thumbnail_file(dst,"states/G.sta.thm",300);free(newer);}
  if(mode==2){put(src,"saves/G.id","first=SCUS12345\n",16);put(dst,"saves/G.id","first=SLUS54321\n",16);}
  char preserved[1024]={0};
  if(mode==0) {
   char h[1024],entry[1024];at(h,dst,"backups");history_set_root(h);
   assert(history_next("Other",HIST_SAVE,".sta",1,entry,sizeof(entry)));write_file(entry,old,0x440000);
   assert(snprintf(preserved,sizeof(preserved),"%s/import/psx3d/breadbox-history/backups/Other/%s/%u-%08x",dst,strrchr(entry,'/')+1,0x440000,state_crc32(old,0x440000,0))<(int)sizeof(preserved));
   /* Cancel while the cross-game safety snapshot is being copied. */
   psx_import_cursor *c=psx_import_open(src,dst,false);assert(c);psx_import_result partial;
   char tmp[1100];snprintf(tmp,sizeof(tmp),"%s.import-tmp",preserved);bool paused=false;
   for(unsigned tick=0;tick<100000 && !psx_import_step(c,&partial);tick++) {
    struct stat st;if(!stat(tmp,&st) && st.st_size>0){paused=true;break;}
   }
   assert(paused);psx_import_close(c);assert(crcfile(dst,"backups/Other/00000001-save-1.sta")==state_crc32(old,0x440000,0));
  }
  unsigned expected=crcfile(dst,"states/G.sta"),picture=crcfile(dst,"states/G.sta.thm");
  psx_import_result r=run_cursor();assert(r.conflicts);
  assert(crcfile(dst,"states/G.sta")== (mode==0?crcfile(src,"states/G.sta"):expected));
  assert(crcfile(dst,"states/G.sta.thm")== (mode==0?crcfile(src,"states/G.sta.thm"):picture));
  if(mode==0){hist_entry_t e[2];assert(history_list("Other",false,e,2)==0);assert(!access(preserved,F_OK));}
  r=run_cursor();assert(!r.copied && !r.conflicts);
 }
 /* A surviving BreadBox recovery copy is preserved before importing a missing
  * primary; the fast-empty path must not bypass that recovery. */
 strcpy(dst,"dest-recovery");setup(dst);put(dst,"states/G.sta.bak",old,0x440000);
 /* Remove the identity fixture from source to exercise ordinary recovery. */
 char p[1024];at(p,src,"saves/G.id");assert(!unlink(p));
 psx_import_result r=run_cursor();assert(r.conflicts && crcfile(dst,"states/G.sta")==crcfile(src,"states/G.sta"));
 free(decoy);free(old);free(new);puts("Yielding cursor conflicts, thumbnails, identity and recovery: PASS");
}
