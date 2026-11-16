#include "gba_color.h"
#include "gba_settings.h"
#include "gba_touch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
extern const uint16_t gba_cc_lut[];
static uint16_t lut[65536],soft[65536];
static int side;
static unsigned notifications;
static void changed(void){notifications++;}
int control_side(void){return side;}
void control_side_set(int v){side=!!v;}
static void colors(void)
{
 gba_color_table(lut,GBA_COLOR_ORIGINAL);
 for(unsigned i=0;i<65536;i++)assert(lut[i]==i);
 gba_color_table(lut,-1);
 for(unsigned i=0;i<65536;i++)assert(lut[i]==i);
 gba_color_table(lut,GBA_COLOR_COUNT);
 for(unsigned i=0;i<65536;i++)assert(lut[i]==i);
 gba_color_table(lut,GBA_COLOR_CORRECTED);
 for(unsigned i=0;i<65536;i++)assert(lut[i]==gba_cc_lut[((i&0xffc0)>>1)|(i&31)]);
 gba_color_table(soft,GBA_COLOR_SOFT);
 assert(soft[0xf800]==0xf187 && soft[0x07e0]==0x4725 && soft[0x001f]==0x01fd);
 assert(soft[0x4208]==0x3186 && soft[0x8410]==0x738e);
 for(unsigned i=0;i<65536;i++)for(unsigned k=0;k<3;k++) {
  unsigned shift=k==0?11:k==1?5:0,mask=k==1?63:31;
  unsigned a=i>>shift&mask,b=lut[i]>>shift&mask,c=soft[i]>>shift&mask;
  assert(c>=(a<b?a:b) && c<=(a>b?a:b));
 }
 gba_color_table(lut,GBA_COLOR_BRIGHT);
 assert(!lut[0] && lut[65535]==65535);
 assert(lut[0x4208]==0x52aa && lut[0x8410]==0x94b2);
 for(unsigned i=0;i<65536;i++) {
  assert((lut[i]>>11)>=(i>>11));assert((lut[i]>>5&63)>=(i>>5&63));assert((lut[i]&31)>=(i&31));
 }
 for(unsigned i=1;i<32;i++)assert((lut[i]&31)>=(lut[i-1]&31));
 assert(!strcmp(gba_color_name(-1),"Original (vivid)"));
}
static void uploads(void)
{
 unsigned seed=123;
 for(unsigned mode=0;mode<GBA_COLOR_COUNT;mode++)for(unsigned pad=0;pad<4;pad++)for(unsigned alignment=0;alignment<4;alignment++) {
  unsigned pitch=480+pad;unsigned char *raw=malloc(pitch*160+4);uint16_t *out=malloc(256*256*2);
  for(unsigned i=0;i<pitch*160+4;i++){seed=seed*1664525+1013904223;raw[i]=seed>>24;}
  gba_color_table(lut,mode);memset(out,0xa7,256*256*2);
  gba_color_upload(out,raw+alignment,pitch,lut);
  for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++) {
   unsigned at=((y>>3)*32+(x>>3))*64+(x&1)+((y&1)<<1)+((x&2)<<1)+((y&2)<<2)+((x&4)<<2)+((y&4)<<3);
   uint16_t want=0xa7a7;
   if(x<240 && y<160){uint16_t in;memcpy(&in,raw+alignment+y*pitch+x*2,2);want=lut[in];}
   assert(out[at]==want);
  }
  free(raw);free(out);
 }
}
static void write_settings(const char *text)
{
 FILE *f=fopen("sdmc:/3ds/BreadBox/config/gba.ini","w");assert(f);fputs(text,f);fclose(f);
}
static void settings(void)
{
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/BreadBox",0700);mkdir("sdmc:/3ds/BreadBox/config",0700);
 gba_settings_load();
 assert(gba_settings.screen_size==1 && !gba_settings.smoothing && !gba_settings.color_correction && !gba_settings.frame_blend);
 for(int c=0;c<GBA_COLOR_COUNT;c++) {
  char text[96];snprintf(text,sizeof(text),"color_correction=%d\nsmoothing=1\nframe_blend=1\n",c);write_settings(text);
  gba_settings_load();assert(gba_settings.color_correction==c && gba_settings.smoothing && gba_settings.frame_blend);
  assert(!gba_settings.core_color_correction && !gba_settings.core_frame_blend);
  gba_settings_for_identity("game.gba","gbs-TEST-01");assert(gba_settings.color_correction==c);
  gba_settings_set(GBA_SET_COLOR,1);assert(gba_settings_overridden(GBA_SET_COLOR));
  gba_settings_for_identity("game.gba","gbs-TEST-01");assert(gba_settings.color_correction==(c+1)%GBA_COLOR_COUNT);
  gba_settings_for_identity("other.gba","gbs-TEST-02");assert(gba_settings.color_correction==c);
  gba_settings_for_identity("game.gba","gbs-TEST-01");gba_settings_clear(GBA_SET_COLOR);
  assert(gba_settings.color_correction==c && !gba_settings_overridden(GBA_SET_COLOR));
 }
 write_settings("color_correction=99\nunknown=1\n");gba_settings_load();assert(!gba_settings.color_correction);
 write_settings("color_correction=-1\n");gba_settings_load();assert(!gba_settings.color_correction);
 write_settings("color_correction=1\n");gba_settings_load();assert(gba_settings.color_correction==GBA_COLOR_CORRECTED);
 gba_settings_set(GBA_SET_COLOR,-1);assert(!gba_settings.color_correction);
 gba_settings_set(GBA_SET_COLOR,-1);assert(gba_settings.color_correction==GBA_COLOR_BRIGHT);
 gba_settings_for_identity("game.gba","gbs-TEST-03");
 gba_settings_changed=changed;notifications=0;
 gba_settings_set(GBA_SET_COLOR,1);assert(notifications==1);
 int color=gba_settings.color_correction;
 gba_touch_fps_save_global();
 assert(notifications==1 && gba_settings.color_correction==color && gba_settings_changed==changed);
 gba_settings_changed=NULL;
}
int main(void)
{
 colors();uploads();settings();
 puts("GBA picture: all RGB565 colors, unchanged identity/correction, preset bounds, fused uploads, stored choices and game inheritance PASS");
}
