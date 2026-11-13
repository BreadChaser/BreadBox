#ifndef BB_RETRO_CTR_H
#define BB_RETRO_CTR_H
#include "retro_host.h"
bb_run_result bb_retro_ctr_run(const bb_launch *,const bb_retro_desc *,char *,size_t);
extern const bb_cheat_ops bb_retro_ctr_cheats;
extern const bb_save_ops bb_retro_ctr_save_ops;
bool bb_retro_ctr_state(int,bool);
bool bb_retro_ctr_key(const char *,unsigned);
#endif
