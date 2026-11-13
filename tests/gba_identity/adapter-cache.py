#!/usr/bin/env python3
"""Production adapter identity cache avoids repeat legacy directory resolution."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as td:
 p=Path(td)
 (p/'check.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bb_system.h"
#include "gba_saveid.h"
#include "gba_cart.h"
extern bool bb_gba_identify(const char *,const bb_entry *,bb_content_id *,char *,size_t);
extern void bb_gba_identity_invalidate(void);
static unsigned resolves;
static bool success=true,too_large;
bool gba_cart_too_large(const char *p) {(void)p;return too_large;}
const char *gba_cart_error(gba_cart_result r) {return r==GBA_CART_TOO_LARGE?"over the limit":"?";}
bool gba_saveid_resolve(const char *p,unsigned char h[192],uint32_t *crc,char id[GBA_ID_LEN]) {
 (void)p;(void)h;resolves++;*crc=123;strcpy(id,"gbs-TEST-12345678");return success;
}
int main(void) {
 FILE *f=fopen("rom.gba","wb");assert(f);fputc(1,f);fclose(f);
 bb_content_id a={0},b={0};
 assert(bb_gba_identify("rom.gba",NULL,&a,NULL,0));
 assert(bb_gba_identify("rom.gba",NULL,&b,NULL,0));
 assert(resolves==1 && !memcmp(&a,&b,sizeof(a)));
 f=fopen("rom.gba","ab");assert(f);fputc(2,f);fclose(f);
 assert(bb_gba_identify("rom.gba",NULL,&b,NULL,0) && resolves==2);
 bb_gba_identity_invalidate();
 assert(bb_gba_identify("rom.gba",NULL,&b,NULL,0) && resolves==3);
 bb_gba_identity_invalidate();success=false;
 assert(!bb_gba_identify("rom.gba",NULL,&b,NULL,0));
 assert(!bb_gba_identify("rom.gba",NULL,&b,NULL,0) && resolves==5);
 char error[64]="";
 assert(!bb_gba_identify("rom.gba",NULL,&b,error,sizeof(error)) && !strcmp(error,"Could not identify cartridge"));
 too_large=true;
 assert(!bb_gba_identify("rom.gba",NULL,&b,error,sizeof(error)) && !strcmp(error,"over the limit"));
 puts("production GBA Home identity cache, size/session invalidation, failure retry, over-limit reason PASS");
}
''')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'systems/gba'),str(p/'check.c'),str(r/'systems/gba/sys_gba_scan.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=p,check=True)
