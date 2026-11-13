#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bb_registry.h"
static const char *const exta[]={".disc",".zip",NULL},*const extb[]={".cart",".zip",NULL};
static int counta(void) {return 4;}
static int countb(void) {return 7;}
static unsigned ticks;
static void tick(void){ticks++;}
static const bb_setting_desc schema[]={{.def={3,7}}};
unsigned bb_platform_model(void){return BB_NEW;}
static const bb_shell_ops a={.count=counta},b={.count=countb};
const bb_system_t bb_system_ps1={.abi=BB_SYSTEM_ABI,.id="first",.tick=tick,.settings=schema,.nsettings=1,.short_name="A",.extensions=exta,.shell=&a};
const bb_system_t bb_system_gba={.abi=BB_SYSTEM_ABI,.id="second",.short_name="B",.extensions=extb,.shell=&b};
static bool enough=true,released,claim_ok=true;
bool bb_platform_claim(const bb_system_t *s) {(void)s;assert(released);return claim_ok;}
void bb_platform_drop(void) {}
int bb_platform_count(const bb_system_t *s) {(void)s;return 0;}
void bb_platform_release_menu(void) {assert(!released);released=true;}
void bb_platform_restore_menu(void) {assert(released);released=false;}
bool bb_platform_budget(const bb_system_t *s) {(void)s;assert(released);return enough;}
int main(void) {
 _Static_assert(BB_SYSTEM_ABI==13,"scoped settings model requires ABI 13");
 assert(bb_system_ps1.settings_model==NULL);
 char counts[64];bb_system_counts(counts,sizeof(counts));assert(!strcmp(counts,"4 A + 7 B"));
 assert(bb_system_total()==11 && bb_system_for_path("test.CART")==&bb_system_gba);
 assert(!bb_system_for_path("ambiguous.zip"));
 assert(bb_setting(&bb_system_ps1,0)==7);
 bb_session_tick();assert(!ticks);
 assert(!bb_system_for_path("test") && !bb_system_for_path("test.unknown"));
 assert(!bb_session_active() && !bb_session_stereo);
 assert(!bb_session_resume_requested());
 bb_system_resume("test.cart",true);assert(bb_session_resume_requested());
 bb_system_resume("test.cart",false);assert(!bb_session_resume_requested());
 bb_launch launch={0};
 assert(bb_session_begin(&bb_system_ps1,&launch));
 assert(!bb_session_begin(&bb_system_gba,&launch));
 bb_session_set_stereo(true);assert(bb_session_stereo);
 assert(bb_session_system()==&bb_system_ps1);bb_session_tick();assert(ticks==1);
 bb_session_end();assert(!bb_session_active() && !bb_session_stereo);
 enough=false;assert(!bb_session_begin(&bb_system_gba,&launch));assert(!bb_session_active());
 assert(!released);enough=true;claim_ok=false;assert(!bb_session_begin(&bb_system_gba,&launch));assert(!released && !bb_session_active());
 claim_ok=true;assert(bb_session_begin(&bb_system_gba,&launch));bb_session_end();
 puts("registry path selection, counts, session exclusivity and stereo cleanup PASS");
}
