#!/usr/bin/env python3
"""Production CPU thumbnail reader: backup, partial files, cancellation."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/ps1/savestates.c').read_text()
s=s[s.index('static bool thumb_read_cancel('):s.index('bool states_auto_texture(')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef uint16_t u16;typedef uint8_t u8;typedef uint32_t u32;
#define PATH_LEN 512
#define TW 256
#define TH 192
#define STATE_AUTO 10
typedef struct {u32 magic,w,h,saved;} thm_hdr_t;
#define THM_SIZE (sizeof(thm_hdr_t)+TW*TH*2)
static bool thm_head_ok(const void *arg){const thm_hdr_t *h=arg;return h->magic==123 && h->w==TW && h->h==TH;}
static void slot_path(char *p,size_t n,const char *prefix,int slot,bool thumb){(void)slot;snprintf(p,n,"%s.st10%s",prefix,thumb?".thm":"");}
static bool thumb_read(const char *prefix,int slot,u16 *px,thm_hdr_t *h,bool skip){(void)prefix;(void)slot;(void)px;(void)h;(void)skip;assert(!"worker must use cancellable reader");return false;}
static int checks,limit;
static bool cancel(void *arg){(void)arg;return ++checks>=limit;}
'''
main=r'''
int main(void){
 u16 *px=calloc(TW*TH,2);assert(px);u32 saved;bool picture;
 FILE *f=fopen("test.st10","wb");assert(f);fclose(f);
 thm_hdr_t h={123,TW,TH,456};f=fopen("test.st10.thm","wb");assert(f);fputs("cut off",f);fclose(f);
 f=fopen("test.st10.thm.bak","wb");assert(f);fwrite(&h,sizeof(h),1,f);for(int i=0;i<TW*TH;i++)px[i]=i;fwrite(px,TW*TH*2,1,f);fclose(f);
 memset(px,0,TW*TH*2);checks=0;limit=1000;
 assert(states_auto_read("test",&saved,px,&picture,cancel,NULL) && picture && saved==456 && px[256]==256);
 checks=0;limit=1;picture=true;assert(!states_auto_read("test",&saved,px,&picture,cancel,NULL) && !picture && checks==1);
 checks=0;limit=12;assert(!states_auto_read("test",&saved,px,&picture,cancel,NULL) && !picture);assert(checks<=14);
 remove("test.st10");checks=0;limit=1000;assert(!states_auto_read("test",&saved,px,&picture,cancel,NULL));free(px);
 puts("Continue CPU I/O: valid backup, truncated current, no state, between-read cancellation PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+s+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
