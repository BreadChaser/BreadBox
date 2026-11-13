#!/usr/bin/env python3
"""Run actual SD mutation/stream receive functions on a temporary filesystem."""
from pathlib import Path
import subprocess,tempfile
src=Path('platform/3ds/devnet.c').read_text()
def fn(start):
 a=src.index(start);return src[a:src.index('\n}',a)+2]
program=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#define PATH_LEN 1024
typedef uint8_t u8; typedef uint64_t u64;
static bool files_busy;
static unsigned char wire[200000];
static size_t available, position;
static char response[2048];
static bool devnet_stopping(void) { return false; }
static u64 osGetTime(void) { return 0; }
static void svcSleepThread(int x) { (void)x; }
static int recv(int fd, void *buf, size_t n, int flags) {
 (void)fd;(void)flags;
 if (n>available-position) n=available-position;
 if (n>137) n=137; /* fragmented transport */
 memcpy(buf,wire+position,n); position+=n; return n;
}
static void send_json(int fd,const char *json) { (void)fd; snprintf(response,sizeof(response),"%s",json); }
'''+fn('static bool sd_path(')+'\n'+fn('static size_t json_quote(')+'\n'+src[src.index('static bool strip_force('):src.index('static bool file_command(')]+r'''
static void expect(bool ok) { assert((strstr(response,"\"ok\":true")!=NULL)==ok); }
static void contents(const char *path,const char *text) {
 char buf[32]={0}; FILE *f=fopen(path,"rb"); assert(f); fread(buf,1,sizeof(buf)-1,f);fclose(f);assert(!strcmp(buf,text));
}
int main(void) {
 assert(!mkdir("sdmc:",0700)); char p[PATH_LEN];
 assert(!sd_path("sdmc:/roms/../a",p,sizeof(p)));
 assert(!sd_path("sdmc:/roms/./a",p,sizeof(p)));
 assert(!sd_path("sdmc:/roms/a\\b",p,sizeof(p)));
 assert(!sd_write_allowed("sdmc:/LuMa/a",false));
 assert(!sd_path("sdmc:/luma./a",p,sizeof(p)));
 assert(!sd_path("sdmc:/Nintendo 3DS /a",p,sizeof(p)));
 assert(!sd_write_allowed("sdmc:/Nintendo 3DS/a",false));
 assert(sd_write_allowed("sdmc:/LuMa/a",true));
 assert(sd_write_allowed("sdmc:/lumax/a",false));
 assert(!sd_write_allowed("sdmc:/",true));
 sd_mutate(1,"mkdir","sdmc:/roms/GBA/nested");expect(true);
 sd_mutate(1,"mkdir","sdmc:/luma/test");expect(false);
 put_file(1,"sdmc:/roms/GBA/nested/test.gba 3",(u8*)"abc",3,false);expect(true);
 contents("sdmc:/roms/GBA/nested/test.gba","abc");
 available=position=0;
 put_file(1,"sdmc:/roms/GBA/nested/test.gba 100",(u8*)"x",1,false);expect(false);
 contents("sdmc:/roms/GBA/nested/test.gba","abc");
 assert(access("sdmc:/roms/GBA/nested/test.gba.part",F_OK));
 put_file(1,"sdmc:/roms/GBA/nested/test.gba 2",(u8*)"xy",2,false);expect(true);
 contents("sdmc:/roms/GBA/nested/test.gba","xy");
 put_file(1,"sdmc:/roms/GBA/nested/test.gba 2",(u8*)"zz",2,true);expect(false);
 for(unsigned i=0;i<sizeof(wire);i++)wire[i]=(i*7)&255;
 available=sizeof(wire);position=7;
 put_file(1,"sdmc:/anything/a.zip 200000",wire,7,false);expect(true);
 FILE *f=fopen("sdmc:/anything/a.zip","rb");assert(f);
 for(unsigned i=0;i<sizeof(wire);i++)assert(fgetc(f)==wire[i]);assert(fgetc(f)==EOF);fclose(f);
 put_file(1,"sdmc:/anything/zero 0",wire,0,false);expect(true);
 put_file(1,"sdmc:/anything/huge 4294967296",wire,0,false);expect(false);
 put_file(1,"sdmc:/luma/test 0",wire,0,false);expect(false);
 put_file(1,"sdmc:/luma/test 0 force",wire,0,false);expect(true);
 sd_mutate(1,"mv","sdmc:/luma/test\tsdmc:/anything/renamed");expect(false);
 sd_mutate(1,"mv","sdmc:/luma/test\tsdmc:/anything/renamed force");expect(true);
 sd_mutate(1,"rm","sdmc:/roms/GBA");expect(false);
 sd_mutate(1,"mv","sdmc:/anything/renamed\tsdmc:/anything/zero");expect(false);
 sd_mutate(1,"rm","sdmc:/anything/renamed");expect(true);
 files_busy=true;
 put_file(1,"sdmc:/anything/blocked 3",(u8*)"abc",3,false);expect(false);
 assert(access("sdmc:/anything/blocked",F_OK));
 sd_mutate(1,"mkdir","sdmc:/busy");expect(false);
 puts("SD paths, protected/force writes, mkdir/rm/mv, fragmented stream, interrupted replacement, size and busy checks: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.c';p.write_text(program)
 subprocess.run(['cc','-Wall','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-g',str(p),'-o',tmp+'/test'],check=True)
 subprocess.run([tmp+'/test'],cwd=tmp,check=True)
