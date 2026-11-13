/* Experimental QuickNES adapter. GPL-2.0-or-later. */
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <3ds.h>
#include <stddef.h>
#include <stdint.h>
#include "app_policy.h"
#include "libretro.h"
#include "app_main.h"
#include "bb_system.h"
#include "retro_ctr.h"
#include "bb_profile.h"
extern const bb_retro_core bb_retro_nes;
extern const bb_system_t bb_system_nes;
extern unsigned char __bb_nes_bss_start[],__bb_nes_bss_end[];
static const char *const extensions[]={".nes",NULL};
static const char *const hints[]={"nes","nintendo entertainment system",NULL};
static bb_probe_result probe(const bb_probe_input *in,bb_entry *out)
{
 if(in->head_len<16 || memcmp(in->head,"NES\032",4) || !in->head[4])return BB_PROBE_NO;
 size_t need=16+((in->head[6]&4)?512:0)+(size_t)in->head[4]*16384+(size_t)in->head[5]*8192;
 if(in->size<need)return BB_PROBE_NO;
 snprintf(out->title,sizeof(out->title),"%s",in->name);
 char *dot=strrchr(out->title,'.');if(dot)*dot=0;
 return BB_PROBE_YES;
}
static bool identify(const char *path,const bb_entry *e,bb_content_id *id,char *error,size_t cap)
{
 (void)e;unsigned char buf[4096];FILE *f=fopen(path,"rb");if(!f)return false;
 size_t n=fread(buf,1,sizeof(buf),f);bb_probe_input in={.head=buf,.head_len=n,.name=path};bb_entry entry;
 if(fseek(f,0,SEEK_END) || ftell(f)<0){fclose(f);return false;}
 in.size=ftell(f);
 if(probe(&in,&entry)!=BB_PROBE_YES){fclose(f);snprintf(error,cap,"Invalid iNES cartridge");return false;}
 rewind(f);uint32_t crc=crc32(0,NULL,0);
 while((n=fread(buf,1,sizeof(buf),f)))crc=crc32(crc,buf,n);
 bool ok=!ferror(f);fclose(f);if(!ok)return false;
 snprintf(id->key,sizeof(id->key),"NES-%08lX",(unsigned long)crc);id->crc=crc;return true;
}
static const char *const sizes[]={"Original","Fit","Stretch"};
static const bb_setting_desc settings[]={
 {.key="screen_size",.label="Screen size",.values=sizes,.kind=BB_SET_CHOICE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE,.nvalues=3,.def={1,1}},
 {.key="filter",.label="Smoothing",.kind=BB_SET_TOGGLE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE},
 {.key="audio",.label="Audio",.kind=BB_SET_TOGGLE,.category=BB_CAT_AUDIO,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE,.def={1,1}}
};
static bb_profile profile;
static void settings_open(const char *key){bb_profile_open(&profile,"nes","settings",key,settings,3,g_new3ds?BB_NEW:BB_OLD);}
static int setting_value(unsigned i){if(!profile.count)settings_open(NULL);return i<3?profile.value[i]:0;}
static void setting_change(unsigned i,int dir){if(!profile.count)settings_open(NULL);bb_profile_change(&profile,i,dir);}
static void setting_clear(unsigned i){bb_profile_clear(&profile,i);}
static const char *setting_source(unsigned i){return bb_profile_source(&profile,i);}
static bb_run_result run(const bb_launch *l,char *error,size_t cap)
{
 const bb_retro_desc d={.system=&bb_system_nes,.core=&bb_retro_nes,
   .common_settings=true,
  .system_dir=APP_SD_ROOT "/bios",.max_width=256,.max_height=240,.screen_size=1,
  .keys={[RETRO_DEVICE_ID_JOYPAD_A]=KEY_A,[RETRO_DEVICE_ID_JOYPAD_B]=KEY_B,
  [RETRO_DEVICE_ID_JOYPAD_START]=KEY_START,[RETRO_DEVICE_ID_JOYPAD_SELECT]=KEY_SELECT,
  [RETRO_DEVICE_ID_JOYPAD_UP]=KEY_UP|KEY_CPAD_UP,[RETRO_DEVICE_ID_JOYPAD_DOWN]=KEY_DOWN|KEY_CPAD_DOWN,
  [RETRO_DEVICE_ID_JOYPAD_LEFT]=KEY_LEFT|KEY_CPAD_LEFT,[RETRO_DEVICE_ID_JOYPAD_RIGHT]=KEY_RIGHT|KEY_CPAD_RIGHT}};
 return bb_retro_ctr_run(l,&d,error,cap);
}
static const bb_dev_ops dev={.state=bb_retro_ctr_state,.key=bb_retro_ctr_key};
/* The cardboard box on the shelf: 127 x 178 x 34 mm. */
static const bb_package package={.width=127,.height=178,.depth=34,.style=BB_PACKAGE_CARTON,
 .spine_head=0xff2a2a2e,.band=0xff2a2a2e};
const bb_system_t bb_system_nes={.controls=true,.cheats=&bb_retro_ctr_cheats,
 .settings=settings,.nsettings=3,.setting_value=setting_value,.setting_change=setting_change,.setting_clear=setting_clear,.setting_source=setting_source,.settings_open=settings_open,.abi=BB_SYSTEM_ABI,.id="nes",.name="Nintendo Entertainment System",.short_name="NES",.release_year=1983,.release_month=7,
 .models=BB_MODEL_ALL,.link=BB_LINK_STATIC,.stereo=BB_STEREO_NONE,
 .extensions=extensions,.folder_hints=hints,.probe_bytes=16,.probe=probe,.identify=identify,.run=run,
 .arena_start=__bb_nes_bss_start,.arena_end=__bb_nes_bss_end,
 .cover_db="Nintendo - Nintendo Entertainment System",.dev=&dev,.save=&bb_retro_ctr_save_ops,.package=&package,
 /* Conservative smoke-only ceilings; measure on hardware before shipping. */
 .budget={{.heap_kib=2048,.linear_kib=256,.exec_kib=512},{.heap_kib=2048,.linear_kib=256,.exec_kib=512}}};
