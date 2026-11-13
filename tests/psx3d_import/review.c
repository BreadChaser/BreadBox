/* Independent review scenarios, using real backend/loader/history and short writes. */
#define main original_import_tests
#include "check.c"
#undef main
static bool has(const char *rel) {char p[1024];at(p,dst,rel);return !access(p,F_OK);}
static void no_partials(const char *root)
{
 DIR *d=opendir(root);assert(d);struct dirent *e;
 while((e=readdir(d)))if(e->d_name[0]!='.') {
  assert(!strstr(e->d_name,".import-tmp"));
  char p[1024];struct stat st;at(p,root,e->d_name);assert(!lstat(p,&st));
  if(S_ISDIR(st.st_mode))no_partials(p);
 }
 closedir(d);
}
int main(int argc,char **argv)
{
 assert(argc==3 && !chdir(argv[1]));int mode=atoi(argv[2]);
 strcpy(src,"PSX3D");setup(src);strcpy(dst,"BreadBox");setup(dst);
 void *new=state(200),*old=state(100);psx_import_result r;
 if(mode==1 || mode==2 || mode==6) {
  put(src,"states/G.sta",new,0x440000);thumbnail_file(src,"states/G.sta.thm",200);
  if(mode==1) {
   fail_state_writes=true;r=psx_import_run(src,dst,NULL,NULL);fail_state_writes=false;assert(r.failed);
   char receipt_path[256];snprintf(receipt_path,sizeof(receipt_path),
    "import/psx3d/source/states/G.sta.thm/%u-%08x.done",12+256*192*2,crcfile(src,"states/G.sta.thm"));
   assert(!has(receipt_path));
   r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && has("states/G.sta") && has("states/G.sta.thm"));
   assert(crcfile(dst,"states/G.sta.thm")==crcfile(src,"states/G.sta.thm"));
  } else if(mode==2) {
   put(dst,"states/G.sta",old,0x440000);thumbnail_file(dst,"states/G.sta.thm",100);
   r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && !has("states/G.sta.psx3d-old") && !has("states/G.sta.thm.psx3d-old"));
   char p[1024];at(p,dst,"states/G.sta");assert(!unlink(p));
   assert(psx_import_recover(dst));r=psx_import_run(src,dst,NULL,NULL);
   assert(!r.failed && !has("states/G.sta") && !has("states/G.sta.psx3d-old"));
  } else {
   write_budget=3*1024*1024;r=psx_import_run(src,dst,NULL,NULL);write_budget=-1;assert(r.failed);no_partials(dst);
   r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && has("states/G.sta.thm"));
  }
 } else if(mode==3) {
  put(src,"states/G.st3",old,0x440000);put(dst,"states/G.st3",new,0x440000);
  r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed);
  hist_entry_t e[10];assert(history_list("G",false,e,10)==1 && !strcmp(e[0].ext,".st3"));
 } else {
  unsigned char *c=calloc(1,128*1024);assert(c);c[0]='M';c[1]='C';c[127]='M'^'C';
  if(mode==5)put(dst,"saves/G.srm",c,128*1024);
  c[128]=0x51;c[255]=0x51;
  if(mode==4)c[383]=0x55; /* runtime-loadable weak directory checksum */
  put(src,"saves/G.srm",c,128*1024);free(c);
  r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && !r.rejected && has("saves/G.srm"));
  assert(crcfile(src,"saves/G.srm")==crcfile(dst,"saves/G.srm"));
  if(mode==5) {hist_entry_t e[20];assert(history_list("G",true,e,20)==1);}
  if(mode==4) {
   assert(r.weak==1); /* visible warning, never silently skipped */
   card_file(src,"saves/G.srm.bak",7);
   strcpy(dst,"BreadBox-strong-recovery");setup(dst);
   r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && !r.weak);
   assert(crcfile(dst,"saves/G.srm")==crcfile(src,"saves/G.srm.bak"));
  }
 }
 r=psx_import_run(src,dst,NULL,NULL);assert(!r.failed && !r.copied);
 free(new);free(old);printf("Review scenario %d: PASS\n",mode);return 0;
}
