/* Test-only PRG observation: compile the real callback unit with this helper.
 * No test hook is compiled into the shipping core. */
#include "../../systems/nes/core/libretro/libretro.cpp"
static retro_environment_t test_environment;
static bool skip_video(unsigned command,void *data) {
 if(command==RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE){*(int *)data=2;return true;}
 return test_environment(command,data);
}
extern "C" bool bb_test_nes_cheats(void)
{
 if(!emu)return false;
 bool lazy=!bb_original_prg;
 uint8_t battery=emu->high_mem()[32];
 retro_cheat_set(19,true,"6020:FF");bb_apply_ram_cheats();
 bool battery_safe=emu->high_mem()[32]==battery;
 retro_cheat_reset();
 if(!lazy || !battery_safe){fprintf(stderr,"NES lazy PRG=%d battery-safe=%d\n",lazy,battery_safe);return false;}
 game_genie_patch_t patch;if(patch.decode("APZLGI"))return false;
 unsigned offset=patch.addr%emu->cart()->prg_size();
 uint8_t original=emu->cart()->prg()[offset];
 if(original==patch.change_to)return false;
 retro_cheat_set(0,true,"APZLGI");
 if(!bb_original_prg)return false;
 if(emu->cart()->prg()[offset]!=patch.change_to)return false;
 retro_cheat_set(1,true,"0010:63+0011:42");bb_apply_ram_cheats();
 if(emu->low_mem()[16]!=99 || emu->low_mem()[17]!=66)return false;
 test_environment=environ_cb;environ_cb=skip_video;emu->low_mem()[16]=0;retro_run();environ_cb=test_environment;
 if(emu->low_mem()[16]!=99)return false;
 retro_cheat_set(0,false,"APZLGI");
 if(emu->cart()->prg()[offset]!=original || bb_original_prg)return false;
 retro_cheat_set(0,true,"APZLGI");retro_cheat_reset();
 if(emu->cart()->prg()[offset]!=original || bb_original_prg)return false;
 emu->low_mem()[16]=1;bb_apply_ram_cheats();if(emu->low_mem()[16]!=1)return false;
 // An 8-letter compare code patches only matching original bytes.
 if(patch.decode("GZUXNGEI") || patch.compare_with<0)return false;
 Nes_Cart cart;if(cart.resize_prg(16384))return false;cart.set_mapper(0,0);
 memset(cart.prg(),patch.compare_with,cart.prg_size());
 if(!patch.apply(cart) || cart.prg()[patch.addr%16384]!=patch.change_to)return false;
 memset(cart.prg(),patch.compare_with^1,cart.prg_size());
 if(patch.apply(cart))return false;
 return true;
}
